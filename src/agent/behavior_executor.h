#pragma once

/**
 * BehaviorExecutor — executes PlanSteps by converting them to ActionProposals.
 *
 * NEW ARCHITECTURE (Plan-based):
 *   The old behaviorToActionProposals() converted a single BehaviorAction to
 *   ActionProposals. That's been deleted — replaced by plan-step execution.
 *
 *   Each PlanStep type maps to ActionProposal(s):
 *     Move(pos)        → pathfind waypoints → TYPE_MOVE proposals
 *     Harvest(block)   → pathfind to range + TYPE_CONVERT
 *     Attack(entity)   → pathfind to range + TYPE_CONVERT
 *     Relocate(f,t,i)  → pathfind to range + TYPE_RELOCATE
 *
 * Kept: gatherNearby() for entity awareness (used by LocalWorld construction).
 */

#include "server/behavior.h"
#include "shared/entity.h"
#include "shared/action.h"
#include <unordered_map>
#include <vector>
#include <functional>
#include <cmath>

namespace modcraft {

// Per-entity state for plan execution
struct AgentEntityState {
	int behaviorHandle = -1;  // PythonBridge handle for this entity's behavior

	// TODO: Plan execution state
	// Plan currentPlan;
	// int currentStep = 0;
	// std::vector<glm::vec3> pathWaypoints;  // pathfind result for current Move step
	// int waypointIndex = 0;

	// HP tracking for reactive re-decide (HP drop → immediate re-plan)
	int lastKnownHp = -1;

	// Performance tracking
	float lastDecideMs  = 0.0f;
	float totalDecideMs = 0.0f;
	int   decideCount   = 0;
};

// Block query function: returns block type string at world position.
// Used by Python pathfinding (get_block()) — queries shared chunk cache.
using BlockTypeFn = std::function<std::string(int, int, int)>;

// TODO: Plan step execution
//
// Execute one tick of a PlanStep, emitting ActionProposals:
//
// void executePlanStep(Entity& entity, AgentEntityState& state,
//                      float dt, std::vector<ActionProposal>& out) {
//     switch (currentStep.type) {
//     case PlanStep::Move:
//         // pathfind from entity.position to step.targetPos
//         // emit TYPE_MOVE proposals along waypoints
//         // advance step when entity reaches destination
//         break;
//     case PlanStep::Harvest:
//         // if in range: emit TYPE_CONVERT (break block)
//         // else: pathfind to block, emit TYPE_MOVE
//         break;
//     case PlanStep::Attack:
//         // if in range: emit TYPE_CONVERT (deal damage)
//         // else: pathfind to entity, emit TYPE_MOVE
//         break;
//     case PlanStep::Relocate:
//         // if in range: emit TYPE_RELOCATE
//         // else: pathfind to container, emit TYPE_MOVE
//         break;
//     }
// }

// Gather nearby entity info from the shared entity cache.
inline std::vector<NearbyEntity> gatherNearby(
		const Entity& self,
		const std::unordered_map<EntityId, std::unique_ptr<Entity>>& entities,
		float radius) {
	std::vector<NearbyEntity> result;
	for (auto& [eid, entPtr] : entities) {
		if (!entPtr || entPtr->removed) continue;
		if (eid == self.id()) continue;
		glm::vec3 delta = entPtr->position - self.position;
		float dist = glm::length(delta);
		if (dist > radius) continue;
		NearbyEntity ne;
		ne.id       = eid;
		ne.typeId   = entPtr->typeId();
		ne.kind     = entPtr->def().kind;
		ne.position = entPtr->position;
		ne.distance = dist;
		ne.hp       = entPtr->hp();
		ne.tags     = entPtr->def().tags;
		result.push_back(ne);
	}
	return result;
}

} // namespace modcraft
