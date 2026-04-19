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

	// Commit duration requested by the behavior. Semantics per step type:
	//   Move, idle-hold (target==self): success at progress >= holdTime
	//   Move, travel:                   success on arrival OR progress >= holdTime
	//   Harvest / Attack / Relocate:    ignored (natural success criteria)
	// holdTime == 0 means "use evaluator default" (see agent_client.h).
	float     holdTime      = 0.0f;

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
