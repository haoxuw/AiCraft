#pragma once

// Per-entity AI controller. Owns one NPC's Plan state and is the sole mutator
// of its own schedule flags.
//
// Scheduling model: every Agent exposes needsCompute() → {None, React, Decide}.
// AgentClient walks agents in a stable order and dispatches the requested kind
// to the DecideWorker. There is no queue — membership in the scheduler's
// "needy" set is derived from needsCompute() after any state-changing call.
//
// State rules:
//   * Plan state (m_plan, m_stepIndex, m_watch, m_viz, m_goalText) mutates
//     only through this class.
//   * m_needsDecide flips true on plan completion / interrupt / error / resume
//     / discovery. It flips false only when a Plan actually gets installed
//     (onDecideResult). Dispatching alone does NOT clear it — the in-flight
//     bool is what guards against double-dispatch, so a stale-dropped result
//     leaves the flag intact and the agent stays in set for the next visit.
//   * m_dirty (react signal pending) + m_reactCooldown gate React.
//   * m_overridePauseTimer > 0 suppresses all compute (player is controlling).
//   * m_reactInFlight / m_decideInFlight: worker has an outstanding request.
//     Cleared unconditionally when a result of that kind returns (stale or
//     not) — the worker returns exactly one result per dispatch.

#include "logic/entity.h"
#include "logic/action.h"
#include "net/server_interface.h"
#include "server/behavior.h"
#include "server/python_bridge.h"  // BehaviorHandle
#include "agent/outcome.h"
#include "debug/move_stuck_log.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace civcraft {

// Visualisation snapshot — read by the world renderer to draw waypoint dashes
// and the action target marker.
struct PlanViz {
	std::vector<glm::vec3> waypoints;
	PlanStep::Type         actionType = PlanStep::Move;
	glm::vec3              actionPos  = {0, 0, 0};
	bool                   hasAction  = false;
};

// Inspector telemetry. Plan-step counters + decide-rate metric + stuck timer.
// Everything else (goalText, behaviorId, error) lives on Entity directly.
struct PlanProgress {
	bool  registered         = false;
	int   stepIndex          = 0;
	int   totalSteps         = 0;
	float decideRatePerMin   = 0.0f;  // EMA — target ≤ 2/min
	float stuckAccum         = 0.0f;
	float overridePauseTimer = 0.0f;
};

class Agent {
public:
	// What the scheduler should dispatch for this agent on its next visit.
	// React wins over Decide per-visit (signals are "right now" events).
	enum class ComputeKind { None, React, Decide };

	// Step-evaluation tunables.
	static constexpr float kArriveEps          = 1.5f;
	static constexpr float kStillEps           = 0.1f;
	static constexpr float kStuckSeconds       = 2.0f;
	static constexpr float kReachHarvest       = 3.0f;
	static constexpr float kReachAttack        = 2.5f;
	static constexpr float kDefaultIdleHoldSec = 30.0f;
	static constexpr float kOverridePauseSec   = 3.14f;
	static constexpr float kForeverPauseSec    = 1.0e9f;
	// Per-agent rate limit for react(signal). Load-bearing: without this,
	// a chatty signal source can pin an agent to React and starve Decide.
	static constexpr float kReactCooldownSec   = 0.5f;

	Agent(EntityId eid, std::string behaviorId, BehaviorHandle handle)
		: m_eid(eid),
		  m_behaviorId(std::move(behaviorId)),
		  m_handle(handle) {
		// First-time discovery acts like a Success outcome with reason
		// "discovery"; Python's callDecide maps this to outcome="success".
		m_lastOutcome.outcome = StepOutcome::Success;
		m_lastOutcome.reason  = "discovery";
		m_needsDecide         = true;
	}

	// ── Identity / handle plumbing ────────────────────────────────────────
	EntityId            id()         const { return m_eid; }
	const std::string&  behaviorId() const { return m_behaviorId; }
	BehaviorHandle      handle()     const { return m_handle; }
	void                setHandle(BehaviorHandle h) { m_handle = h; }

