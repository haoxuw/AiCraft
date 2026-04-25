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
#include "logic/constants.h"       // BlockType strings for WorldView classification
#include "net/server_interface.h"
#include "agent/actions.h"         // ActionHandler hierarchy + actionFor()
#include "agent/behavior.h"
#include "agent/jitter.h"          // per-agent frustration + RNG
#include "python/python_bridge.h"  // BehaviorHandle
#include "agent/outcome.h"
#include "agent/pathfind.h"        // WorldView, DoorOracle, GridPlanner
#include "client/path_executor.h"  // Navigator (facade over unified PathExecutor)
#include "agent/pathlog.h"         // PATHLOG(...) — gated by CIVCRAFT_PATHFINDING_DEBUG
#include "debug/move_stuck_log.h"
#include "debug/entity_log.h"      // still pulled in for logMoveStuck paths

#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
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
	// Consecutive Failed_* plan outcomes before we promote the next failure
	// to Failed_GaveUp. Python's decide() branches on that state to route
	// into the town-center complaint / idle pipeline instead of producing
	// another plan that will fail. Reset on any Success outcome.
	static constexpr int   kFailStreakGiveUp   = 5;

	Agent(EntityId eid, std::string behaviorId, BehaviorHandle handle)
		: m_eid(eid),
		  m_behaviorId(std::move(behaviorId)),
		  m_handle(handle),
		  m_jitter(eid) {
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
	ExecState      execState() const { return m_execState; }
	int            failStreak() const { return m_failStreak; }

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
	bool firstDecideDone() const { return m_firstDecideDone; }

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
		m_firstDecideDone = true;
		m_execLoggedFirstWaypoint = false;
		// Plan installed — executor hasn't ticked yet, so leave execState at
		// PlanRequested. The first evaluateStep / applyStep will flip it to
		// the concrete mode (Walking / Harvesting / DirectApproach / …).
		m_execState = ExecState::PlanRequested;
		e.goalText     = m_goalText;
		e.hasError     = false;
		e.errorText.clear();
		// Lifecycle hook #3 — plan received (Python decide → Plan reaches this
		// process). Pairs with the per-step dump below so one scroll shows the
		// full shape of the new plan.
		PATHLOG(m_eid,
			"pathfind: planReceived steps=%zu goal=\"%s\" prevState=%s",
			m_plan.size(), m_goalText.c_str(), toString(m_execState));
		PATHLOG(m_eid,
			"trigger: onDecideResult steps=%zu goal=\"%s\"",
			m_plan.size(), m_goalText.c_str());
		for (size_t i = 0; i < m_plan.size(); ++i) {
			const PlanStep& s = m_plan[i];
			const char* t =
				(s.type == PlanStep::Move)     ? "Move"     :
				(s.type == PlanStep::Harvest)  ? "Harvest"  :
				(s.type == PlanStep::Attack)   ? "Attack"   :
				(s.type == PlanStep::Relocate) ? "Relocate" :
				(s.type == PlanStep::Interact) ? "Interact" : "?";
			PATHLOG(m_eid,
				"trigger:   step[%zu] type=%s target=(%.1f,%.1f,%.1f) "
				"candidates=%zu useNav=%d item=\"%s\"",
				i, t, s.targetPos.x, s.targetPos.y, s.targetPos.z,
				s.candidates.size(), s.useNavigator ? 1 : 0,
				s.itemId.c_str());
		}
		rebuildViz();
	}

	// Python raised. Surface on entity and mark for retry.
	void onDecideError(const std::string& err, Entity& e) {
		e.goalText    = "ERROR: " + err.substr(0, 60);
		e.hasError    = true;
		e.errorText   = err;
		setNeedsDecide(StepOutcome::Failed, "decide_error",
		               /*goalTextForOutcome*/ {}, /*stepTypeRaw*/ 0,
		               ExecState::Failed_DecideError);
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

	// Multi-step manual override from the RTS wheel. Installs a full Plan in
	// place of decide()'s output; tickPlan executes it like any decide()-
	// produced plan. When the plan finishes naturally (finishPlan → needsDecide),
	// control returns to Python AI. The pause timer only gates decide()
	// dispatch, not the executor — long plans still run to completion.
	void applyPlanOverride(Plan plan, std::string goalText, Entity& e) {
		clearPlan();
		m_plan                = std::move(plan);
		m_goalText            = std::move(goalText);
		m_overridePauseTimer  = kOverridePauseSec;
		m_needsDecide         = false;
		rebuildViz();
		e.goalText = m_goalText;
	}

	// Shift-queue variant: append new steps to the end of the current plan
	// without interrupting the step in flight. Refreshes the obey-pause so
	// decide() stays suppressed across the extended plan. If no plan is
	// running (or the current one is already finished), falls through to
	// applyPlanOverride so the new steps become the active plan.
	void appendPlanOverride(Plan plan, std::string goalText, Entity& e) {
		if (m_plan.empty() || m_stepIndex >= (int)m_plan.size()) {
			applyPlanOverride(std::move(plan), std::move(goalText), e);
			return;
		}
		for (auto& s : plan) m_plan.push_back(std::move(s));
		if (!goalText.empty()) {
			if (!m_goalText.empty()) m_goalText += " → ";
			m_goalText += goalText;
		}
		m_overridePauseTimer = kOverridePauseSec;
		m_needsDecide        = false;
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

	// External interrupt (network-layer S_INTERRUPT). The `reason` string is
	// informational — it lands in the trigger log line — but the plan ends
	// with Failed_Interrupted regardless of what sent the signal.
	void onInterrupt(const std::string& reason) {
		PATHLOG(m_eid, "trigger: onInterrupt reason=\"%s\"", reason.c_str());
		interruptPlan(ExecState::Failed_Interrupted);
	}

	// World event broadcast (AgentClient::onWorldEvent).
	void onWorldEvent(const std::string& kind) {
		PATHLOG(m_eid, "trigger: onWorldEvent kind=\"%s\"", kind.c_str());
		interruptPlan(ExecState::Failed_Interrupted);
	}

	// ── Signal handling ───────────────────────────────────────────────────
	void onSignal(std::string kind,
	              std::vector<std::pair<std::string, std::string>> payload) {
		PATHLOG(m_eid, "trigger: onSignal kind=\"%s\"", kind.c_str());
		// Latest-wins: fresher signals overwrite older ones.
		m_dirtyKind    = std::move(kind);
		m_dirtyPayload = std::move(payload);
		m_dirty        = true;
	}

	// Per-tick timer maintenance. Call once per tick, before dispatch decisions.
	void tickTimers(float dt) {
		if (m_reactCooldown > 0.0f)       m_reactCooldown      -= dt;
		if (m_chopCooldown  > 0.0f)       m_chopCooldown       -= dt;
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
		if (hpDropped) {
			PATHLOG(m_eid, "trigger: hpDrop prev=%d cur=%d", m_prevHp, curHp);
		}
		m_prevHp = curHp;
		if (m_plan.empty()) return;
		if (hpDropped) interruptPlan(ExecState::Failed_Interrupted);
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
				interruptPlan(ExecState::Failed_TargetGone);
				return;
			}
		}

		// Anchored Move target gone → re-decide. "Gone" = erased, flagged
		// removed, or dead; the dead-but-not-erased window is ~0.5s.
		if (step.type == PlanStep::Move && step.anchorEntityId != ENTITY_NONE) {
			Entity* t = server.getEntity(step.anchorEntityId);
			if (!t || t->removed || t->hp() <= 0) {
				interruptPlan(ExecState::Failed_AnchorGone);
				return;
			}
		}

		// Harvest housekeeping: bail out if inventory is full before the next
		// swing (the capacity gate is the step's itemId, a conservative hint
		// from Python — typically the heaviest item the step might produce).
		// The "nothing in range" exit is handled inside applyHarvest, since
		// it needs to run a local block scan anyway.
		if (step.type == PlanStep::Harvest) {
			bool invFull = false;
			if (e->inventory && !step.itemId.empty()) {
				invFull = !e->inventory->canAccept(
					step.itemId, 1, e->def().inventory_capacity);
			}
			if (invFull) {
				advanceStep();
				if (m_stepIndex >= (int)m_plan.size())
					finishPlan(StepOutcome::Success, std::string{}, server);
				return;
			}
		}

		StepOutcome outcome = evaluateStep(step, *e, dt);
		switch (outcome) {
		case StepOutcome::InProgress:
			actionFor(step.type).apply(step, *e, *this, server);
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
	// Action handlers live in agent/actions.cpp and need deep access to
	// m_watch / m_jitter / m_navigator / m_chopCooldown / advanceStep()
	// to do their work. They are part of the same component as Agent, not
	// arms-length consumers — direct friend access beats a side-channel
	// proxy struct.
	friend class MoveAction;
	friend class HarvestAction;
	friend class AttackAction;
	friend class RelocateAction;
	friend class InteractAction;

	struct StepWatch {
		bool        initialized   = false;
		bool        modeDetected  = false;
		bool        isIdleHold    = false;
		float       stillAccum    = 0.0f;
		float       progress      = 0.0f;
		int         prevTargetHP  = 0;
		std::string failReason;
		// Paired with failReason — the specific ExecState variant to transition
		// into when this step fails. Set at the same site as failReason so
		// finishPlan doesn't have to string-match to recover the category.
		// Defaults to Failed_Unknown as a safe catch-all.
		ExecState   failExecState = ExecState::Failed_Unknown;

		// Navigator is the sole arrival/failure authority for useNavigator=true
		// Move steps — Agent no longer runs its own "dist < kArriveEps" check
		// for them. applyMove sets navArrived on NavTickResult::Arrived and
		// navFailed on NavTickResult::Failed; evaluateMove resolves the step
		// accordingly on the next call. Direct-steer Move (useNavigator=false)
		// still owns its own arrival check since no Executor is involved.
		bool        navArrived    = false;
		bool        navFailed     = false;

		// Harvest-only: per-step candidate cycling. `activeIdx` is the current
		// slot in step.candidates; `anchorFailed` marks slots that wedged or
		// exhausted their local sphere. Reset by advanceStep()'s m_watch = {}.
		int               harvestActiveIdx     = 0;
		std::vector<bool> harvestAnchorFailed;
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
	//
	// `stateHint` is the *provisional* terminal ExecState — the call site's
	// best guess at why the prior plan ended (Failed_NavNoPath, Failed_TargetGone,
	// Failed_Interrupted, …). Callers pass PlanRequested (the default) when
	// the agent is simply asking for its next plan without a failure, e.g.
	// on resume or first-time discovery.
	//
	// Responsibilities, in order:
	//   1. Repetitive-decide (churn) diagnostic — fires when setNeedsDecide
	//      is re-entered within 1s of the prior call.
	//   2. Fail-streak maintenance — any isFailed(stateHint) increments
	//      m_failStreak; a non-failure stateHint resets it. When the streak
	//      crosses kFailStreakGiveUp, the state is promoted to Failed_GaveUp
	//      so Python's decide() routes into the town-center complaint path
	//      (see task #62) instead of retrying a doomed plan.
	//   3. Publish to LastOutcome + set m_needsDecide.
	void setNeedsDecide(StepOutcome outcome, std::string reason,
	                    std::string goalTextForOutcome = {},
	                    int stepTypeRaw = 0,
	                    ExecState stateHint = ExecState::PlanRequested) {
		const char* outName =
			(outcome == StepOutcome::Success)    ? "Success" :
			(outcome == StepOutcome::Failed)     ? "Failed"  :
			(outcome == StepOutcome::InProgress) ? "InProgress" : "?";
		// (1) Repetitive-decide detector — uses steady_clock so we don't need
		// dt plumbed into Agent.
		auto now = std::chrono::steady_clock::now();
		float gapSec = m_lastSetNeedsDecideAt.time_since_epoch().count() == 0
			? 1e9f
			: std::chrono::duration<float>(now - m_lastSetNeedsDecideAt).count();
		if (gapSec < 1.0f) {
			PATHLOG(m_eid,
				"pathfind: decideChurn gap=%.3fs prevState=%s prevOutcome=%s "
				"prevReason=\"%s\" prevGoal=\"%s\" prevStreak=%d -> "
				"newOutcome=%s newReason=\"%s\" newStateHint=%s",
				gapSec,
				toString(m_execState),
				(m_lastOutcome.outcome == StepOutcome::Success) ? "Success" :
				(m_lastOutcome.outcome == StepOutcome::Failed)  ? "Failed"  :
				(m_lastOutcome.outcome == StepOutcome::InProgress) ? "InProgress" : "?",
				m_lastOutcome.reason.c_str(),
				m_goalText.c_str(),
				m_failStreak,
				outName, reason.c_str(),
				toString(stateHint));
		}
		m_lastSetNeedsDecideAt = now;

		// (2) Fail-streak bookkeeping. isFailed() covers every Failed_* variant.
		// Non-failure states (PlanRequested, Arrived, etc.) reset the streak —
		// the agent has made real progress.
		ExecState nextState = stateHint;
		if (isFailed(nextState)) {
			m_failStreak++;
			if (m_failStreak >= kFailStreakGiveUp
			    && nextState != ExecState::Failed_GaveUp) {
				PATHLOG(m_eid,
					"pathfind: giveUp streak=%d promoted \"%s\" -> Failed_GaveUp",
					m_failStreak, toString(nextState));
				nextState = ExecState::Failed_GaveUp;
			}
		} else {
			if (m_failStreak != 0) {
				PATHLOG(m_eid,
					"pathfind: failStreak reset (was %d, new state %s)",
					m_failStreak, toString(nextState));
			}
			m_failStreak = 0;
		}

		PATHLOG(m_eid,
			"trigger: setNeedsDecide outcome=%s reason=\"%s\" stepType=%d "
			"state=%s streak=%d",
			outName, reason.c_str(), stepTypeRaw,
			toString(nextState), m_failStreak);

		// (3) Publish.
		m_execState = nextState;
		m_lastOutcome.outcome     = outcome;
		m_lastOutcome.reason      = std::move(reason);
		m_lastOutcome.goalText    = std::move(goalTextForOutcome);
		m_lastOutcome.stepTypeRaw = stepTypeRaw;
		m_lastOutcome.execState   = m_execState;
		m_lastOutcome.failStreak  = m_failStreak;
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
		// Any plan that ended in Success tells the executor "this line of
		// attempts is paying off" — reset frustration so the next plan
		// starts deterministic again. Failed plans keep whatever
		// temperature the handlers already bumped in.
		if (outcome == StepOutcome::Success && m_jitter.cool()) {
			PATHLOG(m_eid, "trigger: frustration cool (plan success)");
		}
		// Failed plans carry their specific reason state via m_watch, set
		// right next to the failReason string at the evaluator/applier site.
		// Success plans advance the agent to PlanRequested (awaiting the next
		// decide) and reset the fail-streak in setNeedsDecide.
		ExecState nextState = (outcome == StepOutcome::Failed)
			? m_watch.failExecState
			: ExecState::PlanRequested;
		const char* outName =
			(outcome == StepOutcome::Success)    ? "Success" :
			(outcome == StepOutcome::Failed)     ? "Failed"  :
			(outcome == StepOutcome::InProgress) ? "InProgress" : "?";
		PATHLOG(m_eid,
			"trigger: finishPlan outcome=%s reason=\"%s\" state=%s "
			"stepIdx=%d/%zu",
			outName, reason.c_str(), toString(nextState),
			m_stepIndex, m_plan.size());
		// Decide is async → zero velocity to prevent drift past target.
		// Skip for anchored Moves: the next re-decide reinstalls velocity
		// within one tick, so braking only creates stutter.
		if (lastType == PlanStep::Move && outcome == StepOutcome::Success
		    && !wasAnchored) {
			if (Entity* e = server.getEntity(m_eid))
				sendStopMove(*e, server, "plan-end-brake");
		}
		std::string goalText = m_goalText;
		clearPlan();
		setNeedsDecide(outcome, std::move(reason),
		               std::move(goalText), (int)lastType, nextState);
	}

	// The enum IS the reason — we derive the "interrupt:<name>" string from
	// toString(state) so Python's last_reason stays populated without the
	// caller having to keep two parallel labels in sync. StepOutcome stays
	// Success to match the legacy "interrupt:*" contract (Python branches on
	// the reason prefix, not outcome=failed), but the streak/give-up logic
	// DOES treat isFailed(state) as a real failure — "target disappeared
	// five times in a row" is exactly what give-up exists to catch.
	void interruptPlan(ExecState state) {
		if (m_plan.empty()) return;
		PlanStep::Type lastType =
			m_plan[std::min(m_stepIndex, (int)m_plan.size() - 1)].type;
		std::string goalText = m_goalText;
		PATHLOG(m_eid,
			"trigger: interruptPlan state=%s stepIdx=%d/%zu goal=\"%s\"",
			toString(state), m_stepIndex, m_plan.size(), goalText.c_str());
		clearPlan();
		setNeedsDecide(StepOutcome::Success,
		               std::string("interrupt:") + toString(state),
		               std::move(goalText), (int)lastType, state);
	}

	void clearPlan() {
		m_plan.clear();
		m_stepIndex = 0;
		m_watch     = StepWatch{};
		m_viz.waypoints.clear();
		m_viz.hasAction = false;
		clearNavigator();
	}

	void advanceStep() {
		PATHLOG(m_eid, "trigger: advanceStep %d -> %d (planSize=%zu)",
			m_stepIndex, m_stepIndex + 1, m_plan.size());
		m_stepIndex++;
		m_watch = StepWatch{};
		clearNavigator();
	}

	// ── Step dispatch ────────────────────────────────────────────────────
	// Per-type ActionHandlers live in agent/actions.cpp. evaluateStep reads
	// state (decides if the step is Success/Failed/InProgress); applyStep
	// takes action (sends ActionProposals, drives the Navigator). Handlers
	// are friends of Agent so they can reach m_watch / m_navigator /
	// m_jitter / m_chopCooldown without a side-channel proxy.
	StepOutcome evaluateStep(PlanStep& step, Entity& e, float dt) {
		return actionFor(step.type).evaluate(step, e, *this, dt);
	}

	void applyStep(PlanStep& step, Entity& e, ServerInterface& server) {
		PATHLOG(m_eid,
			"tick: stepIdx=%d/%zu kind=%s useNavigator=%d "
			"target=(%.2f,%.2f,%.2f) anchor=%d pos=(%.2f,%.2f,%.2f) "
			"entVel=(%.2f,%.2f,%.2f)",
			m_stepIndex, m_plan.size(), toString(step.type),
			step.useNavigator ? 1 : 0,
			step.targetPos.x, step.targetPos.y, step.targetPos.z,
			(int)step.anchorEntityId,
			e.position.x, e.position.y, e.position.z,
			e.velocity.x, e.velocity.y, e.velocity.z);
		actionFor(step.type).apply(step, e, *this, server);
	}

	// ── Navigator-driven approach (Python useNavigator=true) ──────────────
	// Shared by MoveAction / HarvestAction. One tick: ensures the per-agent
	// Navigator is live, (re-)asserts the goal cell, dispatches the next
	// primitive (Move along a waypoint / Interact on a closed door), and
	// returns the terminal status when the plan resolves. Returns Walking
	// during normal progress; caller acts on Arrived/Failed.
	enum class NavTickResult { Walking, Arrived, Failed };

	NavTickResult navigateApproach(Entity& e, glm::ivec3 goalCell,
	                               ServerInterface& server) {
		if (!m_navigator) {
			m_navWorldView = std::make_unique<ChunkWorldView>(
				server.chunks(), server.blockRegistry());
			m_navDoors = std::make_unique<ChunkDoorOracle>(
				server.chunks(), server.blockRegistry());
			m_navigator = std::make_unique<Navigator>(
				*m_navWorldView, m_navDoors.get());
			m_navLastGoal    = glm::ivec3(INT_MIN);
			m_navPrevStatus  = Navigator::Status::Idle;
		}

		// Navigator::setGoal is a no-op when the cell matches the active plan;
		// guarding with a cached cell avoids the cost for the 99% steady case.
		if (goalCell != m_navLastGoal) {
			bool hadActivePlan = m_navigator->hasGoal()
			                    && m_navLastGoal != glm::ivec3(INT_MIN);
			// Lifecycle hook #5 — route replacement. Only fires when a
			// non-sentinel prior goal is being overwritten; the first-time
			// setGoal on a fresh Navigator falls through to pathfind-start.
			if (hadActivePlan) {
				PATHLOG(m_eid,
					"pathfind: replaceRoute oldGoal=(%d,%d,%d) "
					"newGoal=(%d,%d,%d) oldStatus=%s execState=%s "
					"pathSteps=%zu",
					m_navLastGoal.x, m_navLastGoal.y, m_navLastGoal.z,
					goalCell.x, goalCell.y, goalCell.z,
					toString(m_navPrevStatus),
					toString(m_execState),
					m_navigator->pathStepCount());
			}
			// Lifecycle hook #1 — pathfinding start. Every setGoal is a
			// fresh A*; log it with enough context to correlate the plan
			// that pops out on the next tick.
			PATHLOG(m_eid,
				"pathfind: start goal=(%d,%d,%d) from=(%.2f,%.2f,%.2f) "
				"entCell=(%d,%d,%d)",
				goalCell.x, goalCell.y, goalCell.z,
				e.position.x, e.position.y, e.position.z,
				(int)std::floor(e.position.x),
				(int)std::floor(e.position.y),
				(int)std::floor(e.position.z));
			PATHLOG(m_eid,
				"nav: setGoal goal=(%d,%d,%d) from=(%.2f,%.2f,%.2f) "
				"entCell=(%d,%d,%d)",
				goalCell.x, goalCell.y, goalCell.z,
				e.position.x, e.position.y, e.position.z,
				(int)std::floor(e.position.x),
				(int)std::floor(e.position.y),
				(int)std::floor(e.position.z));
			m_navigator->setGoal(goalCell);
			m_navLastGoal   = goalCell;
			m_navPrevStatus = Navigator::Status::Planning;
			m_execState     = ExecState::Planning;
			m_execLoggedFirstWaypoint = false;
		}

		Navigator::Step step = m_navigator->tick(e.position);
		Navigator::Status st = m_navigator->status();

		// Single source of truth for F3 viz: mirror what the PathExecutor is
		// actually walking. Without this, rebuildViz's placeholder (the
		// Harvest step's targetPos, which is the leaf block midair) would
		// keep the dashes pointing at a cell no one can stand in. Sync every
		// tick so the drawn path collapses as cells are consumed, and the
		// post-arrival empty path naturally clears the dashes.
		{
			const Path& p = m_navigator->path();
			m_viz.waypoints.clear();
			m_viz.waypoints.reserve(p.steps.size());
			for (const Waypoint& w : p.steps) {
				m_viz.waypoints.emplace_back(
					(float)w.pos.x + 0.5f,
					(float)w.pos.y + 0.5f,
					(float)w.pos.z + 0.5f);
			}
		}

		// Transition trace: fires at most a handful of times per plan
		// (Idle→Planning→Walking↔OpeningDoor→Arrived|Failed).
		if (st != m_navPrevStatus) {
			if (st == Navigator::Status::Walking) {
				PATHLOG(m_eid,
					"nav: %s -> Walking steps=%zu partial=%d next=(%.2f,%.2f,%.2f)",
					toString(m_navPrevStatus),
					m_navigator->pathStepCount(),
					m_navigator->pathPartial() ? 1 : 0,
					step.moveTarget.x, step.moveTarget.y, step.moveTarget.z);
				const Path& pth = m_navigator->path();
				for (size_t i = 0; i < pth.steps.size(); ++i) {
					const Waypoint& w = pth.steps[i];
					PATHLOG(m_eid,
						"nav: waypoint[%zu/%zu] (%d,%d,%d) %s",
						i + 1, pth.steps.size(),
						w.pos.x, w.pos.y, w.pos.z, toString(w.kind));
				}
			} else if (st == Navigator::Status::Failed) {
				PATHLOG(m_eid,
					"nav: %s -> Failed steps=%zu partial=%d reason=\"%s\"",
					toString(m_navPrevStatus),
					m_navigator->pathStepCount(),
					m_navigator->pathPartial() ? 1 : 0,
					m_navigator->failureReason().c_str());
				if (m_navigator->pathStepCount() == 0) {
					logStartCellDiagnostic(e, server);
				}
			} else if (st == Navigator::Status::Arrived) {
				PATHLOG(m_eid,
					"nav: %s -> Arrived at (%.2f,%.2f,%.2f)",
					toString(m_navPrevStatus),
					e.position.x, e.position.y, e.position.z);
			} else {
				PATHLOG(m_eid, "nav: %s -> %s",
					toString(m_navPrevStatus), toString(st));
			}
			m_navPrevStatus = st;
		}

		// Heartbeat: while Walking, dump the full remaining plan every ~1s so
		// the log captures whether the plan is stable across ticks or being
		// churned. On a stable plan the same waypoints reappear each second
		// with the cursor creeping forward; a re-plan shows different cells.
		if (st == Navigator::Status::Walking) {
			auto nowTp = std::chrono::steady_clock::now();
			auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
				nowTp - m_navLastWalkLog).count();
			if (m_navLastWalkLog.time_since_epoch().count() == 0 ||
			    since >= 1000) {
				m_navLastWalkLog = nowTp;
				const Path& pth = m_navigator->path();
				PATHLOG(m_eid,
					"nav: walking pos=(%.2f,%.2f,%.2f) goal=(%d,%d,%d) "
					"steps=%zu partial=%d next=(%.2f,%.2f,%.2f)",
					e.position.x, e.position.y, e.position.z,
					m_navLastGoal.x, m_navLastGoal.y, m_navLastGoal.z,
					pth.steps.size(),
					m_navigator->pathPartial() ? 1 : 0,
					step.moveTarget.x, step.moveTarget.y, step.moveTarget.z);
				for (size_t i = 0; i < pth.steps.size(); ++i) {
					const Waypoint& w = pth.steps[i];
					PATHLOG(m_eid,
						"nav: walking waypoint[%zu/%zu] (%d,%d,%d) %s",
						i + 1, pth.steps.size(),
						w.pos.x, w.pos.y, w.pos.z, toString(w.kind));
				}
			}
		} else {
			m_navLastWalkLog = {};
		}

		if (st == Navigator::Status::Failed) {
			sendStopMove(e, server, "nav-failed");
			return NavTickResult::Failed;
		}
		if (st == Navigator::Status::Arrived) {
			sendStopMove(e, server, "nav-arrived");
			return NavTickResult::Arrived;
		}

		if (step.kind == Navigator::Step::Move) {
			glm::vec3 delta = step.moveTarget - e.position;
			delta.y = 0;
			float dist = glm::length(delta);
			if (dist < 0.01f) {
				sendStopMove(e, server, "nav-near-waypoint");
				return NavTickResult::Walking;
			}
			glm::vec3 desired = delta / dist;
			// Cap per-tick rotation so pops don't snap the heading.
			glm::vec3 dir = rotateTowardXZ(m_lastMoveDir, desired,
			                               PathExecutor::kMaxTurnPerTick60);
			m_lastMoveDir = dir;
			sendMove(e, dir * e.def().walk_speed, server, "nav-waypoint");
		} else if (step.kind == Navigator::Step::Interact) {
			// One Interact per connected door slab — server fans toggle
			// vertically through the pillar; we cover the horizontal cluster.
			for (auto pos : step.interactPos) {
				ActionProposal p;
				p.type          = ActionProposal::Interact;
				p.actorId       = m_eid;
				p.blockPos      = pos;
				p.appearanceIdx = -1;  // legacy toggle (door)
				server.sendAction(p);
			}
			sendStopMove(e, server, "nav-interact");
		} else {
			sendStopMove(e, server, "nav-unknown");
		}
		return NavTickResult::Walking;
	}

	// Diagnostic — fired when A* returns steps=0 from the entity's feet cell.
	// Physics can hold the entity in cells whose neighbors aren't standable
	// under the planner's floor-solid + head/body-air contract (half-blocks,
	// fences, water, sunken terrain). Logs the entity's cell and, for each of
	// 12 candidate neighbors (walk/jump/descend × 4 cardinals), whether the
	// three standability sub-conditions hold. Gives us the concrete reason an
	// entity is un-planable from its current spot without stepping through A*.
	void logStartCellDiagnostic(Entity& e, ServerInterface& server) {
		auto isSolid = [&](int x, int y, int z) {
			return server.blockRegistry()
				.get(server.chunks().getBlock(x, y, z)).solid;
		};
		glm::ivec3 c{
			(int)std::floor(e.position.x),
			(int)std::floor(e.position.y),
			(int)std::floor(e.position.z)};
		PATHLOG(m_eid,
			"nav: start-diag cell=(%d,%d,%d) pos=(%.2f,%.2f,%.2f) "
			"floor=%d body=%d head=%d headroom=%d",
			c.x, c.y, c.z,
			e.position.x, e.position.y, e.position.z,
			isSolid(c.x, c.y - 1, c.z) ? 1 : 0,
			isSolid(c.x, c.y,     c.z) ? 1 : 0,
			isSolid(c.x, c.y + 1, c.z) ? 1 : 0,
			isSolid(c.x, c.y + 2, c.z) ? 1 : 0);
		const int DX[4] = {1,-1,0,0};
		const int DZ[4] = {0,0,1,-1};
		const char* DN[4] = {"+x","-x","+z","-z"};
		for (int i = 0; i < 4; i++) {
			int nx = c.x + DX[i], nz = c.z + DZ[i];
			auto stand = [&](int y) {
				bool floorSolid = isSolid(nx, y - 1, nz);
				bool bodyAir    = !isSolid(nx, y,     nz);
				bool headAir    = !isSolid(nx, y + 1, nz);
				return std::make_tuple(floorSolid, bodyAir, headAir);
			};
			auto [wF, wB, wH] = stand(c.y);       // Walk
			auto [jF, jB, jH] = stand(c.y + 1);   // Jump
			auto [dF, dB, dH] = stand(c.y - 1);   // Descend
			PATHLOG(m_eid,
				"nav: start-diag %s walk=%d%d%d jump=%d%d%d descend=%d%d%d",
				DN[i],
				wF?1:0, wB?1:0, wH?1:0,
				jF?1:0, jB?1:0, jH?1:0,
				dF?1:0, dB?1:0, dH?1:0);
		}
	}

	// Drop any active Navigator plan — called when a step ends so the next
	// Move/Harvest doesn't inherit stale goal-cell cache.
	void clearNavigator() {
		if (m_navigator) m_navigator->clear();
		m_navLastGoal = glm::ivec3(INT_MIN);
	}

	// Pick the Navigator's goal cell for a given anchor. Default: floor(anchor).
	// With step.ignoreHeight, walk the (x, z) column downward from the anchor
	// Y, skip past any tree / canopy solids until we cross into an air gap,
	// then find the first ground surface below that gap and return the air
	// cell directly above it. Woodcutter uses this so an overhead leaf
	// becomes "walk to the root of the tree"; the gatherRadius sphere still
	// reaches the canopy from ground level. Falls back to floor(anchor) when
	// no ground is found (mid-air leaf over a void — next candidate rotates).
	glm::ivec3 resolveNavGoal(glm::vec3 anchor, const PlanStep& step,
	                          ServerInterface& server) const {
		glm::ivec3 cell{
			(int)std::floor(anchor.x),
			(int)std::floor(anchor.y),
			(int)std::floor(anchor.z)};
		if (!step.ignoreHeight) return cell;

		auto isSolid = [&](int x, int y, int z) {
			return server.blockRegistry()
				.get(server.chunks().getBlock(x, y, z)).solid;
		};
		constexpr int kMaxProbe = 64;
		int yMin = cell.y - kMaxProbe;
		int y    = cell.y;
		while (y >= yMin && isSolid(cell.x, y, cell.z)) y--;  // skip canopy
		while (y >= yMin && !isSolid(cell.x, y, cell.z)) y--; // fall to ground
		glm::ivec3 resolved = (y >= yMin)
			? glm::ivec3(cell.x, y + 1, cell.z) : cell;
		// Dedupe: applyHarvest calls this every tick with the same anchor;
		// only log the first time we observe a given (anchor → ground) pair
		// on this agent to keep the trace readable.
		if (resolved != cell &&
		    (m_navLastResolvedAnchor != cell ||
		     m_navLastResolvedCell   != resolved)) {
			PATHLOG(m_eid,
				"nav: resolveNavGoal anchor=(%d,%d,%d) -> ground=(%d,%d,%d)",
				cell.x, cell.y, cell.z,
				resolved.x, resolved.y, resolved.z);
			m_navLastResolvedAnchor = cell;
			m_navLastResolvedCell   = resolved;
		}
		return resolved;
	}

	// ── Move emission + stuck telemetry ──────────────────────────────────
	void sendMove(Entity& e, glm::vec3 vel, ServerInterface& server,
	              const char* source) {
		PATHLOG(m_eid,
			"steer: source=%s vel=(%.2f,%.2f,%.2f) pos=(%.2f,%.2f,%.2f) "
			"entVel=(%.2f,%.2f,%.2f)",
			source ? source : "?",
			vel.x, vel.y, vel.z,
			e.position.x, e.position.y, e.position.z,
			e.velocity.x, e.velocity.y, e.velocity.z);

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

	void sendStopMove(Entity& e, ServerInterface& server, const char* source) {
		sendMove(e, {0, 0, 0}, server, source);
		m_lastMoveDir = glm::vec3(0);   // next nav leg snaps to new heading
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
	Jitter         m_jitter;

	Plan        m_plan;
	int         m_stepIndex = 0;
	std::string m_goalText;
	PlanViz     m_viz;
	StepWatch   m_watch;

	int m_prevHp = 0;

	// Smoothed XZ heading last sent in a nav-waypoint Move — fed back into
	// rotateTowardXZ next tick so desiredVel stays C¹-continuous on pops.
	glm::vec3 m_lastMoveDir{0, 0, 0};

	// Schedule flags.
	bool        m_needsDecide    = false;
	bool        m_reactInFlight  = false;
	bool        m_decideInFlight = false;
	// Flips true the first time onDecideResult lands (plan OR no-op). Loading
	// screen reads this per agent to know when every owned NPC has had a real
	// first thought; the game only hands off to Playing once the whole fleet
	// has settled, so the first rendered frame isn't under decide load.
	bool        m_firstDecideDone = false;
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

	// Seconds until the next harvest swing is allowed. Lives on the agent,
	// not on PlanStep, so a re-plan mid-chop doesn't grant a free swing.
	float     m_chopCooldown        = 0.0f;

	// A* Navigator — lazy-constructed on first step with useNavigator=true.
	// WorldView/Oracle hold ChunkSource& + BlockRegistry& refs into the
	// ServerInterface; unique_ptr keeps their addresses stable because
	// Navigator captures the WorldView by ref.
	std::unique_ptr<ChunkWorldView>  m_navWorldView;
	std::unique_ptr<ChunkDoorOracle> m_navDoors;
	std::unique_ptr<Navigator>             m_navigator;
	glm::ivec3                             m_navLastGoal{INT_MIN};
	Navigator::Status                      m_navPrevStatus = Navigator::Status::Idle;
	// resolveNavGoal dedupe — the caller invokes per-tick; we only want to
	// log a line when the (anchor, resolved) pair actually changes.
	mutable glm::ivec3                     m_navLastResolvedAnchor{INT_MIN};
	mutable glm::ivec3                     m_navLastResolvedCell  {INT_MIN};

	// Coarse execution state. Advanced at each transition point so the
	// repetitive-decide log can report what the entity was doing when the
	// prior plan ended. Always compiled in (used by UI/logs/gates, not just
	// by PATHLOG). See outcome.h for the enum.
	ExecState m_execState = ExecState::Idle;

	// For the decide-churn detector: wall-clock timestamp of the most recent
	// setNeedsDecide call. steady_clock so we don't need dt plumbed in.
	std::chrono::steady_clock::time_point m_lastSetNeedsDecideAt{};

	// First-waypoint-execute dedupe: the executor tick fires every frame but
	// we only want one "exec start" log line per installed plan.
	bool m_execLoggedFirstWaypoint = false;

	// Heartbeat timer for the periodic "nav: walking" waypoint dump — fires
	// roughly once per second while status==Walking. Confirms the plan is
	// stable (remains the same across ticks) and surfaces how far along the
	// cursor has advanced. Uses steady_clock so we don't need dt plumbing.
	std::chrono::steady_clock::time_point m_navLastWalkLog{};

	// Count of consecutive Failed_* plan outcomes. setNeedsDecide increments
	// on any isFailed(stateHint), resets on a non-failure state. At
	// kFailStreakGiveUp, setNeedsDecide promotes the next failure to
	// Failed_GaveUp — decide() is expected to route that into the town-
	// center complaint path instead of retrying another doomed plan.
	int m_failStreak = 0;
};

} // namespace civcraft
