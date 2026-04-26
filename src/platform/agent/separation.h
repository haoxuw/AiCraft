#pragma once

// Client-side soft entity-vs-entity collision (Phase A).
//
// Pure function: takes the velocity an agent (NPC or player) was about to
// submit in a TYPE_MOVE proposal and returns a nudged version that anticipates
// nearby entities and respects walls. The server stays authoritative; this
// helper only biases `desiredVel` before sendAction.
//
// Algorithm: time-to-collision (Karamouzas-Berseth-Guy 2014) with a
// penetration-spring fallback, asymmetric moving-vs-stationary weights, a
// deterministic head-on tie-breaker, and cardinal-axis wall projection.
// Math doc: src/platform/docs/29_ENTITY_SEPARATION.md.

#include "logic/types.h"
#include "logic/entity.h"
#include "logic/physics.h"
#include "net/server_interface.h"
#include "debug/perf_registry.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace civcraft {

struct SepNeighbor {
	EntityId  eid    = ENTITY_NONE;
	glm::vec3 pos    = {0, 0, 0};
	glm::vec3 vel    = {0, 0, 0};   // intent if known, else measured velocity
	float     radius = 0.4f;        // XZ-projected (max of x/z half-extent)
};

struct SepConfig {
	float tauMax          = 2.0f;    // look-ahead horizon (s)
	float tauMin          = 0.1f;    // 1/τ singularity clip (s)
	float kAnticip        = 4.0f;    // anticipatory gain
	float kPenetration    = 8.0f;    // penetration spring gain
	float slack           = 0.05f;   // padding on combined radius (m)
	float idleVelEps      = 0.05f;   // intent-vel threshold for the asymmetric table
	float parallelEps     = 1e-4f;   // |v_rel|² floor for τ
	float headOnDot       = 0.95f;   // |n_τ · v̂| above this triggers tie-breaker
	// |Δv| cap relative to walk_speed. The bias from separation can never
	// exceed the unit's own movement budget; the user's intent passes
	// through uncapped (no `speedCapFrac`).
	float deltaCapFrac    = 1.0f;
	float wallProbeMargin = 0.1f;    // probe distance past radius into walls (m)
	// Per-eid low-pass on Δv. dv_out = α·dv_new + (1-α)·dv_prev. Smooths the
	// τ_max boundary discontinuity so NPC walk-cycle velocity doesn't twitch
	// when a neighbor enters/exits range. α=0.5 ≈ one-frame half-life.
	float dvLpfAlpha      = 0.5f;
};

struct SepStats {
	int   pairsConsidered = 0;
	int   pairsEmit       = 0;       // produced nonzero contribution
	int   wallBlocked     = 0;       // axis components zeroed by walls
	float forceMaxMag     = 0.0f;    // max |Δv| across calls in this window
};

