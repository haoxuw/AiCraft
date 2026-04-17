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
#include "logic/entity.h"
#include "logic/action.h"
#include <unordered_map>
#include <vector>
#include <functional>
#include <cmath>

namespace civcraft {

// Block query function: returns block type string at world position.
// Used by Python pathfinding (get_block()) — queries shared chunk cache.
using BlockTypeFn = std::function<std::string(int, int, int)>;

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

} // namespace civcraft
