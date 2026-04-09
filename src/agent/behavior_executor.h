#pragma once

/**
 * BehaviorExecutor — converts BehaviorAction to ActionProposal.
 *
 * Extracted from EntityManager::behaviorToMoveProposal().
 * Used by the agent client to translate Python AI decisions into
 * server-compatible ActionProposals sent over TCP.
 *
 * Also contains gatherNearby() for entity awareness.
 * Block awareness is provided via ChunkInfo (see docs/29_CHUNK_INFO.md).
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

// Block query function: returns block type string at world position.
// Used by Python pathfinding (get_block()) — queries agent's local chunk cache.
using BlockTypeFn = std::function<std::string(int, int, int)>;

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

	// Always attach goalText to Move proposals so it reaches the server
	auto makeMove = [&](glm::vec3 vel) -> ActionProposal {
		ActionProposal p;
		p.type = ActionProposal::Move;
		p.actorId = e.id();
		p.desiredVel = vel;
		p.goalText = e.goalText;
		return p;
	};

	switch (action.type) {
	case BehaviorAction::Idle:
		// No proposal — entity keeps its last velocity. This prevents the agent
		// from fighting GUI WASD control with zero-velocity stop actions.
		// goalText is still propagated because the Move actions sent between
		// decide() calls (line 150-158 in agent_client.h) carry it.
		break;
	case BehaviorAction::Move: {
		glm::vec3 dir = action.targetPos - e.position;
		dir.y = 0;
		float dist = glm::length(dir);
		glm::vec3 vel = {0, 0, 0};
		if (dist > 0.5f) {
			dir /= dist;
			smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
			float rad = glm::radians(e.yaw);
			vel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
		}
		auto p = makeMove(vel);
		out.push_back(p);
		break;
	}

	case BehaviorAction::Relocate: {
		// One-shot: stop movement, then relocate
		out.push_back(makeMove({0, 0, 0}));

		ActionProposal p;
		p.type         = ActionProposal::Relocate;
		p.actorId      = e.id();
		p.relocateFrom = action.relocateFrom;
		p.relocateTo   = action.relocateTo;
		p.itemId       = action.itemId;
		p.itemCount    = action.itemCount;
		p.equipSlot    = action.equipSlot;
		out.push_back(p);
		break;
	}

	case BehaviorAction::Convert: {
		out.push_back(makeMove({0, 0, 0}));

		ActionProposal p;
		p.type        = ActionProposal::Convert;
		p.actorId     = e.id();
		p.fromItem    = action.fromItem;
		p.fromCount   = action.fromCount;
		p.toItem      = action.toItem;
		p.toCount     = action.toCount;
		p.convertFrom = action.convertFrom;
		p.convertInto = action.convertInto;
		out.push_back(p);
		break;
	}

	case BehaviorAction::Interact: {
		out.push_back(makeMove({0, 0, 0}));

		ActionProposal p;
		p.type     = ActionProposal::Interact;
		p.actorId  = e.id();
		p.blockPos = action.blockPos;
		out.push_back(p);
		break;
	}
	} // end switch
}

// Gather nearby entity info from the agent's local entity cache.
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
		result.push_back(ne);
	}
	return result;
}

// Block scanning (getKnownBlocks, BlockCache, kIgnoredBlockTypes) removed.
// Block awareness is now provided by ChunkInfo — see docs/29_CHUNK_INFO.md.

} // namespace modcraft
