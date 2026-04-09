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

#include "shared/action.h"
#include "shared/entity.h"
#include "shared/constants.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <cmath>
#include <algorithm>

namespace modcraft {

class EntityManager;

// ================================================================
// BehaviorAction -- what a behavior wants the entity to do
// ================================================================

struct BehaviorAction {
	enum Type {
		Idle,      // do nothing — agent sends no action this tick
		Move,      // walk toward targetPos
		Relocate,  // move item between containers
		Convert,   // transform item (toItem="" = destroy; value must not increase)
		Interact,  // toggle block state (door/button/TNT)
	};

	Type      type      = Idle;
	glm::vec3 targetPos = {0, 0, 0};  // for Move: destination
	float     speed     = 2.0f;

	// Relocate
	Container   relocateFrom;
	Container   relocateTo;
	std::string itemId;
	int         itemCount  = 1;
	std::string equipSlot;

	// Convert
	std::string fromItem;
	int         fromCount  = 1;
	std::string toItem;                // empty = destroy (value decreases, always ok)
	int         toCount    = 1;
	Container   convertFrom;           // where source is taken from (default = Self)
	Container   convertInto;           // where result is placed (default = Self)

	// Interact (reuses no extra fields — block pos is in convertFrom for Convert,
	// or specified explicitly via blockPos below)
	glm::ivec3  blockPos   = {0, 0, 0};
};

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
};

// ================================================================
// BlockSample — a block position from ChunkInfo, passed to behaviors
// ================================================================

struct BlockSample {
	std::string typeId;
	int x, y, z;
	float distance;  // from the querying entity
};

// ================================================================
// BehaviorWorldView -- what a behavior can see
// ================================================================

struct BehaviorWorldView {
	Entity& self;
	std::vector<NearbyEntity> nearbyEntities;
	std::vector<BlockSample> chunkBlocks;
	float dt;
	float timeOfDay = 0.0f;
	// Block query function — maps (x,y,z) → block type string.
	std::function<std::string(int,int,int)> blockQueryFn;
	// Block scan function — targeted search by type ID from real chunk data.
	std::function<std::vector<BlockSample>(const std::string&, float, int)> scanBlocksFn;

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
	float wanderYaw = 0.0f;  // kept for smooth turning in MoveTo
};

// ================================================================
// IdleFallbackBehavior -- used when Python behavior fails to load
// ================================================================

class IdleFallbackBehavior : public Behavior {
public:
	std::string name() const override { return "Idle"; }

	BehaviorAction decide(BehaviorWorldView& view) override {
		view.self.goalText = "No behavior loaded";
		BehaviorAction a;
		a.type      = BehaviorAction::Move;
		a.targetPos = view.self.position;  // move to current pos = stand still
		return a;
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

} // namespace modcraft
