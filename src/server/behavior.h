#pragma once

/**
 * Behavior system interface for living entities.
 *
 * Every living entity (player, animal, NPC) has a Behavior that drives
 * its actions. Behaviors implement decide() which is called at 4 Hz
 * and returns what the entity should do next.
 *
 * All behavior logic lives in Python files (artifacts/behaviors/).
 * PythonBehavior wraps the pybind11 bridge to call user-written .py files.
 * C++ only defines the interface types (BehaviorAction, BehaviorWorldView)
 * and the action executor (in entity_manager.h).
 */

#include "shared/entity.h"
#include "shared/constants.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <string>
#include <memory>
#include <cmath>
#include <algorithm>

namespace agentworld {

class EntityManager;

// ================================================================
// BehaviorAction -- what a behavior wants the entity to do
// ================================================================

struct BehaviorAction {
	enum Type {
		Idle,       // stand still
		Wander,     // random walk in current area
		MoveTo,     // walk toward targetPos
		LookAt,     // rotate to face targetPos
		Follow,     // move toward targetEntity
		Flee,       // move away from targetEntity/targetPos
		Attack,     // damage targetEntity (future)
		BreakBlock, // break block at targetPos (x,y,z)
	};

	Type type = Idle;
	glm::vec3 targetPos = {0, 0, 0};
	EntityId targetEntity = ENTITY_NONE;
	float speed = 2.0f;    // movement speed (multiplied by agility)
	float param = 0.0f;    // action-specific: damage, radius, duration
};

// ================================================================
// NearbyEntity -- info about an entity visible to a behavior
// ================================================================

struct NearbyEntity {
	EntityId id;
	std::string typeId;
	std::string category;
	glm::vec3 position;
	float distance;
	int hp;
};

// ================================================================
// BehaviorWorldView -- what a behavior can see
// ================================================================

// Block info visible to behaviors
struct NearbyBlock {
	glm::ivec3 pos;
	std::string typeId;
	float distance;
};

struct BehaviorWorldView {
	Entity& self;
	std::vector<NearbyEntity> nearbyEntities;
	std::vector<NearbyBlock> nearbyBlocks;
	float dt;
	float timeOfDay = 0.0f;

	// Find closest block matching a type
	const NearbyBlock* closestBlock(const std::string& type) const {
		const NearbyBlock* best = nullptr;
		for (auto& nb : nearbyBlocks)
			if (nb.typeId == type && (!best || nb.distance < best->distance))
				best = &nb;
		return best;
	}

	// Find closest entity matching a category
	const NearbyEntity* closestByCategory(const std::string& cat) const {
		const NearbyEntity* best = nullptr;
		for (auto& ne : nearbyEntities) {
			if (ne.category == cat && (!best || ne.distance < best->distance))
				best = &ne;
		}
		return best;
	}

	// Find all entities matching a category within a radius
	std::vector<const NearbyEntity*> allByCategory(const std::string& cat, float radius = 999.0f) const {
		std::vector<const NearbyEntity*> result;
		for (auto& ne : nearbyEntities)
			if (ne.category == cat && ne.distance <= radius)
				result.push_back(&ne);
		return result;
	}
};

// ================================================================
// Behavior -- abstract base class
// ================================================================

class Behavior {
public:
	virtual ~Behavior() = default;

	// Human-readable name of this behavior type
	virtual std::string name() const = 0;

	// Called at 4 Hz. Inspect the world, set self.goalText, return action.
	virtual BehaviorAction decide(BehaviorWorldView& view) = 0;

	// Python source code for display in editor.
	virtual std::string sourceCode() const { return "# No behavior loaded\n"; }
};

// ================================================================
// Per-entity behavior state
// ================================================================

struct BehaviorState {
	std::unique_ptr<Behavior> behavior;
	BehaviorAction currentAction;
	float decideTimer = 0.0f;
	float wanderYaw = 0.0f;     // used by action executor for smooth turning
};

// ================================================================
// IdleFallbackBehavior -- used when Python behavior fails to load
// ================================================================

class IdleFallbackBehavior : public Behavior {
public:
	std::string name() const override { return "Idle"; }

	BehaviorAction decide(BehaviorWorldView& view) override {
		view.self.goalText = "No behavior loaded";
		return {BehaviorAction::Idle};
	}
};

// ================================================================
// PythonBehavior -- wraps pybind11 bridge to run user-written Python
// ================================================================

class PythonBehavior : public Behavior {
public:
	PythonBehavior(int handle, const std::string& source)
		: m_handle(handle), m_source(source) {}

	std::string name() const override { return "Python"; }

	BehaviorAction decide(BehaviorWorldView& view) override;  // implemented in python_bridge.cpp

	std::string sourceCode() const override { return m_source; }

	int handle() const { return m_handle; }

private:
	int m_handle;
	std::string m_source;
};

} // namespace agentworld