	// ── Read-only inspection ──────────────────────────────────────────────
	const PlanViz& viz() const { return m_viz; }
	bool           hasPlan() const { return !m_plan.empty(); }
	const std::string& goalText() const { return m_goalText; }

	PlanProgress progress() const {
		PlanProgress p;
		p.registered         = true;
		p.stepIndex          = m_stepIndex;
		p.totalSteps         = (int)m_plan.size();
		p.decideRatePerMin   = m_rate.emaPerMin;
		p.stuckAccum         = m_stuckAccum;
		p.overridePauseTimer = m_overridePauseTimer;
		return p;
	}

	// ── Scheduler contract ────────────────────────────────────────────────

	// What, if anything, should the scheduler dispatch for this agent next.
	// React strictly beats Decide (signals represent "right now" events).
	ComputeKind needsCompute() const {
		if (m_overridePauseTimer > 0.0f) return ComputeKind::None;
		if (m_dirty && m_reactCooldown <= 0.0f) return ComputeKind::React;
		if (m_needsDecide) return ComputeKind::Decide;
		return ComputeKind::None;
	}

	bool reactInFlight()  const { return m_reactInFlight; }
	bool decideInFlight() const { return m_decideInFlight; }

	const LastOutcome& lastOutcome() const { return m_lastOutcome; }

	// Consumed by the dispatcher when building a React request.
	std::string takeSignalKind()    { return std::move(m_dirtyKind); }
	std::vector<std::pair<std::string, std::string>> takeSignalPayload() {
		return std::move(m_dirtyPayload);
	}

	// Scheduler calls this exactly once at dispatch time. For React we also
	// clear the dirty bit and arm the cooldown — the signal we just packaged
	// is now the worker's problem.
	void markDispatched(ComputeKind kind, float now) {
		if (kind == ComputeKind::React) {
			m_reactInFlight = true;
			m_dirty         = false;
			m_dirtyKind.clear();
			m_dirtyPayload.clear();
			m_reactCooldown = kReactCooldownSec;
		} else if (kind == ComputeKind::Decide) {
			m_decideInFlight = true;
		}
		m_rate.onDecide(now);
	}

	// Called from drainWorkerResults when a result of the given kind comes
	// back, regardless of freshness. Pair with markDispatched.
	void clearInFlight(bool fromReact) {
		if (fromReact) m_reactInFlight  = false;
		else           m_decideInFlight = false;
	}

	float decideRatePerMin() const { return m_rate.emaPerMin; }

	// ── Plan-state writers ────────────────────────────────────────────────

	// Sole entry point for installing a new Plan. Called from drainWorker
	// after generation-stale filtering.
	void onDecideResult(Plan plan, std::string goalText, Entity& e) {
		m_plan         = std::move(plan);
		m_stepIndex    = 0;
		m_goalText     = std::move(goalText);
		m_watch        = StepWatch{};
		m_needsDecide  = false;          // we have a plan now
		e.goalText     = m_goalText;
		e.hasError     = false;
		e.errorText.clear();
		rebuildViz();
	}

	// Python raised. Surface on entity and mark for retry.
	void onDecideError(const std::string& err, Entity& e) {
		e.goalText    = "ERROR: " + err.substr(0, 60);
		e.hasError    = true;
		e.errorText   = err;
		setNeedsDecide(StepOutcome::Failed, "decide_error");
	}

	// Player click on owned NPC → one-shot Move, arm obey-pause.
	void applyOverride(glm::vec3 goal, Entity& e) {
		clearPlan();
		m_plan.push_back(PlanStep::move(goal));
		m_goalText            = "player_override";
		m_overridePauseTimer  = kOverridePauseSec;
		m_needsDecide         = false;   // plan exists
		rebuildViz();
		e.goalText = m_goalText;
	}

