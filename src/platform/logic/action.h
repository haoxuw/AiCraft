#pragma once

// Rule 3: Objects NEVER mutate world directly — submit ActionProposals, server resolves.
// FPS input, RTS clicks, and Python decide() all produce the same proposals.

#include "logic/entity.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <mutex>

namespace civcraft {

// Container: any item-holding storage (entity inventory, block, ground, self).
// Used by Relocate (from/to) and Convert (source/destination).
struct Container {
	enum class Kind : uint8_t {
		Self   = 0,  // actor's own inventory (default)
		Ground = 1,  // world ground — spawn or collect dropped items
		Entity = 2,  // another entity's inventory (by entityId)
		Block  = 3,  // world block at pos (mine source or placement target)
	};

	Kind       kind     = Kind::Self;
	EntityId   entityId = ENTITY_NONE;
	glm::ivec3 pos      = {0, 0, 0};

	static Container self()                             { return {}; }
	static Container ground()                           { Container c; c.kind = Kind::Ground; return c; }
	static Container entity(EntityId id)                { Container c; c.kind = Kind::Entity; c.entityId = id; return c; }
	static Container block(glm::ivec3 p)                { Container c; c.kind = Kind::Block;  c.pos = p; return c; }
	static Container block(int x, int y, int z)         { return block({x, y, z}); }
};

// ActionProposal — what an entity wants to happen (Rule 0: exactly 4 types).
struct ActionProposal {
	enum Type {
		Move,      // velocity-based move (any owned entity)
		Relocate,  // move item between containers (value conserved, no creation)
		Convert,   // transform item from one type to another (value must not increase)
		Interact,  // toggle interactive block state (door, button, TNT)
	};

	Type     type    = Move;
	EntityId actorId = ENTITY_NONE;

	// Move: velocity set by client (behavior_executor computes from target pos)
	glm::vec3 desiredVel  = {0, 0, 0};  // also: toss direction for Relocate with Ground dest
	bool      jump        = false;
	bool      sprint      = false;
	bool      fly         = false;
	float     jumpVelocity= 17.0f;
	float     lookPitch   = 0.0f;  // for chunk streaming view bias (vertical)
	float     lookYaw     = 0.0f;  // for chunk streaming view bias (horizontal, when standing still)
	std::string goalText;
	// Server accepts as authoritative if within CLIENT_POS_TOLERANCE; else snaps back. Move only.
	glm::vec3 clientPos    = {0, 0, 0};
	bool      hasClientPos = false;

	// Relocate: move item from one container to another (no creation)
	Container   relocateFrom;            // source container (default = Self)
	Container   relocateTo;              // destination container (default = Self)
	std::string itemId;                  // item type to relocate
	int         itemCount  = 1;
	std::string equipSlot;               // non-empty = equip to this slot

	// Convert: transform fromItem → toItem (value must not increase; toItem="" = destroy)
	std::string fromItem;                // source item type or "hp"
	int         fromCount  = 1;
	std::string toItem;                  // result item type or "hp" (empty = destroy)
	int         toCount    = 1;
	Container   convertFrom;             // where source is taken from (default = Self)
	Container   convertInto;             // where result is placed (default = Self)

	// Interact: toggle block state (door/button/TNT)
	glm::ivec3  blockPos   = {0, 0, 0};

	// Hot-reload: non-empty = Python behavior swap (control message, not a game action).
	std::string behaviorSource;
};

// ActionEffect — client-side rendering side-effects (particles, chunk dirty).
struct ActionEffect {
	enum Type { BlockBreakParticles, ItemPickupParticles, ChunkDirty };
	Type type;
	glm::vec3 position;
	glm::vec3 color;
	glm::ivec3 blockPos;
	int count = 1;
};

// Thread-safe buffer; server drains + resolves once per tick.
class ActionProposalQueue {
public:
	void propose(ActionProposal action) {
		m_pending.push_back(std::move(action));
	}

	std::vector<ActionProposal> drain() {
		std::vector<ActionProposal> result;
		result.swap(m_pending);
		return result;
	}

	bool empty() const { return m_pending.empty(); }
	size_t size() const { return m_pending.size(); }

	// Skip AI decide() if player-command already queued a Move for this entity.
	bool hasMove(EntityId id) const {
		for (auto& p : m_pending)
			if (p.type == ActionProposal::Move && p.actorId == id) return true;
		return false;
	}

private:
	std::vector<ActionProposal> m_pending;
};

} // namespace civcraft
