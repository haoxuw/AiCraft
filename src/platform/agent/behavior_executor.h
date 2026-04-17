#pragma once

// Plan-step → ActionProposal translation:
//   Move→TYPE_MOVE, Harvest/Attack→pathfind+TYPE_CONVERT, Relocate→pathfind+TYPE_RELOCATE.
// gatherNearby() kept — used by LocalWorld construction.

#include "server/behavior.h"
#include "logic/entity.h"
#include "logic/action.h"
#include <unordered_map>
#include <vector>
#include <functional>
#include <cmath>

namespace civcraft {

// Python pathfind get_block() query — shared chunk cache.
using BlockTypeFn = std::function<std::string(int, int, int)>;

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