	// Control-mode entry: player is driving this entity. No compute until resume.
	void pause(Entity& e) {
		clearPlan();
		m_goalText            = "controlled";
		m_overridePauseTimer  = kForeverPauseSec;
		m_needsDecide         = false;
		m_dirty               = false;
		m_dirtyKind.clear();
		m_dirtyPayload.clear();
		e.goalText = m_goalText;
	}

	// Control-mode exit. Ask for a fresh decide immediately on next visit.
	void resume() {
		m_overridePauseTimer = 0.0f;
		setNeedsDecide(StepOutcome::Success, "interrupt:resume");
	}

	// External interrupt (network-layer S_INTERRUPT).
	void onInterrupt(const std::string& reason) {
		interruptPlan(reason);
	}

	// World event broadcast (AgentClient::onWorldEvent).
	void onWorldEvent(const std::string& kind) {
		interruptPlan(kind);
	}

	// ── Signal handling ───────────────────────────────────────────────────
	void onSignal(std::string kind,
	              std::vector<std::pair<std::string, std::string>> payload) {
		// Latest-wins: fresher signals overwrite older ones.
		m_dirtyKind    = std::move(kind);
		m_dirtyPayload = std::move(payload);
		m_dirty        = true;
	}

	// Per-tick timer maintenance. Call once per tick, before dispatch decisions.
	void tickTimers(float dt) {
		if (m_reactCooldown > 0.0f)       m_reactCooldown      -= dt;
		if (m_overridePauseTimer > 0.0f
		 && m_overridePauseTimer < kForeverPauseSec * 0.5f) {
			m_overridePauseTimer -= dt;
			if (m_overridePauseTimer < 0.0f) m_overridePauseTimer = 0.0f;
		}
	}

	// HP-drop watchdog. Called every tick by the orchestrator.
	void scanForInterrupts(Entity& e) {
		int curHp = e.hp();
		bool hpDropped = (m_prevHp > 0 && curHp < m_prevHp);
		m_prevHp = curHp;
		if (m_plan.empty()) return;
		if (hpDropped) interruptPlan("hp");
	}

	// ── Per-tick plan driver ──────────────────────────────────────────────
	void tickPlan(float dt, ServerInterface& server) {
		if (m_plan.empty()) return;

		Entity* e = server.getEntity(m_eid);
		if (!e || e->removed) return;

		if (m_stepIndex >= (int)m_plan.size()) {
			finishPlan(StepOutcome::Success, std::string{}, server);
			return;
		}

		PlanStep& step = m_plan[m_stepIndex];
		if (!m_watch.initialized) {
			m_watch = StepWatch{};
			m_watch.initialized = true;
			if (step.type == PlanStep::Attack) {
				if (Entity* t = server.getEntity(step.targetEntity))
					m_watch.prevTargetHP = t->hp();
			}
		}

		// Attack target evaporated mid-step.
		if (step.type == PlanStep::Attack) {
			Entity* t = server.getEntity(step.targetEntity);
			if (!t || t->removed) {
				interruptPlan("target_gone");
				return;
			}
		}

		// Anchored Move target gone → re-decide. "Gone" = erased, flagged
		// removed, or dead; the dead-but-not-erased window is ~0.5s.
		if (step.type == PlanStep::Move && step.anchorEntityId != ENTITY_NONE) {
			Entity* t = server.getEntity(step.anchorEntityId);
			if (!t || t->removed || t->hp() <= 0) {
				interruptPlan("anchor_gone");
				return;
			}
		}

		// Harvest block already gone → step succeeds immediately (avoids
		// spamming Convert proposals that the server rejects as SourceBlockGone).
		if (step.type == PlanStep::Harvest) {
			glm::ivec3 bp = glm::ivec3(glm::floor(step.targetPos));
			BlockId bid = server.chunks().getBlock(bp.x, bp.y, bp.z);
			if (bid == BLOCK_AIR) {
				advanceStep();
				if (m_stepIndex >= (int)m_plan.size())
					finishPlan(StepOutcome::Success, std::string{}, server);
				return;
			}
		}

		StepOutcome outcome = evaluateStep(step, *e, dt);
		switch (outcome) {
		case StepOutcome::InProgress:
			applyStep(step, *e, server);
			break;
		case StepOutcome::Success:
			advanceStep();
			if (m_stepIndex >= (int)m_plan.size())
				finishPlan(StepOutcome::Success, std::string{}, server);
			break;
		case StepOutcome::Failed:
			finishPlan(StepOutcome::Failed, m_watch.failReason, server);
			break;
		}
	}

private:
	struct StepWatch {
		bool        initialized   = false;
		bool        modeDetected  = false;
		bool        isIdleHold    = false;
		float       stillAccum    = 0.0f;
		float       progress      = 0.0f;
		int         prevTargetHP  = 0;
		std::string failReason;
	};

