#pragma once

// Outcome types shared by Agent (producer) and DecideWorker (consumer).
// Split out so neither has to include the other.

#include <string>
#include <utility>

namespace civcraft {

// Handed to next decide() so Python can branch on outcome.
enum class StepOutcome {
	InProgress,   // evaluator sentinel, never published
	Success,
	Failed,
};

// Coarse execution-layer state for an Agent, kept alongside goalText so the
// repetitive-decide logger (and future UI) can see *why* the previous plan
// ended without string-matching. Mirrors Navigator's states plus the non-nav
// executor modes so a reader can tell at a glance whether the entity is
// stuck mid-A*, chasing directly, harvesting, etc.
//
// The Failed_* variants exist so the NEXT decide() can branch on the specific
// reason (e.g. NavNoPath → try a different target; TargetGone → pick a new
// anchor; Stuck → wait and retry). Any new failure site must pick the most
// specific variant it can justify; fall back to Failed_Unknown only if none
// fits. Keep toString() in sync.
enum class ExecState {
	Idle,           // no plan installed (pre-first decide, or post-finish)
	PlanRequested,  // setNeedsDecide fired; awaiting DecideWorker result
	Planning,       // Navigator::setGoal issued; A* has not returned Walking yet
	DirectApproach, // applyMove straight-line (no Navigator, or anchor-chase)
	Walking,        // Navigator is feeding waypoints to the executor
	OpeningDoor,    // Navigator emitted Interact on a closed door
	Harvesting,     // applyHarvest: chopping in range
	Interacting,    // applyInteract in flight
	Relocating,     // applyRelocate just dispatched
	Attacking,      // applyAttack in flight
	Arrived,        // terminal success of the current plan

	// --- Terminal failures (keep contiguous; isFailed() uses range check) ---
	Failed_NavNoPath,        // Navigator gave up: empty path / partial + unreachable
	Failed_NavStuck,         // Walking but entity stopped making progress (Move-Stuck)
	Failed_DirectStuck,      // Direct-approach (non-nav) stuck; velocity held, no displace
	Failed_TargetGone,       // Target entity disappeared (died, despawned, removed)
	Failed_AnchorGone,       // Anchor block (resource/structure) no longer present
	Failed_HarvestExhausted, // Reached target, nothing harvestable in range
	Failed_DecideError,      // Python decide() raised or returned invalid plan
	Failed_Interrupted,      // Interrupt signal (hp drop, react) aborted plan mid-step
	Failed_GaveUp,           // Fail streak exceeded threshold — don't retry until rescued
	Failed_Unknown,          // Fallback when the site can't attribute more precisely
};

constexpr const char* toString(ExecState s) {
	switch (s) {
		case ExecState::Idle:                   return "Idle";
		case ExecState::PlanRequested:          return "PlanRequested";
		case ExecState::Planning:               return "Planning";
		case ExecState::DirectApproach:         return "DirectApproach";
		case ExecState::Walking:                return "Walking";
		case ExecState::OpeningDoor:            return "OpeningDoor";
		case ExecState::Harvesting:             return "Harvesting";
		case ExecState::Interacting:            return "Interacting";
		case ExecState::Relocating:             return "Relocating";
		case ExecState::Attacking:              return "Attacking";
		case ExecState::Arrived:                return "Arrived";
		case ExecState::Failed_NavNoPath:       return "Failed_NavNoPath";
		case ExecState::Failed_NavStuck:        return "Failed_NavStuck";
		case ExecState::Failed_DirectStuck:     return "Failed_DirectStuck";
		case ExecState::Failed_TargetGone:      return "Failed_TargetGone";
		case ExecState::Failed_AnchorGone:      return "Failed_AnchorGone";
		case ExecState::Failed_HarvestExhausted:return "Failed_HarvestExhausted";
		case ExecState::Failed_DecideError:     return "Failed_DecideError";
		case ExecState::Failed_Interrupted:     return "Failed_Interrupted";
		case ExecState::Failed_GaveUp:          return "Failed_GaveUp";
		case ExecState::Failed_Unknown:         return "Failed_Unknown";
	}
	return "?";
}

// Range check — all Failed_* variants are contiguous after Arrived. Callers
// use this to classify whether the previous plan ended in failure without
// enumerating every variant.
constexpr bool isFailed(ExecState s) {
	return s >= ExecState::Failed_NavNoPath && s <= ExecState::Failed_Unknown;
}

// Give-up is "soft terminal" — the agent stopped retrying but a rescue event
// (player interaction, world change, new signal) could revive it. Callers
// use this to decide whether to route the agent into the town-center
// complaint pipeline rather than spin on decide retries.
constexpr bool isGaveUp(ExecState s) {
	return s == ExecState::Failed_GaveUp;
}

// Navigation-layer failures — the entity couldn't *reach* its goal. Distinct
// from TargetGone / AnchorGone (target disappeared) and HarvestExhausted
// (reached but nothing to do). Python behaviors branch on this to back off
// their movement target; exposed as LocalWorld.last_nav_failed.
constexpr bool isNavFailed(ExecState s) {
	return s == ExecState::Failed_NavNoPath
	    || s == ExecState::Failed_NavStuck
	    || s == ExecState::Failed_DirectStuck
	    || s == ExecState::Failed_GaveUp;
}

struct LastOutcome {
	StepOutcome  outcome     = StepOutcome::Success;
	std::string  goalText;
	int          stepTypeRaw = 0;            // PlanStep::Type as int to avoid include cycle
	std::string  reason;                     // "stuck", "target_gone", "interrupt:…", "discovery", …
	ExecState    execState   = ExecState::Idle;  // carries across decides so the next one can see why the last ended
	int          failStreak  = 0;            // consecutive Failed_* outcomes; reset on Success
};

} // namespace civcraft
