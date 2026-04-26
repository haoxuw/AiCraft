#pragma once

// Per-action executor handlers. Each concrete ActionHandler owns the
// evaluate() + apply() bodies for one PlanStep::Type, keeping Agent out
// of the business of knowing what a harvest swing does vs. how a Move
// arrives vs. what Interact sends. Agent's per-tick loop just looks up
// the handler via actionFor() and delegates.
//
// Handlers read/write Agent state (m_watch, m_navigator, m_chopCooldown,
// m_jitter) through friend access rather than a side-channel context
// struct — they're part of the same component as Agent, not an arm's-
// length consumer, so a clean object boundary would be ceremony without
// benefit.
//
// Adding a new PlanStep type:
//   1. Add the enum variant in behavior.h.
//   2. Subclass ActionHandler here with evaluate()/apply().
//   3. Register the singleton in actionFor() below.
//   4. Teach python_bridge.cpp's parsePyResult about the type tag.

#include "agent/behavior.h"           // PlanStep
#include "agent/outcome.h"            // StepOutcome, ExecState
#include "net/server_interface.h"
#include "debug/entity_log.h"
#include "agent/pathlog.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

namespace solarium {

class Agent;  // fwd — defined in agent.h, friended by handlers via Agent.

class ActionHandler {
public:
	virtual ~ActionHandler() = default;

	// Per-tick step-state read. Returns Success/Failed/InProgress; Agent
	// drives the transition (advanceStep / finishPlan) based on the result.
	virtual StepOutcome evaluate(PlanStep& step, Entity& e,
	                              Agent& agent, float dt) = 0;

	// Per-tick side effects — dispatches ActionProposals, drives the
	// Navigator, rotates harvest anchors. Only called while evaluate()
	// last returned InProgress (and the inventory-capacity gate passed).
	virtual void apply(PlanStep& step, Entity& e,
	                   Agent& agent, ServerInterface& server) = 0;
};

// ── Move ────────────────────────────────────────────────────────────────
class MoveAction final : public ActionHandler {
public:
	StepOutcome evaluate(PlanStep& step, Entity& e,
	                      Agent& agent, float dt) override;
	void apply(PlanStep& step, Entity& e,
	           Agent& agent, ServerInterface& server) override;
};

// ── Harvest ─────────────────────────────────────────────────────────────
// Walk to a candidate anchor, scan a gatherRadius volume (sphere for mining;
// cylinder for chopping — see HarvestAction::findHits), swing on cooldown,
// repeat until the inventory-capacity gate ends the step. Jitter influences
// which hit is picked among equally-in-range candidates and which anchor
// comes next after a wedge — warmer temperature → more willingness to skip
// the strictly-nearest choice.
class HarvestAction final : public ActionHandler {
public:
	// Leaves/canopy reach above the villager (chop only); stumps / buried
	// roots below. Sphere mining keeps to ±radius on all axes.
	static constexpr int kChopCanopyUp   = 64;
	static constexpr int kChopCanopyDown = 8;

	StepOutcome evaluate(PlanStep& step, Entity& e,
	                      Agent& agent, float dt) override;
	void apply(PlanStep& step, Entity& e,
	           Agent& agent, ServerInterface& server) override;

	// Active anchor = candidates[activeIdx] if the step has candidates,
	// else the legacy single-anchor targetPos.
	static glm::vec3 activeAnchor(const PlanStep& step, const Agent& agent);

	// Mark the current anchor failed and advance to the next viable index.
	// With jitter>0 the next pick is weighted-random over remaining slots;
	// cold, it's strict next-in-list.
	static bool advanceAnchor(PlanStep& step, Agent& agent);

private:
	// Collect every matching block within the gather volume and, if any,
	// return a (jitter-weighted) pick. Ordering: nearest-first. Volume
	// shape depends on step.ignoreHeight (chop = XZ cylinder, mine =
	// sphere — see kChopCanopyUp/Down).
	static std::optional<glm::ivec3> findHit(
		const std::string& typeName, const glm::vec3& origin,
		float radius, bool ignoreHeight,
		Agent& agent, ServerInterface& server);
};

// ── Attack ──────────────────────────────────────────────────────────────
class AttackAction final : public ActionHandler {
public:
	StepOutcome evaluate(PlanStep& /*step*/, Entity& /*e*/,
	                      Agent& /*agent*/, float /*dt*/) override {
		return StepOutcome::InProgress;
	}
	void apply(PlanStep& step, Entity& e,
	           Agent& agent, ServerInterface& server) override;
};

// ── Relocate ────────────────────────────────────────────────────────────
class RelocateAction final : public ActionHandler {
public:
	StepOutcome evaluate(PlanStep& /*s*/, Entity& /*e*/,
	                      Agent& agent, float /*dt*/) override;
	void apply(PlanStep& step, Entity& e,
	           Agent& agent, ServerInterface& server) override;
};

// ── Interact ────────────────────────────────────────────────────────────
class InteractAction final : public ActionHandler {
public:
	StepOutcome evaluate(PlanStep& /*s*/, Entity& /*e*/,
	                      Agent& agent, float /*dt*/) override;
	void apply(PlanStep& step, Entity& e,
	           Agent& agent, ServerInterface& server) override;
};

// Dispatch table. Returned reference is to a function-local singleton; safe
// to store, never null. Unknown types abort — every PlanStep::Type must
// have a handler registered here.
ActionHandler& actionFor(PlanStep::Type t);

} // namespace solarium