	struct DecideRateTracker {
		float lastDecideTime = -1.0f;
		float emaPerMin      = 0.0f;

		void onDecide(float now) {
			if (lastDecideTime < 0.0f) { lastDecideTime = now; return; }
			float gap = std::max(now - lastDecideTime, 0.001f);
			float instant = 60.0f / gap;
			constexpr float kAlpha = 0.2f;
			emaPerMin = emaPerMin <= 0.0f
				? instant
				: emaPerMin * (1.0f - kAlpha) + instant * kAlpha;
			lastDecideTime = now;
		}
	};

	// The single chokepoint for requesting a decide.
	void setNeedsDecide(StepOutcome outcome, std::string reason,
	                    std::string goalTextForOutcome = {},
	                    int stepTypeRaw = 0) {
		m_lastOutcome.outcome     = outcome;
		m_lastOutcome.reason      = std::move(reason);
		m_lastOutcome.goalText    = std::move(goalTextForOutcome);
		m_lastOutcome.stepTypeRaw = stepTypeRaw;
		m_needsDecide = true;
	}

	void finishPlan(StepOutcome outcome, std::string reason,
	                ServerInterface& server) {
		PlanStep::Type lastType = PlanStep::Move;
		bool wasAnchored = false;
		if (!m_plan.empty()) {
			int idx = std::min(m_stepIndex, (int)m_plan.size() - 1);
			lastType    = m_plan[idx].type;
			wasAnchored = m_plan[idx].anchorEntityId != ENTITY_NONE;
		}
		// Decide is async → zero velocity to prevent drift past target.
		// Skip for anchored Moves: the next re-decide reinstalls velocity
		// within one tick, so braking only creates stutter.
		if (lastType == PlanStep::Move && outcome == StepOutcome::Success
		    && !wasAnchored) {
			if (Entity* e = server.getEntity(m_eid))
				sendStopMove(*e, server);
		}
		std::string goalText = m_goalText;
		clearPlan();
		setNeedsDecide(outcome, std::move(reason),
		               std::move(goalText), (int)lastType);
	}

	void interruptPlan(const std::string& reason) {
		if (m_plan.empty()) return;
		PlanStep::Type lastType =
			m_plan[std::min(m_stepIndex, (int)m_plan.size() - 1)].type;
		std::string goalText = m_goalText;
		clearPlan();
		setNeedsDecide(StepOutcome::Success,
		               "interrupt:" + reason,
		               std::move(goalText), (int)lastType);
	}

	void clearPlan() {
		m_plan.clear();
		m_stepIndex = 0;
		m_watch     = StepWatch{};
		m_viz.waypoints.clear();
		m_viz.hasAction = false;
	}

	void advanceStep() {
		m_stepIndex++;
		m_watch = StepWatch{};
	}

