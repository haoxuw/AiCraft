#pragma once

/**
 * BehaviorExecutor — converts BehaviorAction to ActionProposal.
 *
 * Extracted from EntityManager::behaviorToMoveProposal().
 * Used by the bot client to translate Python AI decisions into
 * server-compatible ActionProposals sent over TCP.
 *
 * Also contains gatherNearby() and block scanning (getKnownBlocks)
 * that operate on the bot's local state caches instead of the server's World.
 */

#include "server/behavior.h"
#include "shared/entity.h"
#include "shared/action.h"
#include "shared/constants.h"
#include <unordered_map>
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <glm/trigonometric.hpp>

namespace agentworld {

// Per-entity state for behavior execution (wander direction, timers)
struct BotBehaviorState {
	std::unique_ptr<Behavior> behavior;
	BehaviorAction currentAction;
	float decideTimer = 0;
	float wanderYaw = 0;
};

// Block query function: returns block type string at world position
using BlockTypeFn = std::function<std::string(int, int, int)>;

// Block cache: one sample position per block type, per entity.
struct BlockCache {
	std::unordered_map<std::string, glm::ivec3> known;
	bool initialized = false;
	glm::vec3 scanCenter = {0, 0, 0};
};

// Convert a BehaviorAction to ActionProposal(s) and push to the output list.
inline void behaviorToActionProposals(Entity& e, BotBehaviorState& state,
                                       const BehaviorAction& action, float dt,
                                       std::vector<ActionProposal>& out) {
	const float TURN_SPEED = 4.0f;
	auto smoothYaw = [&](float targetYaw) {
		float diff = targetYaw - e.yaw;
		while (diff > 180.0f) diff -= 360.0f;
		while (diff < -180.0f) diff += 360.0f;
		e.yaw += diff * std::min(dt * TURN_SPEED, 1.0f);
	};

	ActionProposal p;
	p.type = ActionProposal::Move;
	p.actorId = e.id();
	p.desiredVel = {0, 0, 0};

	switch (action.type) {
	case BehaviorAction::Idle:
		p.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
		break;

	case BehaviorAction::Wander: {
		if (action.param > 0.5f) {
			unsigned int seed = (unsigned int)(e.id() * 2654435761u +
				(unsigned int)(e.getProp<float>(Prop::Age, 0.0f) * 100));
			seed ^= seed >> 16;
			float r = (float)(seed & 0xFFFF) / 65535.0f;
			state.wanderYaw = e.yaw + (r - 0.5f) * 120.0f;
		}
		smoothYaw(state.wanderYaw);
		float rad = glm::radians(e.yaw);
		p.desiredVel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
		break;
	}

	case BehaviorAction::MoveTo: {
		glm::vec3 dir = action.targetPos - e.position;
		dir.y = 0;
		float dist = glm::length(dir);
		if (dist > 0.5f) {
			dir /= dist;
			smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
			float rad = glm::radians(e.yaw);
			p.desiredVel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
		}
		break;
	}

	case BehaviorAction::Follow: {
		glm::vec3 dir = action.targetPos - e.position;
		dir.y = 0;
		float dist = glm::length(dir);
		float minDist = std::max(action.param, 1.5f);
		if (dist > minDist) {
			dir /= dist;
			smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
			float rad = glm::radians(e.yaw);
			p.desiredVel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
		}
		break;
	}

	case BehaviorAction::Flee: {
		glm::vec3 dir = e.position - action.targetPos;
		dir.y = 0;
		float dist = glm::length(dir);
		if (dist > 0.1f) {
			dir /= dist;
			smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
		}
		float rad = glm::radians(e.yaw);
		p.desiredVel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
		break;
	}

	case BehaviorAction::LookAt: {
		glm::vec3 dir = action.targetPos - e.position;
		if (glm::length(glm::vec2(dir.x, dir.z)) > 0.01f)
			smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
		p.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
		break;
	}

	case BehaviorAction::Attack:
		p.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
		break;

	case BehaviorAction::BreakBlock: {
		ActionProposal bp;
		bp.type = ActionProposal::BreakBlock;
		bp.actorId = e.id();
		bp.blockPos = glm::ivec3((int)action.targetPos.x, (int)action.targetPos.y, (int)action.targetPos.z);
		out.push_back(bp);
		p.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
		break;
	}
	case BehaviorAction::DropItem: {
		ActionProposal dp;
		dp.type = ActionProposal::DropItem;
		dp.actorId = e.id();
		dp.blockType = action.itemType;
		dp.itemCount = action.itemCount;
		out.push_back(dp);
		break;
	}
	} // end switch

	out.push_back(p);
}

// Gather nearby entity info from a local entity cache.
inline std::vector<NearbyEntity> gatherNearby(
		const Entity& self,
		const std::unordered_map<EntityId, std::unique_ptr<Entity>>& entities,
		float radius) {
	std::vector<NearbyEntity> result;
	float r2 = radius * radius;
	for (auto& [id, e] : entities) {
		if (e->removed || id == self.id()) continue;
		float d2 = glm::dot(e->position - self.position, e->position - self.position);
		if (d2 <= r2) {
			result.push_back({
				id, e->typeId(), e->def().category,
				e->position, std::sqrt(d2),
				e->getProp<int>(Prop::HP, e->def().max_hp)
			});
		}
	}
	return result;
}

// Scan blocks near entity position. Uses a cache for efficiency.
inline std::vector<NearbyBlock> getKnownBlocks(
		Entity& e, int radius, const BlockTypeFn& getType,
		BlockCache& cache) {
	// Rescan if entity has moved far from original scan center
	if (cache.initialized) {
		float movedDist = glm::length(e.position - cache.scanCenter);
		if (movedDist > (float)radius * 0.5f)
			cache.initialized = false;
	}

	if (!cache.initialized) {
		cache.initialized = true;
		cache.scanCenter = e.position;
		cache.known.clear();
		int cx = (int)e.position.x, cy = (int)e.position.y, cz = (int)e.position.z;
		for (int dy = -2; dy <= 12; dy++)
			for (int dz = -radius; dz <= radius; dz += 3)
				for (int dx = -radius; dx <= radius; dx += 3) {
					if (dx*dx + dz*dz > radius*radius) continue;
					std::string type = getType(cx+dx, cy+dy, cz+dz);
					if (type.empty() || type == "base:air" ||
					    type == "base:dirt" || type == "base:grass" ||
					    type == "base:stone" || type == "base:sand") continue;
					if (cache.known.find(type) == cache.known.end())
						cache.known[type] = {cx+dx, cy+dy, cz+dz};
				}
	} else {
		std::vector<std::string> gone;
		for (auto& [type, pos] : cache.known) {
			std::string actual = getType(pos.x, pos.y, pos.z);
			if (actual != type) gone.push_back(type);
		}
		for (auto& g : gone) {
			cache.known.erase(g);
			int cx = (int)e.position.x, cy = (int)e.position.y, cz = (int)e.position.z;
			for (int dy = 0; dy <= 10; dy++)
				for (int dz = -radius; dz <= radius; dz += 4)
					for (int dx = -radius; dx <= radius; dx += 4) {
						if (dx*dx + dz*dz > radius*radius) continue;
						std::string t = getType(cx+dx, cy+dy, cz+dz);
						if (t == g) { cache.known[g] = {cx+dx, cy+dy, cz+dz}; goto found; }
					}
			found:;
		}
	}

	std::vector<NearbyBlock> result;
	for (auto& [type, pos] : cache.known) {
		float dist = glm::length(glm::vec3(pos) - e.position);
		result.push_back({pos, type, dist});
	}
	std::sort(result.begin(), result.end(), [](const NearbyBlock& a, const NearbyBlock& b) {
		return a.distance < b.distance;
	});
	return result;
}

} // namespace agentworld
