#pragma once

/**
 * Action queue — the bridge between intent and world mutation.
 *
 * DESIGN PRINCIPLE: Objects NEVER directly modify world state.
 * They submit ActionProposals to the queue. The server validates
 * and executes them in Phase 1 of the step loop.
 *
 * All three control modes produce the same ActionProposals:
 *   1. First-Person (FPS) — keyboard/mouse → ActionProposal
 *   2. RTS (Warcraft-like) — click orders → ActionProposal
 *   3. Python Auto-Pilot — decide() → ActionProposal
 *
 * The server doesn't know or care which mode generated them.
 */

#include "shared/entity.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <mutex>

namespace aicraft {

// ================================================================
// ActionProposal — what an entity wants to happen
// ================================================================

struct ActionProposal {
	enum Type {
		// Movement
		Move,           // set desired velocity (desiredVel, jump, fly)
		// Block interaction
		BreakBlock,     // break block at blockPos
		PlaceBlock,     // place blockType at blockPos from slotIndex
		IgniteTNT,      // light TNT at blockPos
		// Entity interaction
		Attack,         // damage targetEntity
		PickupItem,     // pick up targetEntity (item)
		// Farming/active blocks
		GrowCrop,       // advance growth at blockPos
	};

	Type type = Move;
	EntityId actorId = ENTITY_NONE;

	// Movement
	glm::vec3 desiredVel = {0, 0, 0};
	bool jump = false;
	bool fly = false;
	float jumpVelocity = 17.0f;  // upward velocity when jumping

	// Block interaction
	glm::ivec3 blockPos = {0, 0, 0};
	std::string blockType;       // for PlaceBlock
	int slotIndex = 0;           // inventory slot

	// Entity interaction
	EntityId targetEntity = ENTITY_NONE;
	float damage = 0;
};

// ================================================================
// ActionEffect — side effects from executing an action
// (particles, sounds, chunk updates — for client rendering)
// ================================================================

struct ActionEffect {
	enum Type { BlockBreakParticles, ItemPickupParticles, ChunkDirty };
	Type type;
	glm::vec3 position;
	glm::vec3 color;
	glm::ivec3 blockPos;
	int count = 1;
};

// ================================================================
// ActionQueue — thread-safe proposal buffer
// ================================================================

class ActionQueue {
public:
	void propose(ActionProposal action) {
		m_pending.push_back(std::move(action));
	}

	// Drain all pending proposals. Returns them and clears the queue.
	std::vector<ActionProposal> drain() {
		std::vector<ActionProposal> result;
		result.swap(m_pending);
		return result;
	}

	bool empty() const { return m_pending.empty(); }
	size_t size() const { return m_pending.size(); }

private:
	std::vector<ActionProposal> m_pending;
};

} // namespace aicraft