	// ── Step evaluators ──────────────────────────────────────────────────
	StepOutcome evaluateStep(PlanStep& step, Entity& e, float dt) {
		switch (step.type) {
		case PlanStep::Move:     return evaluateMove(step, e, dt);
		case PlanStep::Harvest:  return evaluateHarvest(step, e, dt);
		case PlanStep::Attack:   return evaluateAttack(step, e, dt);
		case PlanStep::Relocate: return evaluateRelocate(step, e, dt);
		case PlanStep::Interact: return evaluateInteract(step, e, dt);
		}
		return StepOutcome::Success;
	}

	StepOutcome evaluateMove(PlanStep& step, Entity& e, float dt) {
		glm::vec3 delta = step.targetPos - e.position;
		delta.y = 0;
		float dist = glm::length(delta);

		// Anchored Move: applyMove re-aims every tick. Only holdTime (the
		// re-decide cadence) completes the step — arrival is meaningless
		// for a moving target.
		if (step.anchorEntityId != ENTITY_NONE) {
			float hold = step.holdTime > 0.0f ? step.holdTime
			                                  : kDefaultIdleHoldSec;
			m_watch.progress += dt;
			if (m_watch.progress >= hold) return StepOutcome::Success;
			return StepOutcome::InProgress;
		}

		if (!m_watch.modeDetected) {
			m_watch.modeDetected = true;
			m_watch.isIdleHold   = (dist < kArriveEps);
		}

		float effectiveHold = step.holdTime > 0.0f
			? step.holdTime
			: (m_watch.isIdleHold ? kDefaultIdleHoldSec : 0.0f);

		if (m_watch.isIdleHold) {
			m_watch.progress += dt;
			if (m_watch.progress >= effectiveHold) return StepOutcome::Success;
			return StepOutcome::InProgress;
		}

		if (dist < kArriveEps) return StepOutcome::Success;
		if (effectiveHold > 0.0f) {
			m_watch.progress += dt;
			if (m_watch.progress >= effectiveHold) return StepOutcome::Success;
		}

		float horiz = std::sqrt(e.velocity.x * e.velocity.x +
		                        e.velocity.z * e.velocity.z);
		if (horiz < kStillEps) {
			m_watch.stillAccum += dt;
			if (m_watch.stillAccum > kStuckSeconds) {
				m_watch.failReason = "stuck";
				return StepOutcome::Failed;
			}
		} else {
			m_watch.stillAccum = 0;
		}
		return StepOutcome::InProgress;
	}

	StepOutcome evaluateHarvest(PlanStep& step, Entity& e, float dt) {
		glm::vec3 delta = step.targetPos - e.position;
		delta.y = 0;
		if (glm::length(delta) > kReachHarvest + 2.0f) {
			float horiz = std::sqrt(e.velocity.x * e.velocity.x +
			                        e.velocity.z * e.velocity.z);
			if (horiz < kStillEps) {
				m_watch.stillAccum += dt;
				if (m_watch.stillAccum > kStuckSeconds) {
					m_watch.failReason = "stuck";
					return StepOutcome::Failed;
				}
			} else m_watch.stillAccum = 0;
		}
		return StepOutcome::InProgress;
	}

	StepOutcome evaluateAttack(PlanStep& /*step*/, Entity& /*e*/, float /*dt*/) {
		return StepOutcome::InProgress;
	}

	StepOutcome evaluateRelocate(PlanStep& /*s*/, Entity& /*e*/, float /*dt*/) {
		if (!m_watch.modeDetected) {
			m_watch.modeDetected = true;
			return StepOutcome::InProgress;
		}
		return StepOutcome::Success;
	}

	StepOutcome evaluateInteract(PlanStep& /*s*/, Entity& /*e*/, float /*dt*/) {
		if (!m_watch.modeDetected) {
			m_watch.modeDetected = true;
			return StepOutcome::InProgress;
		}
		return StepOutcome::Success;
	}