// Apply soft separation to `selfVelIntent` (XZ only; y is preserved verbatim).
// `isSolid` is the same predicate used by moveAndCollide. `selfBoxHeight` is
// the AABB height for the wall probe — pass `def.collision_box_max.y - min.y`.
// `stepHeight` is `MoveParams::stepHeight` from physics.h (≈1.0 for Living);
// the wall probe raises by this amount so step-uppable obstacles read as free.
// `dvPrev` is the per-caller LPF state (`α·dv_new + (1-α)·dvPrev`); zero on
// first call, then carry between frames.
//
// Per-frame cost is O(neighbors). Caller owns the neighbor gather (typically
// via forEachEntity + a radius filter) and the dvPrev storage.
inline glm::vec3 applySeparation(
	EntityId                       selfEid,
	glm::vec3                      selfPos,
	glm::vec3                      selfVelIntent,
	float                          selfRadius,
	float                          selfWalkSpeed,
	float                          selfBoxHeight,
	float                          stepHeight,
	const std::vector<SepNeighbor>& neighbors,
	const BlockSolidFn&            isSolid,
	glm::vec2&                     dvPrev,
	const SepConfig&               cfg   = {},
	SepStats*                      stats = nullptr)
{
	glm::vec2 v_i  = {selfVelIntent.x, selfVelIntent.z};
	float selfSpeedSq = v_i.x * v_i.x + v_i.y * v_i.y;
	float idleSq      = cfg.idleVelEps * cfg.idleVelEps;

	// §5: w_i = 0 in both rows where self is not moving — no force. Clear the
	// LPF state so the next move starts fresh (don't carry stale push from a
	// scenario whose neighbors may have moved on).
	if (selfSpeedSq <= idleSq) { dvPrev = {0.0f, 0.0f}; return selfVelIntent; }
	if (selfWalkSpeed <= 0.0f) { dvPrev = {0.0f, 0.0f}; return selfVelIntent; }

	const glm::vec2 p_i = {selfPos.x, selfPos.z};
	glm::vec2 dv = {0, 0};

	for (const SepNeighbor& n : neighbors) {
		if (n.eid == selfEid) continue;
		if (stats) stats->pairsConsidered++;

		const glm::vec2 p_j = {n.pos.x, n.pos.z};
		const glm::vec2 v_j = {n.vel.x, n.vel.z};
		float otherSpeedSq  = v_j.x * v_j.x + v_j.y * v_j.y;
		bool  otherMoving   = otherSpeedSq > idleSq;

		// §5 asymmetric table — self is known to be moving here.
		float w = otherMoving ? 0.5f : 1.0f;

		glm::vec2 d = p_i - p_j;
		glm::vec2 v = v_i - v_j;
		float R   = selfRadius + n.radius + cfg.slack;
		float dd  = d.x * d.x + d.y * d.y;
		float c   = dd - R * R;

		// §4 — penetration fallback when already overlapping.
		if (c <= 0.0f) {
			float dist    = std::sqrt(std::max(dd, 1e-8f));
			float overlap = R - dist;
			if (overlap > 0.0f) {
				// Coincident agents: pick a deterministic axis from eid parity
				// so two stacked entities still separate instead of locking.
				glm::vec2 dir;
				if (dist > 1e-4f) {
					dir = d / dist;
				} else {
					dir = (selfEid & 1u) ? glm::vec2(1, 0) : glm::vec2(0, 1);
				}
				dv += cfg.kPenetration * w * overlap * dir;
				if (stats) stats->pairsEmit++;
			}
			continue;
		}

		// §1 TTC quadratic.
		float a = v.x * v.x + v.y * v.y;
		float b = d.x * v.x + d.y * v.y;
		if (a < cfg.parallelEps) continue;   // parallel motion → τ = ∞
		if (b >= 0.0f)           continue;   // diverging → τ = ∞
		float disc = b * b - a * c;
		if (disc < 0.0f)         continue;

		float tau = (-b - std::sqrt(disc)) / a;
		if (tau >= cfg.tauMax)   continue;
		if (tau < 0.0f) tau = 0.0f;

		// §2 anticipatory normal at the predicted contact.
		glm::vec2 contact = d + v * tau;
		glm::vec2 n_tau   = contact * (1.0f / R);

		float g_tau = 1.0f / std::max(tau, cfg.tauMin) - 1.0f / cfg.tauMax;
		if (g_tau <= 0.0f) continue;

		glm::vec2 force = (cfg.kAnticip * w * g_tau) * n_tau;

		// §3 head-on tie-breaker — both n_τ and v̂ ~parallel, agents lock without it.
		float vMag = std::sqrt(a);
		if (vMag > 1e-4f) {
			glm::vec2 vHat = v / vMag;
			float dot  = std::abs(n_tau.x * vHat.x + n_tau.y * vHat.y);
			if (dot > cfg.headOnDot) {
				float side = (selfEid & 1u) ? +1.0f : -1.0f;
				glm::vec2 perp = {-n_tau.y, n_tau.x};
				force += (0.5f * cfg.kAnticip * w * side) * perp;
			}
		}

		dv += force;
		if (stats) stats->pairsEmit++;
	}

	// §8 cap |Δv| at the unit's own walk_speed — bias never exceeds the
	// movement budget the unit could produce on its own.
	float maxDelta = cfg.deltaCapFrac * selfWalkSpeed;
	float dvSq     = dv.x * dv.x + dv.y * dv.y;
	if (dvSq > maxDelta * maxDelta) {
		float scale = maxDelta / std::sqrt(dvSq);
		dv *= scale;
	}

	// Per-eid LPF: smooths the τ_max-boundary discontinuity so NPC walk-cycle
	// velocity (broadcast `moveSpeed = |hVel|`) doesn't twitch when a
	// neighbor enters/exits range.
	dv = cfg.dvLpfAlpha * dv + (1.0f - cfg.dvLpfAlpha) * dvPrev;
	dvPrev = dv;

	if (stats) {
		float dvMag = std::sqrt(dv.x * dv.x + dv.y * dv.y);
		if (dvMag > stats->forceMaxMag) stats->forceMaxMag = dvMag;
	}

	glm::vec2 v_new = v_i + dv;

	// §6 wall projection — zero out cardinal components that push into a wall
	// taller than `stepHeight`. Probing AT or BELOW step height would read a
	// 1-block step-up obstacle as a wall and kill `desiredVel.x`, blocking
	// moveAndCollide's step-up. So we probe ABOVE step height: only walls
	// that physics can't step over count here.
	const glm::vec2 axes[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	float probeHeight = std::max(selfBoxHeight - stepHeight, 0.5f);
	for (const glm::vec2& ax : axes) {
		float component = v_new.x * ax.x + v_new.y * ax.y;
		if (component <= 0.0f) continue;
		glm::vec3 probe = selfPos;
		probe.x += ax.x * (selfRadius + cfg.wallProbeMargin);
		probe.z += ax.y * (selfRadius + cfg.wallProbeMargin);
		probe.y += stepHeight;
		if (!isPositionBlocked(isSolid, probe, selfRadius, probeHeight)) continue;
		v_new.x -= component * ax.x;
		v_new.y -= component * ax.y;
		if (stats) stats->wallBlocked++;
	}

	// No final speed cap — user/AI intent passes through; only Δv is bounded.
	return {v_new.x, selfVelIntent.y, v_new.y};
}

// React-kick for sleeping NPCs. Mirrors §7 of the math doc — when self is at
// rest but a moving neighbor is on a near-term collision course, return a unit
// XZ direction the NPC should shuffle in. {0,0,0} = no kick needed.
//
// Caller multiplies by a desired kick speed (e.g. 0.5 · walk_speed) and feeds
// into sendMove. This is intentionally separate from applySeparation: the
// asymmetric-weight table says w_self=0 when self is idle, so applySeparation
// returns no force in that case. This function fills the "shuffle out of the
// way" gap that the asymmetric table deliberately leaves open.
inline glm::vec3 computeReactKick(
	EntityId                       selfEid,
	glm::vec3                      selfPos,
	float                          selfRadius,
	const std::vector<SepNeighbor>& neighbors,
	float                          tauReact = 0.5f,
	float                          idleVelEps = 0.05f,
	float                          slack = 0.05f)
{
	const glm::vec2 p_i = {selfPos.x, selfPos.z};
	float bestTau = tauReact;
	glm::vec2 bestKick = {0, 0};
	float idleSq = idleVelEps * idleVelEps;

	for (const SepNeighbor& n : neighbors) {
		if (n.eid == selfEid) continue;
		glm::vec2 v_j = {n.vel.x, n.vel.z};
		float vMagSq = v_j.x * v_j.x + v_j.y * v_j.y;
		if (vMagSq <= idleSq) continue;            // neighbor not approaching

		glm::vec2 p_j = {n.pos.x, n.pos.z};
		glm::vec2 d  = p_i - p_j;                  // self relative to other
		glm::vec2 v  = -v_j;                       // selfVel = 0
		float R   = selfRadius + n.radius + slack;
		float dd  = d.x * d.x + d.y * d.y;
		float c   = dd - R * R;

		// Already overlapping → kick straight away from neighbor's current pos.
		if (c <= 0.0f) {
			float dist = std::sqrt(std::max(dd, 1e-8f));
			glm::vec2 dir = (dist > 1e-4f) ? d / dist
			                               : ((selfEid & 1u) ? glm::vec2(1, 0)
			                                                 : glm::vec2(0, 1));
			bestKick = dir;
			bestTau  = 0.0f;
			continue;
		}

		float a = v.x * v.x + v.y * v.y;
		float b = d.x * v.x + d.y * v.y;
		if (a < 1e-6f) continue;
		if (b >= 0.0f) continue;                   // diverging
		float disc = b * b - a * c;
		if (disc < 0.0f) continue;
		float tau = (-b - std::sqrt(disc)) / a;
		if (tau >= bestTau) continue;              // we already have a sooner one

		glm::vec2 contact = d + v * tau;
		float cMag = std::sqrt(contact.x * contact.x + contact.y * contact.y);
		if (cMag < 1e-4f) continue;
		bestKick = contact / cMag;                 // unit n_τ
		bestTau  = tau;
	}

	return {bestKick.x, 0.0f, bestKick.y};
}

// Drain a SepStats into the perf registry. Zero-cost when CIVCRAFT_PERF is off.
inline void recordSepPerf(const SepStats& s) {
	PERF_COUNT_BY("client.steering.separation_pairs",   s.pairsConsidered);
	PERF_COUNT_BY("client.steering.separation_emit",    s.pairsEmit);
	PERF_COUNT_BY("client.steering.separation_blocked", s.wallBlocked);
	if (s.pairsEmit > 0)
		PERF_RECORD_MS("client.steering.separation_force_max", s.forceMaxMag);
}

// Helpers ---------------------------------------------------------------------

// XZ-projected radius from a collision_box (max of x/z half-extents). Used as
// `selfRadius` and per-neighbor radius. Items/structures pass through here too;
// callers filter them out before calling applySeparation.
inline float sepRadiusOf(const EntityDef& def) {
	float rx = (def.collision_box_max.x - def.collision_box_min.x) * 0.5f;
	float rz = (def.collision_box_max.z - def.collision_box_min.z) * 0.5f;
	return std::max(rx, rz);
}

inline float sepHeightOf(const EntityDef& def) {
	return def.collision_box_max.y - def.collision_box_min.y;
}

// Linear scan over the entity table; only Living entities push. queryRadius
// should be ≥ tauMax · (selfWalkSpeed + neighborMaxSpeed) + radii — 8 m is
// fine for default walk speeds and τ_max=2 s.
inline std::vector<SepNeighbor> gatherSepNeighbors(
	ServerInterface& server,
	const Entity&    self,
	float            queryRadius)
{
	std::vector<SepNeighbor> out;
	float r2 = queryRadius * queryRadius;
	server.forEachEntity([&](Entity& e) {
		if (e.id() == self.id() || e.removed)   return;
		if (!e.def().isLiving())                return;
		glm::vec3 d = e.position - self.position;
		// Y-distance gate is generous — vertical neighbors don't push but we
		// still skip far-stacked entities (1 chunk vertical) to keep the list small.
		if (std::abs(d.y) > 4.0f)               return;
		float dxz2 = d.x * d.x + d.z * d.z;
		if (dxz2 > r2)                           return;
		out.push_back({e.id(), e.position, e.velocity, sepRadiusOf(e.def())});
	});
	return out;
}

} // namespace civcraft
