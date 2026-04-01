#pragma once

/**
 * Behavior system for living entities.
 *
 * Every living entity (player, animal, NPC) has a Behavior that drives
 * its actions. Behaviors implement decide() which is called at 4 Hz
 * and returns what the entity should do next.
 *
 * Currently uses C++ built-in behaviors. When pybind11 is connected,
 * PythonBehavior will call into user-written .py files.
 *
 * The behavior sets entity.goalText (visible via lightbulb tooltip)
 * and returns BehaviorActions that the executor converts to velocity/yaw.
 */

#include "shared/entity.h"
#include "shared/constants.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <functional>

namespace aicraft {

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

struct BehaviorWorldView {
	Entity& self;
	std::vector<NearbyEntity> nearbyEntities;
	float dt;
	float timeOfDay = 0.0f;

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

	// Python source code for display in editor. C++ behaviors return
	// a readable Python-style pseudocode equivalent.
	virtual std::string sourceCode() const { return "# Built-in behavior (C++)\n"; }
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
// Built-in: WanderBehavior -- animals roam, flee from players
// ================================================================

class WanderBehavior : public Behavior {
public:
	std::string name() const override { return "Wander"; }

	BehaviorAction decide(BehaviorWorldView& view) override {
		auto& e = view.self;
		float walkSpeed = e.def().walk_speed;

		// Check for nearby threats (players)
		auto* threat = view.closestByCategory(Category::Player);
		if (threat && threat->distance < 5.0f) {
			e.goalText = "Fleeing!";
			BehaviorAction a;
			a.type = BehaviorAction::Flee;
			a.targetPos = threat->position;
			a.speed = walkSpeed * 1.8f;
			return a;
		}

		// Try to group with same species
		std::vector<const NearbyEntity*> same;
		for (auto& ne : view.nearbyEntities)
			if (ne.typeId == e.typeId() && ne.id != e.id() && ne.distance < 12.0f)
				same.push_back(&ne);

		if (!same.empty() && same[0]->distance > 6.0f) {
			e.goalText = "Joining friends";
			BehaviorAction a;
			a.type = BehaviorAction::MoveTo;
			a.targetPos = same[0]->position;
			a.speed = walkSpeed;
			return a;
		}

		// Wander/rest cycle using timer stored in entity props
		float timer = e.getProp<float>(Prop::WanderTimer, 0.0f);
		timer -= view.dt * 4.0f; // timer ticks 4x per decide call period

		if (timer <= 0) {
			// Pick new timer: mostly walk, sometimes pause
			unsigned int seed = (unsigned int)(e.id() * 73856093 +
				(int)(e.getProp<float>(Prop::Age, 0.0f) * 10));
			seed = (seed << 13) ^ seed;
			float r = ((seed * (seed * seed * 15731 + 789221) + 1376312589) & 0x7fffffff)
			          / 2147483647.0f;
			timer = (r < 0.3f) ? (0.5f + r * 2.0f) : (2.0f + r * 3.0f);
			e.setProp(Prop::WanderTimer, timer);

			// Signal that we want a new random direction
			BehaviorAction a;
			a.type = BehaviorAction::Wander;
			a.speed = walkSpeed;
			a.param = 1.0f; // flag: pick new direction
			e.goalText = "Wandering";
			return a;
		}

		e.setProp(Prop::WanderTimer, timer);

		if (timer > 0.6f) {
			e.goalText = "Wandering";
			BehaviorAction a;
			a.type = BehaviorAction::Wander;
			a.speed = walkSpeed;
			a.param = 0.0f; // continue current direction
			return a;
		}

		e.goalText = "Resting";
		return {BehaviorAction::Idle};
	}

