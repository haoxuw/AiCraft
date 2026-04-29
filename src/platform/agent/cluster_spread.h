#pragma once

// ClusterSpread — idle-NPC cluster reflex (case 1 + case 2 of docs/29 §7).
//
// Self-contained. Owns its phase accumulator + per-entity cooldown map so
// AgentClient doesn't have to. Single entry point: tick(dt, server, fn).
//
// Why a class and not a free function:
//   * Phase accumulator and cooldown map ARE state — they belong with the
//     algorithm, not on AgentClient (which had no other reason to know
//     about them).
//   * Config is tunable in one place. AgentClient's tick() becomes a
//     one-liner; tuning happens here without recompiling the agent loop.
//   * forget(eid) is the natural lifecycle hook — when an agent is
//     removed, AgentClient calls forget once instead of erasing from a
//     ClusterSpread-owned map directly.
//
// Algorithm (unchanged from the old phaseReactSeparation):
//
//   case 1 — overlap: any pair-wise overlap (two clustered idle units, or
//   a unit standing in another's collision box) gets a kick proportional
//   to the overlap depth. Pure geometry, fires regardless of who's moving
//   — this gives clusters the SC2 "liquid spread" feel.
//
//   case 2 — incoming pusher: an idle self with a moving neighbor on a
//   near-term collision course shuffles aside. Kick magnitude is a
//   fraction (transferFraction) of the PUSHER's speed, so a slow walker
//   nudges gently while a sprinter shoves harder. Chains naturally:
//   once the recipient has velocity, it becomes the next pusher.
//
// Sweep is O(A·N) and rate-limited to phasePeriodSec; per-agent kick
// cooldown is reactCooldownSec.

#include "agent/separation.h"        // computeOverlapKick / computeReactKick
#include "logic/action.h"
#include "logic/entity.h"
#include "net/server_interface.h"
#include "debug/perf_registry.h"

#include <cmath>
#include <unordered_map>
#include <utility>

namespace solarium {

class ClusterSpread {
public:
	struct Config {
		// Below this XZ speed (m²/s²) the entity is treated as idle and
		// becomes a candidate for both overlap and react kicks. (0.2 m/s)².
		float idleVelSq         = 0.04f;
		// Per-agent throttle: don't re-kick within this window. Prevents
		// a tight cluster from emitting one Move per pass per pair.
		float reactCooldownSec  = 0.4f;
		// How far to look for overlap-causing neighbors (m). Larger than
		// any reasonable bodyRadius sum.
		float queryRadius       = 6.0f;
		// Sweep cadence — the only "expensive" work runs at this period.
		// 0.1 = 10 Hz, plenty for a reflex pass.
		float phasePeriodSec    = 0.1f;
		// Force constants. case 1: kick = depth_m × overlapForce ≈ m/s.
		// case 2: kick = pusher_speed × transferFraction.
		float overlapForce      = 6.0f;
		float transferFraction  = 0.3f;
	};

	ClusterSpread() = default;
	explicit ClusterSpread(Config cfg) : m_cfg(cfg) {}

	Config&       config()       { return m_cfg; }
	const Config& config() const { return m_cfg; }

	// Drop per-entity bookkeeping. AgentClient calls this when an agent
	// is removed so the cooldown map stays bounded.
	void forget(EntityId eid) { m_cooldown.erase(eid); }

	// Drive the sweep. `forEachHostedEid` lets the caller (AgentClient)
	// keep ownership of its agents map — we only need a stream of the
	// EntityIds it hosts. The callable receives a visitor `f(EntityId)`
	// and is expected to invoke it once per hosted entity.
	//
	// `dt` is the agent-tick delta. Cheap to call every tick — the
	// phasePeriodSec gate makes the heavy work runs at ~10 Hz.
	template <class HostedFn>
	void tick(float dt, ServerInterface& server, HostedFn&& forEachHostedEid) {
		m_phaseAccum += dt;
		if (m_phaseAccum < m_cfg.phasePeriodSec) return;
		const float windowDt = m_phaseAccum;
		m_phaseAccum = 0.0f;

		forEachHostedEid([&](EntityId eid) {
			processOne(eid, windowDt, server);
		});
	}

private:
	// Single-entity reflex. Extracted from the lambda so the loop body
	// is one method call — easier to step in a debugger and lets the
	// per-entity decision tree fit on one screen.
	void processOne(EntityId eid, float windowDt, ServerInterface& server) {
		Entity* self = server.getEntity(eid);
		if (!self || self->removed)        return;
		if (!self->def().isLiving())       return;

		const float vSq = self->velocity.x * self->velocity.x
		                + self->velocity.z * self->velocity.z;
		if (vSq > m_cfg.idleVelSq)         return;     // behavior is driving

		float& cd = m_cooldown[eid];
		cd -= windowDt;
		if (cd > 0)                        return;

		const auto neighbors  = gatherSepNeighbors(server, *self,
		                                            m_cfg.queryRadius);
		const float selfRadius = sepRadiusOf(self->def());

		const glm::vec3 kick = computeKick(eid, *self, selfRadius, neighbors);
		if (kick.x == 0.0f && kick.z == 0.0f) return;

		ActionProposal p;
		p.type       = ActionProposal::Move;
		p.actorId    = eid;
		p.desiredVel = kick;
		server.sendAction(p);

		cd = m_cfg.reactCooldownSec;
		PERF_COUNT("client.steering.react_kicks");
	}

	// Pure-function kick computation: case 1 wins over case 2 because
	// existing overlap is geometrically more urgent than anticipated.
	// Returns {0,0,0} when no kick is needed.
	glm::vec3 computeKick(EntityId eid, const Entity& self,
	                      float selfRadius,
	                      const std::vector<SepNeighbor>& neighbors) const {
		// case 1: pair-wise overlap depth.
		const glm::vec3 ovr = computeOverlapKick(eid, self.position, selfRadius,
		                                          neighbors);
		const float ovrMag = std::sqrt(ovr.x * ovr.x + ovr.z * ovr.z);
		if (ovrMag > 1e-4f) {
			const glm::vec3 dir = ovr / ovrMag;
			return dir * (ovrMag * m_cfg.overlapForce);
		}

		// case 2: incoming pusher → transfer a fraction of pusher's speed.
		const glm::vec3 rea = computeReactKick(eid, self.position, selfRadius,
		                                        neighbors);
		const float reaMag = std::sqrt(rea.x * rea.x + rea.z * rea.z);
		if (reaMag > 1e-4f) {
			const glm::vec3 dir = rea / reaMag;
			return dir * (reaMag * m_cfg.transferFraction);
		}
		return {0, 0, 0};
	}

	Config                                  m_cfg;
	std::unordered_map<EntityId, float>     m_cooldown;
	float                                   m_phaseAccum = 0.0f;
};

}  // namespace solarium
