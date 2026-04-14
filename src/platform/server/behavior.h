#pragma once

/**
 * Behavior system — data types for entity AI.
 *
 * NEW ARCHITECTURE (Plan-based):
 *   Python decide(entity, local_world) → Plan (list of PlanStep)
 *   PlanStep types: Move(pos), Harvest(block_pos), Attack(entity_id),
 *                   Relocate(from, to, item)
 *
 * The old single-action BehaviorAction / Behavior class hierarchy / BehaviorWorldView
 * have been removed. AgentClient now calls Python decide() directly via
 * PythonBridge and gets back a Plan, which it executes step by step.
 *
 * Kept: NearbyEntity (used by gatherNearby), BlockSample (used by scan_blocks).
 */

#include "shared/action.h"
#include "shared/entity.h"
#include "shared/constants.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace civcraft {

// ================================================================
// NearbyEntity -- info about an entity visible to a behavior
// ================================================================

struct NearbyEntity {
	EntityId id;
	std::string typeId;
	EntityKind kind = EntityKind::Living;
	glm::vec3 position;
	float distance;
	int hp;
	std::vector<std::string> tags;  // feature tags from EntityDef (e.g. "humanoid")
};

// ================================================================
// BlockSample — a block position returned by scan_blocks()
// ================================================================

struct BlockSample {
	std::string typeId;
	int x, y, z;
	float distance;  // from the querying entity
};

// ================================================================
// PlanStep — one step in a Plan returned by Python decide()
// ================================================================

struct PlanStep {
	enum Type { Move, Harvest, Attack, Relocate };

	Type      type          = Move;
	glm::vec3 targetPos     = {0, 0, 0};     // Move/Harvest: world position
	EntityId  targetEntity  = ENTITY_NONE;    // Attack: entity to hit
	Container relocateFrom;                   // Relocate: source container
	Container relocateTo;                     // Relocate: destination container
	std::string itemId;                       // Relocate: item type
	int       itemCount     = 1;
	float     speed         = 2.0f;           // Move: walk speed

	static PlanStep move(glm::vec3 pos, float spd = 2.0f) {
		PlanStep s; s.type = Move; s.targetPos = pos; s.speed = spd; return s;
	}
	static PlanStep harvest(glm::vec3 pos) {
		PlanStep s; s.type = Harvest; s.targetPos = pos; return s;
	}
	static PlanStep attack(EntityId eid) {
		PlanStep s; s.type = Attack; s.targetEntity = eid; return s;
	}
	static PlanStep relocate(Container from, Container to,
	                         const std::string& item, int count = 1) {
		PlanStep s; s.type = Relocate;
		s.relocateFrom = from; s.relocateTo = to;
		s.itemId = item; s.itemCount = count; return s;
	}
};

using Plan = std::vector<PlanStep>;

} // namespace civcraft
