#pragma once

// Plan-based AI data types. Python decide() returns Plan (vector<PlanStep>);
// AgentClient executes step by step. Also NearbyEntity + BlockSample scan results.

#include "logic/action.h"
#include "logic/entity.h"
#include "logic/constants.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace civcraft {

struct NearbyEntity {
	EntityId id;
	std::string typeId;
	EntityKind kind = EntityKind::Living;
	glm::vec3 position;
	float distance;
	int hp;
	std::vector<std::string> tags;  // EntityDef feature tags (e.g. "humanoid")
};

struct BlockSample {
	std::string typeId;
	int x, y, z;
	float distance;
};

struct PlanStep {
	enum Type { Move, Harvest, Attack, Relocate, Interact };

	Type      type          = Move;
	glm::vec3 targetPos     = {0, 0, 0};
	EntityId  targetEntity  = ENTITY_NONE;
	Container relocateFrom;
	Container relocateTo;
	std::string itemId;
	int       itemCount     = 1;
	float     speed         = 2.0f;

	// Interact: world-space block to mutate. appearanceIdx >= 0 writes the
	// palette entry at targetPos without changing block type (I3). -1 means
	// legacy "toggle" (door/TNT); see docs/22_APPEARANCE.md.
	int16_t   appearanceIdx = -1;

	// Move: client-side Execute() target. applyMove re-reads live target pos
	// each tick (chase inside keepWithin / scatter past keepAway). Client-only
	// — never serialized onto ActionProposal (Rule 4).
	EntityId  anchorEntityId = ENTITY_NONE;
	float     keepWithin     = 0.0f;  // chase stop ring
	float     keepAway       = 0.0f;  // flee stop ring

	// Commit duration requested by the behavior. Semantics per step type:
	//   Move, idle-hold (target==self): success at progress >= holdTime
	//   Move, travel:                   success on arrival OR progress >= holdTime
	//   Harvest / Attack / Relocate:    ignored (natural success criteria)
	// holdTime == 0 means "use evaluator default" (see agent_client.h).
	float     holdTime      = 0.0f;

	// Harvest: priority-ordered list of block string_ids the villager is
	// willing to gather (index 0 = highest). Executor walks to targetPos,
	// then every chop tick scans a gatherRadius sphere around the entity
	// and chops the nearest hit of the highest-priority type still present.
	// When no block of any type exists in range, the step concludes Success
	// and decide() runs again (re-scans for higher tiers in the world).
	// This makes the *executor* the authority on "don't chop a lower-tier
	// block while a higher-tier one exists nearby" — Python only declares
	// the ordering, never enumerates the individual blocks.
	std::vector<std::string> gatherTypes;
	float     gatherRadius   = 6.0f;

	// Harvest: seconds between swings. 0 → evaluator uses entity default.
	// Lives on the step (not the agent) so different gather steps in the
	// same plan can pace differently (fast leaves, slow stone).
	float     chopCooldown   = 0.0f;

	static PlanStep move(glm::vec3 pos, float spd = 2.0f, float hold = 0.0f) {
		PlanStep s; s.type = Move; s.targetPos = pos; s.speed = spd;
		s.holdTime = hold; return s;
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
	static PlanStep interact(glm::vec3 pos, int16_t appearanceIdx = -1) {
		PlanStep s; s.type = Interact; s.targetPos = pos;
		s.appearanceIdx = appearanceIdx; return s;
	}
};

using Plan = std::vector<PlanStep>;

} // namespace civcraft