	// ── Step appliers ────────────────────────────────────────────────────
	void applyStep(PlanStep& step, Entity& e, ServerInterface& server) {
		switch (step.type) {
		case PlanStep::Move:     applyMove(step, e, server); break;
		case PlanStep::Harvest:  applyHarvest(step, e, server); break;
		case PlanStep::Attack:   applyAttack(step, e, server); break;
		case PlanStep::Relocate: applyRelocate(step, server); break;
		case PlanStep::Interact: applyInteract(step, server); break;
		}
	}

	void applyMove(PlanStep& step, Entity& e, ServerInterface& server) {
		float speed = step.speed > 0 ? step.speed : e.def().walk_speed;
		if (step.anchorEntityId != ENTITY_NONE) {
			Entity* t = server.getEntity(step.anchorEntityId);
			if (!t || t->removed || t->hp() <= 0) {
				sendStopMove(e, server);
				return;
			}
			glm::vec3 to = t->position - e.position;
			to.y = 0;
			float hLen = glm::length(to);
			bool flee = step.keepAway > 0.0f;
			float ring = flee ? step.keepAway : step.keepWithin;
			bool stop = flee ? (hLen >= ring) : (hLen <= ring);
			if (stop || hLen < 0.01f) {
				sendStopMove(e, server);
				return;
			}
			float sign = flee ? -1.0f : 1.0f;
			glm::vec3 dir = {sign * to.x / hLen, 0, sign * to.z / hLen};
			sendMove(e, dir * speed, server);
			return;
		}
		glm::vec3 dir = step.targetPos - e.position;
		dir.y = 0;
		float dist = glm::length(dir);
		if (dist < kArriveEps) {
			sendStopMove(e, server);
			return;
		}
		dir /= dist;
		sendMove(e, dir * speed, server);
	}

	void applyHarvest(PlanStep& step, Entity& e, ServerInterface& server) {
		glm::vec3 delta = step.targetPos - e.position;
		delta.y = 0;
		float dist = glm::length(delta);
		if (dist > kReachHarvest) {
			glm::vec3 dir = glm::normalize(delta);
			sendMove(e, dir * e.def().walk_speed, server);
			return;
		}
		sendStopMove(e, server);
		ActionProposal p;
		p.type = ActionProposal::Convert;
		p.actorId = m_eid;
		p.fromItem  = step.itemId;
		p.fromCount = step.itemCount > 0 ? step.itemCount : 1;
		p.toItem    = step.itemId;
		p.toCount   = step.itemCount > 0 ? step.itemCount : 1;
		p.convertFrom = Container::block(glm::ivec3(step.targetPos));
		p.convertInto = Container::self();
		server.sendAction(p);
	}

	void applyAttack(PlanStep& step, Entity& e, ServerInterface& server) {
		Entity* target = server.getEntity(step.targetEntity);
		if (!target || target->removed) return;
		glm::vec3 delta = target->position - e.position;
		delta.y = 0;
		float dist = glm::length(delta);
		if (dist > kReachAttack) {
			glm::vec3 dir = glm::normalize(delta);
			sendMove(e, dir * e.def().walk_speed, server);
		} else {
			sendStopMove(e, server);
			// TODO: melee-damage Convert.
		}
	}

	void applyRelocate(PlanStep& step, ServerInterface& server) {
		ActionProposal p;
		p.type = ActionProposal::Relocate;
		p.actorId = m_eid;
		p.relocateFrom = step.relocateFrom;
		p.relocateTo = step.relocateTo;
		p.itemId = step.itemId;
		p.itemCount = step.itemCount;
		server.sendAction(p);
	}

	void applyInteract(PlanStep& step, ServerInterface& server) {
		ActionProposal p;
		p.type          = ActionProposal::Interact;
		p.actorId       = m_eid;
		p.blockPos      = glm::ivec3(step.targetPos);
		p.appearanceIdx = step.appearanceIdx;
		server.sendAction(p);
	}