	std::string sourceCode() const override {
		return R"py("""Wander - default animal behavior.

Animals roam randomly, flee from nearby players,
and group with others of the same species.
"""
import random
from aicraft.api import Wander, Flee, MoveTo, Idle

goal = "Wandering"

def decide(self, world):
    # Flee from players within 5 blocks
    players = world.get_entities_in_radius(
        self.pos, 5.0, category="player")
    if players:
        closest = min(players, key=lambda e: e.distance)
        self.goal = "Fleeing!"
        return Flee(closest.id, speed=self.walk_speed * 1.8)

    # Group with same species if too far apart
    friends = world.get_entities_in_radius(
        self.pos, 12.0, type=self.type_id)
    friends = [f for f in friends if f.id != self.id]
    if friends and friends[0].distance > 6.0:
        self.goal = "Joining friends"
        return MoveTo(friends[0].pos, speed=self.walk_speed)

    # Random wander/rest cycle
    if random.random() < 0.3:
        self.goal = "Resting"
        return Idle(duration=1.0 + random.random() * 2.0)

    self.goal = "Wandering"
    return Wander(speed=self.walk_speed)
)py";
	}
};

// ================================================================
// Built-in: IdleBehavior -- stand still, look around
// ================================================================

class IdleBehavior : public Behavior {
public:
	std::string name() const override { return "Idle"; }

	BehaviorAction decide(BehaviorWorldView& view) override {
		view.self.goalText = "Idle";
		return {BehaviorAction::Idle};
	}

	std::string sourceCode() const override {
		return R"py("""Idle - stand still and do nothing.

The simplest possible behavior. A good starting
point for writing your own!
"""
from aicraft.api import Idle

goal = "Idle"

def decide(self, world):
    self.goal = "Idle"
    return Idle()
)py";
	}
};

// ================================================================
// Built-in: PeckBehavior -- chicken-specific pecking + scatter
// ================================================================

class PeckBehavior : public Behavior {
public:
	std::string name() const override { return "Peck"; }

	BehaviorAction decide(BehaviorWorldView& view) override {
		auto& e = view.self;
		float walkSpeed = e.def().walk_speed;

		// Scatter from players (chickens are more skittish)
		auto* threat = view.closestByCategory(Category::Player);
		if (threat && threat->distance < 3.5f) {
			e.goalText = "Scattering!";
			BehaviorAction a;
			a.type = BehaviorAction::Flee;
			a.targetPos = threat->position;
			a.speed = walkSpeed * 2.0f;
			return a;
		}

		// Peck cycle: short walks, frequent stops
		float timer = e.getProp<float>(Prop::WanderTimer, 0.0f);
		timer -= view.dt * 4.0f;

		if (timer <= 0) {
			unsigned int seed = (unsigned int)(e.id() * 48271 +
				(int)(e.getProp<float>(Prop::Age, 0.0f) * 7));
			seed = (seed << 13) ^ seed;
			float r = ((seed * (seed * seed * 15731 + 789221) + 1376312589) & 0x7fffffff)
			          / 2147483647.0f;
			// Chickens: lots of short pauses (pecking), brief walks
			timer = (r < 0.5f) ? (0.3f + r * 0.8f) : (0.8f + r * 1.5f);
			e.setProp(Prop::WanderTimer, timer);

			BehaviorAction a;
			a.type = BehaviorAction::Wander;
			a.speed = walkSpeed;
			a.param = 1.0f;
			e.goalText = "Pecking at ground";
			return a;
		}

		e.setProp(Prop::WanderTimer, timer);

		if (timer > 0.4f) {
			e.goalText = "Pecking at ground";
			BehaviorAction a;
			a.type = BehaviorAction::Wander;
			a.speed = walkSpeed * 0.7f; // slower, pecking pace
			return a;
		}

		e.goalText = "Looking around";
		return {BehaviorAction::Idle};
	}

	std::string sourceCode() const override {
		return R"py("""Peck - chicken behavior.

Chickens peck at the ground with frequent short pauses,
and scatter quickly when players get close.
"""
import random
from aicraft.api import Wander, Flee, Idle

goal = "Pecking at ground"

def decide(self, world):
    # Scatter from players (chickens are skittish!)
    players = world.get_entities_in_radius(
        self.pos, 3.5, category="player")
    if players:
        closest = min(players, key=lambda e: e.distance)
        self.goal = "Scattering!"
        return Flee(closest.id, speed=self.walk_speed * 2.0)

    # Short peck-walk-pause cycle
    if random.random() < 0.5:
        self.goal = "Looking around"
        return Idle(duration=0.3 + random.random() * 0.8)

    self.goal = "Pecking at ground"
    return Wander(speed=self.walk_speed * 0.7)
)py";
	}
};

// ================================================================
// PythonBehavior — wraps pybind11 bridge to run user-written Python
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

// ================================================================
// Factory: create default behavior for an entity category/type
// ================================================================

inline std::unique_ptr<Behavior> createDefaultBehavior(const std::string& typeId) {
	if (typeId == EntityType::Chicken)
		return std::make_unique<PeckBehavior>();
	if (typeId == EntityType::Pig)
		return std::make_unique<WanderBehavior>();
	// Default for unknown living entities
	return std::make_unique<IdleBehavior>();
}

// ================================================================
// Action executor -- converts BehaviorAction → entity velocity/yaw
// ================================================================

inline void executeBehaviorAction(Entity& e, BehaviorState& state,
                                   const BehaviorAction& action,
                                   const EntityManager& /*entities*/,
                                   float dt) {
	const float TURN_SPEED = 4.0f; // rad/s max yaw change

	auto smoothYaw = [&](float targetYaw) {
		float diff = targetYaw - e.yaw;
		while (diff > 180.0f) diff -= 360.0f;
		while (diff < -180.0f) diff += 360.0f;
		e.yaw += diff * std::min(dt * TURN_SPEED, 1.0f);
	};

	switch (action.type) {

	case BehaviorAction::Idle:
		e.velocity.x *= 0.85f;
		e.velocity.z *= 0.85f;
		break;

	case BehaviorAction::Wander: {
		if (action.param > 0.5f) {
			// Pick new random direction
			unsigned int seed = (unsigned int)(e.id() * 2654435761u +
				(unsigned int)(e.getProp<float>(Prop::Age, 0.0f) * 100));
			seed ^= seed >> 16;
			float r = (float)(seed & 0xFFFF) / 65535.0f;
			state.wanderYaw = e.yaw + (r - 0.5f) * 120.0f;
		}

		smoothYaw(state.wanderYaw);

		float rad = glm::radians(e.yaw);
		e.velocity.x = std::cos(rad) * action.speed;
		e.velocity.z = std::sin(rad) * action.speed;
		break;
	}

	case BehaviorAction::MoveTo: {
		glm::vec3 dir = action.targetPos - e.position;
		dir.y = 0;
		float dist = glm::length(dir);
		if (dist > 0.5f) {
			dir /= dist;
			float targetYaw = glm::degrees(std::atan2(dir.z, dir.x));
			smoothYaw(targetYaw);

			float rad = glm::radians(e.yaw);
			e.velocity.x = std::cos(rad) * action.speed;
			e.velocity.z = std::sin(rad) * action.speed;
		} else {
			e.velocity.x *= 0.85f;
			e.velocity.z *= 0.85f;
		}
		break;
	}

	case BehaviorAction::LookAt: {
		glm::vec3 dir = action.targetPos - e.position;
		if (glm::length(glm::vec2(dir.x, dir.z)) > 0.01f) {
			float targetYaw = glm::degrees(std::atan2(dir.z, dir.x));
			smoothYaw(targetYaw);
		}
		e.velocity.x *= 0.85f;
		e.velocity.z *= 0.85f;
		break;
	}

	case BehaviorAction::Follow: {
		// Same as MoveTo but keeps minimum distance
		glm::vec3 dir = action.targetPos - e.position;
		dir.y = 0;
		float dist = glm::length(dir);
		float minDist = std::max(action.param, 1.5f);
		if (dist > minDist) {
			dir /= dist;
			float targetYaw = glm::degrees(std::atan2(dir.z, dir.x));
			smoothYaw(targetYaw);
			float rad = glm::radians(e.yaw);
			e.velocity.x = std::cos(rad) * action.speed;
			e.velocity.z = std::sin(rad) * action.speed;
		} else {
			e.velocity.x *= 0.85f;
			e.velocity.z *= 0.85f;
		}
		break;
	}

	case BehaviorAction::Flee: {
		glm::vec3 dir = e.position - action.targetPos;
		dir.y = 0;
		float dist = glm::length(dir);
		if (dist > 0.1f) {
			dir /= dist;
			float targetYaw = glm::degrees(std::atan2(dir.z, dir.x));
			smoothYaw(targetYaw);
		}
		float rad = glm::radians(e.yaw);
		e.velocity.x = std::cos(rad) * action.speed;
		e.velocity.z = std::sin(rad) * action.speed;
		break;
	}

	case BehaviorAction::Attack:
		// Future: deal damage to target entity
		e.velocity.x *= 0.85f;
		e.velocity.z *= 0.85f;
		break;
	}
}

} // namespace aicraft
