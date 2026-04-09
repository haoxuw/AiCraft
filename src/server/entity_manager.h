#pragma once

/**
 * EntityManager — entity lifecycle, type registry, and physics.
 *
 * Behavior/AI code has been moved to the agent client process (src/agent/).
 * This class is now Python-free: no behavior loading, no decision gathering.
 * Entity AI arrives as ActionProposals from agent client processes over TCP.
 */

#include "shared/entity.h"
#include "shared/constants.h"
#include "shared/physics.h"
#include "server/server_tuning.h"
#include "shared/action.h"
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <glm/trigonometric.hpp>

namespace modcraft {

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

	// --- Physics ---

	void stepPhysics(float dt, const BlockSolidFn& isSolid) {
		// Remove dead
		for (auto it = m_entities.begin(); it != m_entities.end(); ) {
			if (it->second->removed) {
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

			// Skip physics for entities whose position was set by clientPos this tick.
			// The client already ran moveAndCollide — running it again would double
			// gravity, causing jumps to fail and movement to feel sluggish.
			if (e.skipPhysics) {
				e.skipPhysics = false;
			} else {
				MoveParams mp = makeMoveParams(def.collision_box_min, def.collision_box_max,
					def.gravity_scale, def.isLiving(), e.getProp<bool>("fly_mode", false));

				auto result = moveAndCollide(isSolid, e.position, e.velocity, dt, mp, e.onGround);
				e.position = result.position;
				e.velocity = result.velocity;
				e.onGround = result.onGround;
			}

			// Item entity: freeze on ground (no bouncing), despawn after timeout
			if (e.typeId() == ItemName::ItemEntity) {
				if (e.onGround) {
					e.velocity = {0, 0, 0}; // stop moving once landed
				}
				float age = e.getProp<float>(Prop::Age);
				if (age > e.getProp<float>(Prop::DespawnTime, 300.0f))
					e.removed = true;
			}
		}
	}

	// Item pickup is client-initiated via ActionProposal::PickupItem.
	// NPC pickup is handled by Python behavior code sending the same action.

	void forEach(std::function<void(Entity&)> fn) {
		for (auto& [id, e] : m_entities)
			if (!e->removed) fn(*e);
	}

	void forEachIncludingRemoved(std::function<void(Entity&)> fn) {
		for (auto& [id, e] : m_entities)
			fn(*e);
	}

	size_t count() const { return m_entities.size(); }

private:
	std::unordered_map<std::string, EntityDef> m_typeDefs;
	std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
	EntityId m_nextId = 1;
};

} // namespace modcraft