	// ── Move emission + stuck telemetry ──────────────────────────────────
	void sendMove(Entity& e, glm::vec3 vel, ServerInterface& server) {
		e.velocity.x = vel.x;
		e.velocity.z = vel.z;

		float intent = std::sqrt(vel.x * vel.x + vel.z * vel.z);
		float moved  = glm::length(glm::vec2(e.position.x, e.position.z) -
		                           glm::vec2(m_stuckLastSampledPos.x,
		                                     m_stuckLastSampledPos.z));
		constexpr float kIntentThresh = 0.2f;
		constexpr float kMoveThresh   = 0.05f;
		constexpr float kStuckWindow  = 1.5f;
		const float dt = 1.0f / 60.0f;

		if (intent > kIntentThresh && moved < kMoveThresh) {
			m_stuckAccum += dt;
			if (m_stuckAccum >= kStuckWindow && !m_stuckLogged) {
				char detail[192];
				std::snprintf(detail, sizeof(detail),
					"pos=(%.2f,%.2f,%.2f) intent=(%.2f,%.2f) goal=\"%s\" "
					"held=%.1fs",
					e.position.x, e.position.y, e.position.z,
					vel.x, vel.z, m_goalText.c_str(), m_stuckAccum);
				logMoveStuck(m_eid, "Agent-Stuck",
					"agent held non-zero velocity but entity failed to "
					"displace (likely server collision clamp or "
					"client/server pos delta)",
					detail);
				m_stuckLogged = true;
			}
		} else {
			if (m_stuckLogged) {
				char detail[96];
				std::snprintf(detail, sizeof(detail),
					"pos=(%.2f,%.2f,%.2f)",
					e.position.x, e.position.y, e.position.z);
				logMoveStuck(m_eid, "Agent-Unstuck",
					"entity resumed displacement after prior Agent-Stuck",
					detail);
			}
			m_stuckAccum  = 0.0f;
			m_stuckLogged = false;
		}
		m_stuckLastSampledPos = e.position;

		ActionProposal p;
		p.type       = ActionProposal::Move;
		p.actorId    = m_eid;
		p.desiredVel = vel;
		p.goalText   = m_goalText;
		server.sendAction(p);
	}

	void sendStopMove(Entity& e, ServerInterface& server) {
		sendMove(e, {0, 0, 0}, server);
	}

	void rebuildViz() {
		m_viz.waypoints.clear();
		m_viz.hasAction = false;
		if (m_plan.empty()) return;
		for (auto& step : m_plan) {
			if (step.type == PlanStep::Move) {
				m_viz.waypoints.push_back(step.targetPos);
			} else {
				m_viz.actionPos  = step.targetPos;
				m_viz.actionType = step.type;
				m_viz.hasAction  = true;
				m_viz.waypoints.push_back(step.targetPos);
			}
		}
	}

	// ── State ────────────────────────────────────────────────────────────
	EntityId       m_eid;
	std::string    m_behaviorId;
	BehaviorHandle m_handle = -1;

	Plan        m_plan;
	int         m_stepIndex = 0;
	std::string m_goalText;
	PlanViz     m_viz;
	StepWatch   m_watch;

	int m_prevHp = 0;

	// Schedule flags.
	bool        m_needsDecide    = false;
	bool        m_reactInFlight  = false;
	bool        m_decideInFlight = false;
	LastOutcome m_lastOutcome;

	// Player-override gate.
	float m_overridePauseTimer = 0.0f;

	DecideRateTracker m_rate;

	// React/signal state. Dirty bit + latest-wins payload, consumed at most
	// once per kReactCooldownSec.
	bool        m_dirty         = false;
	std::string m_dirtyKind;
	std::vector<std::pair<std::string, std::string>> m_dirtyPayload;
	float       m_reactCooldown = 0.0f;

	// "Wants to move, not moving" → [MoveStuck:Agent-Stuck].
	glm::vec3 m_stuckLastSampledPos = glm::vec3(0.0f);
	float     m_stuckAccum          = 0.0f;
	bool      m_stuckLogged         = false;
};

} // namespace civcraft
