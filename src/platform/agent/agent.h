#pragma once

// Per-entity AI controller. Owns one NPC's Plan state and is the sole
// producer of decide-requests for its own eid.
//
// Single-producer / single-consumer invariants this class enforces:
//
//   * Plan state (m_plan, m_stepIndex, m_watch, m_viz, m_goalText) is mutated
//     ONLY through Agent's public API. Nothing outside this class reaches in.
//
//   * Decide-requests for this eid funnel through the private requestRedecide()
//     chokepoint. Whether the trigger is plan completion, an interrupt, an
//     HP drop, a world event, or a control-mode resume — all paths land here.
//
//   * AgentClient is the sole orchestrator: it drains the DecisionQueue, calls
//     tickPlan() in execute phase, and routes external callbacks to the right
//     Agent. It never touches plan state directly.
//
// See docs in agent_client.h for the orchestrator side.

#include "logic/entity.h"
#include "logic/action.h"
#include "net/server_interface.h"
#include "server/behavior.h"
#include "server/python_bridge.h"  // BehaviorHandle
#include "agent/decision_queue.h"
#include "debug/move_stuck_log.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
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
	// Step-evaluation tunables. Public so the orchestrator can mirror them
	// in diagnostics, but Agent is the only mutator.
	static constexpr float kArriveEps          = 1.5f;
	static constexpr float kStillEps           = 0.1f;
	static constexpr float kStuckSeconds       = 2.0f;
	static constexpr float kReachHarvest       = 3.0f;
	static constexpr float kReachAttack        = 2.5f;
	// Fallback idle-hold when a behavior returns Move(self) with holdTime==0.
	// Real behaviors should set duration explicitly (Rest/Wander already do).
	static constexpr float kDefaultIdleHoldSec = 30.0f;
	// Player override: ~π seconds of "obey" before decide() may run again.
	static constexpr float kOverridePauseSec   = 3.14f;
	// Pause-while-controlled marker. Just needs to outlive any session.
	static constexpr float kForeverPauseSec    = 1.0e9f;
	// Per-agent rate limit for react(signal). Prevents a chatty signal
	// source from pinning one Agent's Python budget.
	static constexpr float kReactCooldownSec   = 0.5f;

	Agent(EntityId eid, std::string behaviorId, BehaviorHandle handle)
		: m_eid(eid),
		  m_behaviorId(std::move(behaviorId)),
		  m_handle(handle) {}

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

	// Per-entity decide rate metric, updated when a decide is dispatched.
	void  noteDecideFired(float now) { m_rate.onDecide(now); }
	float decideRatePerMin() const   { return m_rate.emaPerMin; }

	// ── Plan-state writers — every mutation funnels through here ──────────

	// Called by AgentClient::drainWorkerResults() after stale-generation
	// filtering. Sole entry point for installing a new Plan.
	void onDecideResult(Plan plan, std::string goalText, Entity& e) {
		m_plan      = std::move(plan);
		m_stepIndex = 0;
		m_goalText  = std::move(goalText);
		m_watch     = StepWatch{};
		e.goalText  = m_goalText;
		e.hasError  = false;
		e.errorText.clear();
		rebuildViz();
	}

	// Decode error from Python: surface on entity + queue priority retry.
	void onDecideError(const std::string& err, Entity& e,
	                   DecisionQueue& q, float now) {
		e.goalText   = "ERROR: " + err.substr(0, 60);
		e.hasError   = true;
		e.errorText  = err;
		LastOutcome out;
		out.outcome  = StepOutcome::Failed;
		out.reason   = "decide_error";
		// decide_error is a priority retry (broken behaviors should not
		// starve waiting behind a long normal-lane queue).
		requestRedecide(std::move(out), /*priority=*/true, q, now);
	}

	// First-time discovery → normal lane (no urgency).
	void requestInitialDecide(DecisionQueue& q, float now) {
		LastOutcome out;
		out.outcome = StepOutcome::Success;
		out.reason  = "discovery";
		requestRedecide(std::move(out), /*priority=*/false, q, now);
	}

	// Player-override: replace plan with a single Move and arm an obey-pause.
	// No decide-request — the pause is the visible "obey beat".
	void applyOverride(glm::vec3 goal, Entity& e) {
		clearPlan();
		m_plan.push_back(PlanStep::move(goal));
		m_goalText = "player_override";
		m_overridePauseTimer = kOverridePauseSec;
		m_hasPendingOutcome = false;
		rebuildViz();
		e.goalText = m_goalText;
	}

	// Control-mode entry: park the pause indefinitely, no flush.
	void pause(Entity& e) {
		clearPlan();
		m_goalText = "controlled";
		m_overridePauseTimer = kForeverPauseSec;
		m_hasPendingOutcome = false;
		e.goalText = m_goalText;
	}

	// Control-mode exit: drop the pause, enqueue an immediate decide().
	void resume(DecisionQueue& q, float now) {
		m_overridePauseTimer = 0.0f;
		m_hasPendingOutcome  = false;
		LastOutcome out;
		out.outcome = StepOutcome::Success;
		out.reason  = "interrupt:resume";
		requestRedecide(std::move(out), /*priority=*/true, q, now);
	}

	// External interrupt callback (network-layer S_INTERRUPT). Priority lane.
	void onInterrupt(const std::string& reason,
	                 DecisionQueue& q, float now) {
		interruptPlan(reason, q, now);
	}

	// ── Signal handling (react path) ──────────────────────────────────────
	//
	// AgentClient detects world events (threat_nearby, …) and pushes them
	// here via onSignal(). We don't run Python inline — we just set a dirty
	// bit. maybeReact() is called once per tick and enqueues a React request
	// on the priority lane if the rate limit + guards allow.
	void onSignal(std::string kind,
	              std::vector<std::pair<std::string, std::string>> payload) {
		// Latest-wins: a new signal overrides an older pending one (we'll
		// only ever react to the freshest event).
		m_dirtyKind    = std::move(kind);
		m_dirtyPayload = std::move(payload);
		m_dirty        = true;
	}

	// Returns true when a React request was enqueued.
	bool maybeReact(DecisionQueue& q, float now, float dt) {
		if (m_reactCooldown > 0.0f) m_reactCooldown -= dt;

		if (!m_dirty) return false;
		if (m_overridePauseTimer > 0.0f) return false;  // player controls us
		if (m_reactCooldown > 0.0f)      return false;  // rate-limit

		q.requestReact(m_eid, std::move(m_dirtyKind),
		               std::move(m_dirtyPayload), now);
		m_dirty = false;
		m_dirtyKind.clear();
		m_dirtyPayload.clear();
		m_reactCooldown = kReactCooldownSec;
		return true;
	}

	// HP drop + target-gone detection. Called every tick by the orchestrator.
	void scanForInterrupts(Entity& e, DecisionQueue& q, float now) {
		int curHp = e.hp();
		bool hpDropped = (m_prevHp > 0 && curHp < m_prevHp);
		m_prevHp = curHp;

		if (m_plan.empty()) return;

		if (hpDropped) {
			interruptPlan("hp", q, now);
			return;
		}
		if (m_stepIndex < (int)m_plan.size()) {
			PlanStep& step = m_plan[m_stepIndex];
			if (step.type == PlanStep::Attack) {
				// Note: target lookup happens in orchestrator (it has server).
				// We delegate via the return — orchestrator decides whether
				// to call interruptPlan("target_gone").
			}
		}
	}

	// ── Per-tick driver ───────────────────────────────────────────────────
	//
	// Called by AgentClient::phaseExecute. Walks the override pause timer,
	// then evaluates+applies the current step. Self-contained: takes a
	// ServerInterface for entity lookup + ActionProposal submission, and
	// the queue+now to enqueue decide-requests on plan completion.
	void tickPlan(float dt, ServerInterface& server,
	              DecisionQueue& q, float now) {
		// Drain override pause → flush deferred outcome.
		if (m_overridePauseTimer > 0.0f) {
			m_overridePauseTimer -= dt;
			if (m_overridePauseTimer <= 0.0f) {
				m_overridePauseTimer = 0.0f;
				if (m_hasPendingOutcome) {
					m_hasPendingOutcome = false;
					// Pause expired — pendingOutcome was stashed because
					// some path tried to enqueue while we were paused.
					if (!q.hasPending(m_eid)) {
						// Always priority — these are deferred interrupts.
						q.requestPriority(m_eid,
						                  std::move(m_pendingOutcome), now);
					}
				}
			}
		}

		if (m_plan.empty()) return;

		Entity* e = server.getEntity(m_eid);
		if (!e || e->removed) return;

		if (m_stepIndex >= (int)m_plan.size()) {
			finishPlan(StepOutcome::Success, std::string{}, server, q, now);
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

		// Attack target evaporated mid-step → priority interrupt.
		if (step.type == PlanStep::Attack) {
			Entity* t = server.getEntity(step.targetEntity);
			if (!t || t->removed) {
				interruptPlan("target_gone", q, now);
				return;
			}
		}

		// Anchored Move target gone → priority re-decide. "Gone" = erased
		// (S_REMOVE already fired), flagged removed (transition window), or
		// dead (hp==0). Dead-but-not-yet-erased is a ~0.5s window; checking
		// hp here stops followers/fleers from tailing a corpse in that gap.
		if (step.type == PlanStep::Move && step.anchorEntityId != ENTITY_NONE) {
			Entity* t = server.getEntity(step.anchorEntityId);
			if (!t || t->removed || t->hp() <= 0) {
				interruptPlan("anchor_gone", q, now);
				return;
			}
		}

		// Harvest block gone mid-step → step succeeds immediately. Without
		// this the agent keeps firing Convert proposals against air and the
		// server rejects them as SourceBlockGone every tick.
		if (step.type == PlanStep::Harvest) {
			glm::ivec3 bp = glm::ivec3(glm::floor(step.targetPos));
			BlockId bid = server.chunks().getBlock(bp.x, bp.y, bp.z);
			if (bid == BLOCK_AIR) {
				advanceStep();
				if (m_stepIndex >= (int)m_plan.size())
					finishPlan(StepOutcome::Success, std::string{},
					           server, q, now);
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
				finishPlan(StepOutcome::Success, std::string{},
				           server, q, now);
			break;
		case StepOutcome::Failed:
			finishPlan(StepOutcome::Failed, m_watch.failReason,
			           server, q, now);
			break;
		}
	}

private:
	// Per-step observation state. Reset on every stepIndex advance.
	struct StepWatch {
		bool        initialized   = false;
		bool        modeDetected  = false;
		bool        isIdleHold    = false;  // Move target == current pos
		float       stillAccum    = 0.0f;
		float       progress      = 0.0f;
		int         prevTargetHP  = 0;
		std::string failReason;
	};

	// EMA-smoothed decides-per-minute. Aggressive smoothing (~10-decide
	// average) so the F3 overlay shows steady-state, not spike values.
	struct DecideRateTracker {
		float lastDecideTime = -1.0f;  // -1 = never decided
		float emaPerMin      = 0.0f;

		void onDecide(float now) {
			if (lastDecideTime < 0.0f) {
				lastDecideTime = now;
				return;
			}
			float gap = std::max(now - lastDecideTime, 0.001f);
			float instant = 60.0f / gap;
			constexpr float kAlpha = 0.2f;
			emaPerMin = emaPerMin <= 0.0f
				? instant
				: emaPerMin * (1.0f - kAlpha) + instant * kAlpha;
			lastDecideTime = now;
		}
	};

	// ── The single chokepoint for decide-requests ────────────────────────
	//
	// EVERY code path that wants this eid to re-decide ends up here. If a
	// player-override pause is active, the request is stashed; otherwise
	// the DecisionQueue is called directly — it has latest-wins semantics
	// per lane plus upgrade-from-normal behavior for priority requests, so
	// de-duping here would drop legitimate priority upgrades (e.g. an HP
	// drop arriving while a normal plan-completion request was pending
	// used to get silently lost).
	void requestRedecide(LastOutcome out, bool priority,
	                     DecisionQueue& q, float now) {
		if (m_overridePauseTimer > 0.0f) {
			m_pendingOutcome    = std::move(out);
			m_hasPendingOutcome = true;
			return;
		}
		if (priority) q.requestPriority(m_eid, std::move(out), now);
		else          q.requestNormal  (m_eid, std::move(out), now);
	}

	// Plan completion → normal lane (it's expected, not urgent).
	void finishPlan(StepOutcome outcome, std::string reason,
	                ServerInterface& server,
	                DecisionQueue& q, float now) {
		PlanStep::Type lastType = PlanStep::Move;
		if (!m_plan.empty()) {
			int idx = std::min(m_stepIndex, (int)m_plan.size() - 1);
			lastType = m_plan[idx].type;
		}
		// Decide is async → zero velocity to prevent drift past target.
		// Skip for anchored Moves: the next re-decide reinstalls velocity
		// within one tick, so braking only creates stutter.
		bool wasAnchored = false;
		if (!m_plan.empty()) {
			int idx = std::min(m_stepIndex, (int)m_plan.size() - 1);
			wasAnchored = m_plan[idx].anchorEntityId != ENTITY_NONE;
		}
		if (lastType == PlanStep::Move && outcome == StepOutcome::Success
		    && !wasAnchored) {
			if (Entity* e = server.getEntity(m_eid))
				sendStopMove(*e, server);
		}
		LastOutcome next;
		next.outcome     = outcome;
		next.goalText    = m_goalText;
		next.stepTypeRaw = (int)lastType;
		next.reason      = std::move(reason);

		clearPlan();
		// Plan-completion failure → priority retry; success → normal.
		bool priority = (outcome == StepOutcome::Failed);
		requestRedecide(std::move(next), priority, q, now);
	}

	// All interrupts go to the priority lane with a tagged reason.
	void interruptPlan(const std::string& reason,
	                   DecisionQueue& q, float now) {
		if (m_plan.empty()) return;
		PlanStep::Type lastType = m_plan[
			std::min(m_stepIndex, (int)m_plan.size() - 1)].type;
		LastOutcome next;
		next.outcome     = StepOutcome::Success;
		next.goalText    = m_goalText;
		next.stepTypeRaw = (int)lastType;
		next.reason      = "interrupt:" + reason;

		clearPlan();
		requestRedecide(std::move(next), /*priority=*/true, q, now);
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
	//
	// All step durations are observable: arrival, hp drop, block disappears.
	// The ONE timer is the per-step holdTime carried on the PlanStep itself
	// (semantics described in server/behavior.h).
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

		// Latch idle-hold mode on first eval (target == current pos).
		if (!m_watch.modeDetected) {
			m_watch.modeDetected = true;
			m_watch.isIdleHold   = (dist < kArriveEps);
		}

		// Effective hold = behavior request, or default for idle-hold steps.
		float effectiveHold = step.holdTime > 0.0f
			? step.holdTime
			: (m_watch.isIdleHold ? kDefaultIdleHoldSec : 0.0f);

		if (m_watch.isIdleHold) {
			m_watch.progress += dt;
			if (m_watch.progress >= effectiveHold) return StepOutcome::Success;
			return StepOutcome::InProgress;
		}

		// Travel mode: arrival OR holdTime budget exceeded → Success.
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
		// Block-gone is checked in tickPlan() before we get here, parallel to
		// the Attack target-gone check. Here we only watch for stuck travel.
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

	StepOutcome evaluateAttack(PlanStep& step, Entity& /*e*/, float /*dt*/) {
		// Target-gone is detected in tickPlan() before we get here.
		// Success on target HP <= 0 is enforced via this same scan.
		// We can't see the target without a server ref; rely on tickPlan().
		(void)step;
		return StepOutcome::InProgress;
	}

	StepOutcome evaluateRelocate(PlanStep& /*s*/, Entity& /*e*/, float /*dt*/) {
		// First eval applies once; second eval succeeds (server has executed).
		if (!m_watch.modeDetected) {
			m_watch.modeDetected = true;
			return StepOutcome::InProgress;
		}
		return StepOutcome::Success;
	}

	StepOutcome evaluateInteract(PlanStep& /*s*/, Entity& /*e*/, float /*dt*/) {
		// One-shot: first eval fires apply, second eval completes.
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
		// Anchored Move = per-tick Execute(). Reads live target position,
		// seeks (keepWithin) or scatters (keepAway). Server sees plain Moves.
		// tickPlan's anchor-gone guard fires first, but keep a defensive
		// check here in case evaluate→apply interleaves with a removal.
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
		// Client-side prediction; yaw derives from velocity elsewhere.
		e.velocity.x = vel.x;
		e.velocity.z = vel.z;

		// Stuck probe: intent>0 but no displacement ≥1.5s → log once.
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

	// Override pause: Python decide() blocked until expires.
	float       m_overridePauseTimer = 0.0f;
	bool        m_hasPendingOutcome  = false;
	LastOutcome m_pendingOutcome;

	DecideRateTracker m_rate;

	// React/signal state. Dirty bit + latest-wins payload, consumed at most
	// once per kReactCooldownSec by maybeReact().
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
