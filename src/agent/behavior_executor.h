#pragma once

/**
 * BehaviorExecutor — converts BehaviorAction to ActionProposal.
 *
 * Extracted from EntityManager::behaviorToMoveProposal().
 * Used by the agent client to translate Python AI decisions into
 * server-compatible ActionProposals sent over TCP.
 *
 * Also contains gatherNearby() and block scanning (getKnownBlocks)
 * that operate on the agent's local state caches instead of the server's World.
 */

#include "server/behavior.h"
#include "shared/entity.h"
#include "shared/action.h"
#include "shared/constants.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <glm/trigonometric.hpp>

namespace modcraft {

// Per-entity state for behavior execution (wander direction, timers)
struct AgentBehaviorState {
	std::unique_ptr<Behavior> behavior;
	BehaviorAction currentAction;
	float decideTimer = 0;
	float wanderYaw = 0;
	bool justDecided = false;  // true for the one tick immediately after decide() fires

	// ── Active-trigger support ────────────────────────────────────────────────
	// forceDecide is set by server events (HP drop, time-of-day change) to
	// bypass the passive timer and call decide() immediately this tick.
	bool forceDecide  = false;
	int  lastKnownHp  = -1;   // HP at last decide(); -1 = not yet observed

	// ── Performance tracking ──────────────────────────────────────────────────
	float lastDecideMs  = 0.0f;   // duration of the most recent decide() call
	float totalDecideMs = 0.0f;   // cumulative decide() time
	int   decideCount   = 0;      // total decide() calls fired
};

// Block query function: returns block type string at world position
using BlockTypeFn = std::function<std::string(int, int, int)>;

// Block cache: all known positions per block type within scan radius, per entity.
// Stores multiple positions per type so Python behaviors can pick the closest reachable one.
struct BlockCache {
	std::unordered_map<std::string, std::vector<glm::ivec3>> known;  // type → list of positions
	bool initialized = false;
	glm::vec3 scanCenter = {0, 0, 0};
	float rescanTimer = 0.0f;  // seconds until empty-cache rescan is allowed
};

// Convert a BehaviorAction to ActionProposal(s) and push to the output list.
inline void behaviorToActionProposals(Entity& e, AgentBehaviorState& state,
                                       const BehaviorAction& action, float dt,
                                       std::vector<ActionProposal>& out) {
	const float TURN_SPEED = 4.0f;
	auto smoothYaw = [&](float targetYaw) {
		float diff = targetYaw - e.yaw;
		while (diff > 180.0f) diff -= 360.0f;
		while (diff < -180.0f) diff += 360.0f;
		e.yaw += diff * std::min(dt * TURN_SPEED, 1.0f);
	};

	switch (action.type) {
	case BehaviorAction::Idle: {
		// Friction stop
		ActionProposal p;
		p.type = ActionProposal::Move;
		p.actorId = e.id();
		p.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
		out.push_back(p);
		break;
	}

	case BehaviorAction::Move: {
		ActionProposal p;
		p.type = ActionProposal::Move;
		p.actorId = e.id();
		glm::vec3 dir = action.targetPos - e.position;
		dir.y = 0;
		float dist = glm::length(dir);
		if (dist > 0.5f) {
			dir /= dist;
			smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
			float rad = glm::radians(e.yaw);
			p.desiredVel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
		} else {
			p.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
		}
		out.push_back(p);
		break;
	}

	case BehaviorAction::Relocate: {
		// One-shot action — also produce a friction stop
		ActionProposal stop;
		stop.type = ActionProposal::Move;
		stop.actorId = e.id();
		stop.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
		out.push_back(stop);

		ActionProposal p;
		p.type = ActionProposal::Relocate;
		p.actorId = e.id();
		p.fromEntity = action.fromEntity;
		p.toEntity = action.toEntity;
		p.toGround = action.toGround;
		p.itemId = action.itemId;
		p.itemCount = action.itemCount;
		p.equipSlot = action.equipSlot;
		out.push_back(p);
		break;
	}

	case BehaviorAction::ConvertObject: {
		ActionProposal stop;
		stop.type = ActionProposal::Move;
		stop.actorId = e.id();
		stop.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
		out.push_back(stop);

		ActionProposal p;
		p.type = ActionProposal::ConvertObject;
		p.actorId = e.id();
		p.fromItem = action.fromItem;
		p.fromCount = action.fromCount;
		p.toItem = action.toItem;
		p.toCount = action.toCount;
		p.blockPos = action.blockPos;
		p.convertFromBlock = action.convertFromBlock;
		p.convertToBlock = action.convertToBlock;
		p.convertDirect = action.convertDirect;
		p.convertFromEntity = action.convertFromEntity;
		out.push_back(p);
		break;
	}

	case BehaviorAction::InteractBlock: {
		ActionProposal stop;
		stop.type = ActionProposal::Move;
		stop.actorId = e.id();
		stop.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
		out.push_back(stop);

		ActionProposal p;
		p.type = ActionProposal::InteractBlock;
		p.actorId = e.id();
		p.blockPos = action.blockPos;
		out.push_back(p);
		break;
	}
	} // end switch
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

// Ignored "common" block types that provide no useful navigation info.
static const std::unordered_set<std::string> kIgnoredBlockTypes = {
	"", "base:air", "base:dirt", "base:grass", "base:stone", "base:sand",
	"base:gravel", "base:bedrock", "base:water",
};

// Scan blocks near entity. Stores ALL positions per block type (not just one),
// so Python behaviors can iterate candidates and pick the closest reachable one.
//
// Full rescan: on first call, when entity moves >radius/2, or when all blocks empty.
// Incremental update: removes positions whose block was broken, then re-scans to fill gaps.
inline std::vector<NearbyBlock> getKnownBlocks(
		Entity& e, int radius, const BlockTypeFn& getType,
		BlockCache& cache, float dt = 0.0f) {
	cache.rescanTimer -= dt;

	// Trigger full rescan if entity moved far from last scan center
	if (cache.initialized) {
		float movedDist = glm::length(e.position - cache.scanCenter);
		if (movedDist > (float)radius * 0.5f)
			cache.initialized = false;
		// Also rescan when everything was found to be gone (chunks not loaded yet, or area cleared)
		else if (cache.known.empty() && cache.rescanTimer <= 0.0f)
			cache.initialized = false;
	}

	// Precomputed XZ offsets sorted by distance — built once per (radius) value and reused.
	// Outward-sorted order: nearest XZ columns are checked first.
	// Step=1 (no skipping) ensures 1×1 blocks (trunks) are never missed.
	auto buildXZRing = [](int r) {
		std::vector<std::pair<int,int>> ring;
		ring.reserve((2*r+1)*(2*r+1));
		int r2 = r * r;
		for (int dz = -r; dz <= r; dz++)
			for (int dx = -r; dx <= r; dx++)
				if (dx*dx + dz*dz <= r2)
					ring.push_back({dx, dz});
		std::sort(ring.begin(), ring.end(), [](const std::pair<int,int>& a, const std::pair<int,int>& b) {
			return a.first*a.first + a.second*a.second < b.first*b.first + b.second*b.second;
		});
		return ring;
	};

	auto scanBlocks = [&](int cx, int cy, int cz,
	                      const std::vector<std::pair<int,int>>& ring,
	                      const std::unordered_set<std::string>* filter) {
		for (auto& [dx, dz] : ring) {
			for (int dy = -25; dy <= 15; dy++) {
				std::string type = getType(cx+dx, cy+dy, cz+dz);
				if (kIgnoredBlockTypes.count(type)) continue;
				if (filter && !filter->count(type)) continue;
				cache.known[type].push_back({cx+dx, cy+dy, cz+dz});
			}
		}
	};

	if (!cache.initialized) {
		cache.initialized = true;
		cache.scanCenter = e.position;
		cache.known.clear();
		int cx = (int)e.position.x, cy = (int)e.position.y, cz = (int)e.position.z;
		auto ring = buildXZRing(radius);
		scanBlocks(cx, cy, cz, ring, nullptr);
		if (cache.known.empty()) {
			// No blocks found yet (chunks still loading) — retry in 2 seconds
			cache.initialized = false;
			cache.rescanTimer = 2.0f;
		}
	} else {
		// Incremental update: validate existing positions, remove any that changed type.
		// Then do an outward rescan to replenish emptied type lists.
		std::unordered_set<std::string> needRescan;
		for (auto& [type, positions] : cache.known) {
			auto& vec = positions;
			vec.erase(std::remove_if(vec.begin(), vec.end(),
				[&](const glm::ivec3& p) { return getType(p.x, p.y, p.z) != type; }),
				vec.end());
			if (vec.empty()) needRescan.insert(type);
		}
		for (auto& g : needRescan) cache.known.erase(g);

		if (!needRescan.empty()) {
			int cx = (int)e.position.x, cy = (int)e.position.y, cz = (int)e.position.z;
			auto ring = buildXZRing(radius);
			scanBlocks(cx, cy, cz, ring, &needRescan);
		}
	}

	// Flatten to result list — cache entries per type are already in distance order
	// (outward scan fills them nearest-first), so final sort is cheap.
	std::vector<NearbyBlock> result;
	for (auto& [type, positions] : cache.known) {
		for (auto& pos : positions) {
			float dist = glm::length(glm::vec3(pos) - e.position);
			result.push_back({pos, type, dist});
		}
	}
	std::sort(result.begin(), result.end(), [](const NearbyBlock& a, const NearbyBlock& b) {
		return a.distance < b.distance;
	});
	return result;
}

} // namespace modcraft
