#pragma once

#include "shared/types.h"
#include "server/world.h"
#include <glm/glm.hpp>
#include <optional>

namespace aicraft {

struct RayHit {
	glm::ivec3 blockPos;
	glm::ivec3 placePos;
	glm::vec3  normal;
	float      distance;
	BlockId    blockId;
};

inline std::optional<RayHit> raycastBlocks(World& world, glm::vec3 origin,
                                            glm::vec3 dir, float maxDist) {
	dir = glm::normalize(dir);
	glm::ivec3 pos((int)std::floor(origin.x), (int)std::floor(origin.y), (int)std::floor(origin.z));
	glm::ivec3 step(dir.x >= 0 ? 1 : -1, dir.y >= 0 ? 1 : -1, dir.z >= 0 ? 1 : -1);
	glm::vec3 tMax(
		dir.x != 0 ? ((dir.x > 0 ? (pos.x + 1.0f - origin.x) : (origin.x - pos.x)) / std::abs(dir.x)) : 1e30f,
		dir.y != 0 ? ((dir.y > 0 ? (pos.y + 1.0f - origin.y) : (origin.y - pos.y)) / std::abs(dir.y)) : 1e30f,
		dir.z != 0 ? ((dir.z > 0 ? (pos.z + 1.0f - origin.z) : (origin.z - pos.z)) / std::abs(dir.z)) : 1e30f);
	glm::vec3 tDelta(
		dir.x != 0 ? std::abs(1.0f / dir.x) : 1e30f,
		dir.y != 0 ? std::abs(1.0f / dir.y) : 1e30f,
		dir.z != 0 ? std::abs(1.0f / dir.z) : 1e30f);

	glm::ivec3 prevPos = pos;
	float dist = 0.0f;

	for (int i = 0; i < (int)(maxDist * 3); i++) {
		BlockId block = world.getBlock(pos.x, pos.y, pos.z);
		if (world.blocks.get(block).solid) {
			glm::ivec3 normal = prevPos - pos;
			return RayHit{pos, prevPos, glm::vec3(normal), dist, block};
		}
		prevPos = pos;
		if (tMax.x < tMax.y && tMax.x < tMax.z) {
			dist = tMax.x; pos.x += step.x; tMax.x += tDelta.x;
		} else if (tMax.y < tMax.z) {
			dist = tMax.y; pos.y += step.y; tMax.y += tDelta.y;
		} else {
			dist = tMax.z; pos.z += step.z; tMax.z += tDelta.z;
		}
		if (dist > maxDist) break;
	}
	return std::nullopt;
}

} // namespace aicraft
