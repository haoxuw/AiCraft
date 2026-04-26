#pragma once

// Ray-AABB crosshair hit-test against entity collision boxes.

#include "logic/entity.h"
#include <glm/glm.hpp>
#include <optional>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>

namespace solarium {

struct EntityHit {
	EntityId entityId;
	float distance;
	glm::vec3 hitPoint;
	std::string typeId;
	std::string goalText;
	bool hasError = false;
};

// Slab method.
inline bool rayIntersectsAABB(glm::vec3 origin, glm::vec3 dir,
                               glm::vec3 boxMin, glm::vec3 boxMax,
                               float& tOut) {
	glm::vec3 invDir = 1.0f / dir;

	float t1 = (boxMin.x - origin.x) * invDir.x;
	float t2 = (boxMax.x - origin.x) * invDir.x;
	float t3 = (boxMin.y - origin.y) * invDir.y;
	float t4 = (boxMax.y - origin.y) * invDir.y;
	float t5 = (boxMin.z - origin.z) * invDir.z;
	float t6 = (boxMax.z - origin.z) * invDir.z;

	float tmin = std::max({std::min(t1, t2), std::min(t3, t4), std::min(t5, t6)});
	float tmax = std::min({std::max(t1, t2), std::max(t3, t4), std::max(t5, t6)});

	if (tmax < 0 || tmin > tmax) return false;
	tOut = tmin >= 0 ? tmin : tmax;
	return true;
}

struct RaycastEntity {
	EntityId id;
	std::string typeId;
	glm::vec3 position;
	glm::vec3 collisionMin;
	glm::vec3 collisionMax;
	std::string goalText;
	bool hasError = false;
};

// Returns closest hit within maxDist.
inline std::optional<EntityHit> raycastEntities(
	const std::vector<RaycastEntity>& entities,
	glm::vec3 origin, glm::vec3 dir, float maxDist,
	EntityId excludeId = ENTITY_NONE)
{
	std::optional<EntityHit> closest;
	float closestDist = maxDist;

	for (auto& e : entities) {
		if (e.id == excludeId) continue;

		glm::vec3 boxMin = e.position + e.collisionMin;
		glm::vec3 boxMax = e.position + e.collisionMax;

		float t;
		if (rayIntersectsAABB(origin, dir, boxMin, boxMax, t)) {
			if (t < closestDist && t >= 0) {
				closestDist = t;
				closest = EntityHit{
					e.id, t, origin + dir * t,
					e.typeId, e.goalText, e.hasError
				};
			}
		}
	}

	return closest;
}

} // namespace solarium
