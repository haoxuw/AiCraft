#pragma once

#include "common/entity.h"
#include "common/constants.h"
#include "common/physics.h"
#include "common/behavior.h"
#include "common/action.h"
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>
#include <cmath>
#include <glm/trigonometric.hpp>

namespace agentworld {

class EntityManager {
public:
	// --- Type Registry ---

	void registerType(const EntityDef& def) {
		m_typeDefs[def.string_id] = def;
	}

	const EntityDef* getTypeDef(const std::string& typeId) const {
		auto it = m_typeDefs.find(typeId);
		return it != m_typeDefs.end() ? &it->second : nullptr;
	}

	// registerBuiltins() removed -- use builtin/builtin.h instead.

	// --- Instance Management ---

	EntityId spawn(const std::string& typeId, glm::vec3 pos) {
		auto* def = getTypeDef(typeId);
		if (!def) return ENTITY_NONE;
		EntityId id = m_nextId++;
		auto entity = std::make_unique<Entity>(id, typeId, *def);
		entity->position = pos;
		m_entities[id] = std::move(entity);
		return id;
	}

	EntityId spawn(const std::string& typeId, glm::vec3 pos,
	               const std::unordered_map<std::string, PropValue>& overrides) {
		EntityId id = spawn(typeId, pos);
		if (id == ENTITY_NONE) return id;
		auto& entity = m_entities[id];
		for (auto& [key, val] : overrides)
			entity->setProp(key, val);
		entity->clearDirty();
		return id;
	}

	Entity* get(EntityId id) {
		auto it = m_entities.find(id);
		return it != m_entities.end() ? it->second.get() : nullptr;
	}

	void remove(EntityId id) {
		auto it = m_entities.find(id);
		if (it != m_entities.end()) it->second->removed = true;
	}

	std::vector<Entity*> getInRadius(glm::vec3 center, float radius) {
		std::vector<Entity*> result;
		float r2 = radius * radius;
		for (auto& [id, e] : m_entities) {
			if (e->removed) continue;
			if (glm::dot(e->position - center, e->position - center) <= r2)
				result.push_back(e.get());
		}
		return result;
	}

	std::vector<Entity*> getByType(const std::string& typeId) {
		std::vector<Entity*> result;
		for (auto& [id, e] : m_entities) {
			if (!e->removed && e->typeId() == typeId) result.push_back(e.get());
		}
		return result;
	}

	// ---------------------------------------------------------------
	// Phase 3 (GATHER): AI behaviors decide and push ActionProposals.
	// Player input is handled separately in gameplay.cpp.
	// ---------------------------------------------------------------
	void gatherDecisions(float dt, ActionQueue& actions) {
		for (auto& [id, entity] : m_entities) {
			auto& e = *entity;
			const auto& def = e.def();

			// Behavior-driven AI (animals, future: NPCs)
			if (def.category == Category::Animal) {
				auto& state = m_behaviors[id];

				if (!state.behavior)
					state.behavior = createDefaultBehavior(e.typeId());

				// Decide at 4 Hz
				state.decideTimer -= dt;
				if (state.decideTimer <= 0) {
					state.decideTimer = 0.25f;
					BehaviorWorldView view{e, gatherNearby(e, 16.0f), dt};
					state.currentAction = state.behavior->decide(view);
				}

				// Convert behavior action to ActionProposal::Move
				// (the action executor will set velocity from this)
				behaviorToMoveProposal(e, state, state.currentAction, dt, actions);
			}
		}
	}

	// ---------------------------------------------------------------
	// Phase 1 (RESOLVE): Execute move proposals, then apply physics.
	// Called AFTER actions are resolved by the gameplay controller.
	// ---------------------------------------------------------------
	void stepPhysics(float dt, const BlockSolidFn& isSolid) {
		// Remove dead
		for (auto it = m_entities.begin(); it != m_entities.end(); ) {
			if (it->second->removed) {
				m_behaviors.erase(it->first);
				it = m_entities.erase(it);
			} else ++it;
		}

		for (auto& [id, entity] : m_entities) {
			auto& e = *entity;
			const auto& def = e.def();

			// Age
			if (e.hasProp(Prop::Age))
				e.setProp(Prop::Age, e.getProp<float>(Prop::Age) + dt);

			// Track walk distance for animation
			float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
			if (hSpeed > 0.01f) {
				float dist = e.getProp<float>(Prop::WalkDistance, 0.0f);
				e.setProp(Prop::WalkDistance, dist + hSpeed * dt);
			}

			// Unified physics: same collision for ALL entities
			MoveParams mp;
			mp.halfWidth = (def.collision_box_max.x - def.collision_box_min.x) * 0.5f;
			mp.height = def.collision_box_max.y - def.collision_box_min.y;
			mp.gravity = 20.0f * def.gravity_scale;
			mp.stepHeight = (def.category == Category::Animal || def.category == Category::Player) ? 1.0f : 0.0f;
			mp.canFly = e.getProp<bool>("fly_mode", false);
			mp.smoothStep = (def.category == Category::Animal);

			auto result = moveAndCollide(isSolid, e.position, e.velocity, dt, mp, e.onGround);
			e.position = result.position;
			e.velocity = result.velocity;
			e.onGround = result.onGround;

			// Item entity: bob offset + despawn
			if (e.typeId() == EntityType::ItemEntity) {
				float age = e.getProp<float>(Prop::Age);
				e.position.y += std::sin(age * 3.0f) * 0.003f;
				if (age > e.getProp<float>(Prop::DespawnTime, 300.0f))
					e.removed = true;
			}
		}
	}

