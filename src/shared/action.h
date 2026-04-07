#pragma once

/**
 * ActionProposalQueue — the bridge between intent and world mutation.
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

namespace modcraft {

// ================================================================
// ActionProposal — what an entity wants to happen
// ================================================================

struct ActionProposal {
	enum Type {
		Move,           // velocity-based move (any owned entity)
		Relocate,       // move item between inventories (no creation)
		ConvertObject,  // transform items (value must not increase; toItem="" = destroy)
		InteractBlock,  // toggle block state (door/button/TNT)
		ReloadBehavior, // infrastructure: hot-swap behavior source
	};

	Type type = Move;
	EntityId actorId = ENTITY_NONE;

	// Move: velocity set by client (behavior_executor computes from target pos)
	glm::vec3 desiredVel = {0, 0, 0};  // also: toss direction for Relocate+toGround
	bool jump = false;
	bool fly = false;
	float jumpVelocity = 17.0f;
	float lookPitch = 0.0f;
	float lookYaw = 0.0f;
	std::string goalText;

	// Relocate: move item between any two inventory references
	EntityId fromEntity = ENTITY_NONE;  // source entity (item entity, chest, etc.)
	EntityId toEntity = ENTITY_NONE;    // dest entity (ENTITY_NONE = actor's own inventory)
	bool toGround = false;              // true = spawn dropped item entity at feet
	std::string itemId;                 // item type to relocate
	int itemCount = 1;
	std::string equipSlot;              // non-empty = equip to this equipment slot

	// ConvertObject: transform items; toItem="" means destroy (value decreases → always ok)
	std::string fromItem;               // source item type or "hp"
	int fromCount = 1;
	std::string toItem;                 // dest item type or "hp" (empty = destroy)
	int toCount = 1;
	glm::ivec3 blockPos = {0, 0, 0};   // shared: block position for ConvertObject AND InteractBlock
	bool convertFromBlock = false;      // source is world block at blockPos
	bool convertToBlock = false;        // dest is world block at blockPos
	bool convertDirect = true;          // false = spawn result as dropped item on ground
	EntityId convertFromEntity = ENTITY_NONE; // act on another entity's inventory (e.g. attack: target's HP)

	// ReloadBehavior: hot-swap Python behavior source code
	std::string behaviorSource;
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
// ActionProposalQueue — thread-safe buffer of pending ActionProposals.
// Entities submit proposals here; the server drains and resolves them
// once per tick. Renamed from ActionQueue for clarity.
// ================================================================

class ActionProposalQueue {
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

	// Check if the queue already contains a Move proposal for this entity
	// (used to skip AI behavior when entity is player-commanded)
	bool hasMove(EntityId id) const {
		for (auto& p : m_pending)
			if (p.type == ActionProposal::Move && p.actorId == id) return true;
		return false;
	}

private:
	std::vector<ActionProposal> m_pending;
};

} // namespace modcraft