	// Attract item entities toward a position. Returns items close enough to pick up.
	std::vector<Entity*> attractItemsToward(glm::vec3 pos, float attractRadius = 3.0f, float pickupRadius = 1.2f, float dt = 0.016f) {
		std::vector<Entity*> picked;
		for (auto& [id, e] : m_entities) {
			if (e->removed || e->typeId() != EntityType::ItemEntity) continue;
			float dist = glm::length(e->position - pos);
			if (dist < pickupRadius) {
				picked.push_back(e.get());
			} else if (dist < attractRadius) {
				glm::vec3 dir = glm::normalize(pos - e->position);
				float strength = 1.0f - (dist / attractRadius);
				e->velocity += dir * strength * 15.0f * dt;
			}
		}
		return picked;
	}

	void forEach(std::function<void(Entity&)> fn) {
		for (auto& [id, e] : m_entities)
			if (!e->removed) fn(*e);
	}

	size_t count() const { return m_entities.size(); }

	// --- Behavior access ---
	BehaviorState* getBehaviorState(EntityId id) {
		auto it = m_behaviors.find(id);
		return it != m_behaviors.end() ? &it->second : nullptr;
	}

private:
	// Convert a BehaviorAction to an ActionProposal::Move.
	// Computes velocity direction from action type, sets yaw locally (animation only).
	void behaviorToMoveProposal(Entity& e, BehaviorState& state,
	                             const BehaviorAction& action, float dt,
	                             ActionQueue& actions) {
		const float TURN_SPEED = 4.0f;
		auto smoothYaw = [&](float targetYaw) {
			float diff = targetYaw - e.yaw;
			while (diff > 180.0f) diff -= 360.0f;
			while (diff < -180.0f) diff += 360.0f;
			e.yaw += diff * std::min(dt * TURN_SPEED, 1.0f);
		};

		ActionProposal p;
		p.type = ActionProposal::Move;
		p.actorId = e.id();
		p.desiredVel = {0, 0, 0};

		switch (action.type) {
		case BehaviorAction::Idle:
			p.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
			break;

		case BehaviorAction::Wander: {
			if (action.param > 0.5f) {
				unsigned int seed = (unsigned int)(e.id() * 2654435761u +
					(unsigned int)(e.getProp<float>(Prop::Age, 0.0f) * 100));
				seed ^= seed >> 16;
				float r = (float)(seed & 0xFFFF) / 65535.0f;
				state.wanderYaw = e.yaw + (r - 0.5f) * 120.0f;
			}
			smoothYaw(state.wanderYaw);
			float rad = glm::radians(e.yaw);
			p.desiredVel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
			break;
		}

		case BehaviorAction::MoveTo: {
			glm::vec3 dir = action.targetPos - e.position;
			dir.y = 0;
			float dist = glm::length(dir);
			if (dist > 0.5f) {
				dir /= dist;
				smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
				float rad = glm::radians(e.yaw);
				p.desiredVel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
			}
			break;
		}

		case BehaviorAction::Follow: {
			glm::vec3 dir = action.targetPos - e.position;
			dir.y = 0;
			float dist = glm::length(dir);
			float minDist = std::max(action.param, 1.5f);
			if (dist > minDist) {
				dir /= dist;
				smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
				float rad = glm::radians(e.yaw);
				p.desiredVel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
			}
			break;
		}

		case BehaviorAction::Flee: {
			glm::vec3 dir = e.position - action.targetPos;
			dir.y = 0;
			float dist = glm::length(dir);
			if (dist > 0.1f) {
				dir /= dist;
				smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
			}
			float rad = glm::radians(e.yaw);
			p.desiredVel = {std::cos(rad) * action.speed, 0, std::sin(rad) * action.speed};
			break;
		}

		case BehaviorAction::LookAt: {
			glm::vec3 dir = action.targetPos - e.position;
			if (glm::length(glm::vec2(dir.x, dir.z)) > 0.01f)
				smoothYaw(glm::degrees(std::atan2(dir.z, dir.x)));
			p.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
			break;
		}

		case BehaviorAction::Attack:
			p.desiredVel = {e.velocity.x * 0.85f, 0, e.velocity.z * 0.85f};
			break;
		}

		actions.propose(p);
	}

	// Gather nearby entity info for a behavior's world view
	std::vector<NearbyEntity> gatherNearby(const Entity& self, float radius) {
		std::vector<NearbyEntity> result;
		float r2 = radius * radius;
		for (auto& [id, e] : m_entities) {
			if (e->removed || id == self.id()) continue;
			float d2 = glm::dot(e->position - self.position, e->position - self.position);
			if (d2 <= r2) {
				result.push_back({
					id, e->typeId(), e->def().category,
					e->position, std::sqrt(d2),
					e->getProp<int>(Prop::HP, e->def().max_hp)
				});
			}
		}
		return result;
	}

	std::unordered_map<std::string, EntityDef> m_typeDefs;
	std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
	std::unordered_map<EntityId, BehaviorState> m_behaviors;
	EntityId m_nextId = 1;
};

} // namespace agentworld
