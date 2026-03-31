#pragma once

#include "common/entity.h"
#include "common/constants.h"
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>
#include <cmath>
#include <glm/trigonometric.hpp>

namespace aicraft {

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

	// BlockQuery: callback to check if block at (x,y,z) is solid.
	using BlockQuery = std::function<bool(int x, int y, int z)>;

	void step(float dt, BlockQuery isSolid = nullptr) {
		// Remove dead
		for (auto it = m_entities.begin(); it != m_entities.end(); ) {
			if (it->second->removed) it = m_entities.erase(it);
			else ++it;
		}

		for (auto& [id, entity] : m_entities) {
			auto& e = *entity;
			const auto& def = e.def();

			// Age
			if (e.hasProp(Prop::Age))
				e.setProp(Prop::Age, e.getProp<float>(Prop::Age) + dt);

			// --- Animal wander AI ---
			if (def.category == Category::Animal) {
				float timer = e.getProp<float>(Prop::WanderTimer, 0.0f);
				float targetYaw = e.getProp<float>(Prop::WanderYaw, e.yaw);
				timer -= dt;

				if (timer <= 0) {
					unsigned int seed = (unsigned int)(id * 73856093 +
						(int)(e.getProp<float>(Prop::Age) * 10));
					seed = (seed << 13) ^ seed;
					float r1 = ((seed*(seed*seed*15731+789221)+1376312589)&0x7fffffff)/2147483647.0f;
					seed = seed * 1103515245 + 12345;
					float r2 = ((seed*(seed*seed*15731+789221)+1376312589)&0x7fffffff)/2147483647.0f;

					// Mostly small turns (±60°), occasionally larger ones
					if (r2 < 0.15f) {
						// Big direction change
						targetYaw = r1 * 360.0f;
					} else {
						// Small turn from current facing
						targetYaw = e.yaw + (r1 - 0.5f) * 120.0f;
					}
					e.setProp(Prop::WanderYaw, targetYaw);

					// Varied pacing: walk, pause, walk longer
					if (r2 < 0.25f)
						timer = 0.8f + r1 * 1.5f;  // short pause
					else
						timer = 2.0f + r2 * 4.0f;   // walk for a while
				}
				e.setProp(Prop::WanderTimer, timer);

				if (timer > 0.6f) {
					// Smooth yaw turning (animals don't snap direction)
					float diff = targetYaw - e.yaw;
					while (diff > 180.0f) diff -= 360.0f;
					while (diff < -180.0f) diff += 360.0f;
					e.yaw += diff * std::min(dt * 4.0f, 1.0f);

					// Walk forward in facing direction
					float rad = glm::radians(e.yaw);
					e.velocity.x = std::cos(rad) * def.walk_speed;
					e.velocity.z = std::sin(rad) * def.walk_speed;
				} else {
					// Pausing: slow to a stop, gentle head turn
					e.velocity.x *= 0.85f;
					e.velocity.z *= 0.85f;
				}
			}

			// Gravity
			if (def.gravity_scale > 0)
				e.velocity.y -= 20.0f * def.gravity_scale * dt;

			// Track walk distance for animation
			float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
			if (hSpeed > 0.01f) {
				float dist = e.getProp<float>(Prop::WalkDistance, 0.0f);
				e.setProp(Prop::WalkDistance, dist + hSpeed * dt);
			}

			// Move + ground collision
			e.position += e.velocity * dt;
			if (isSolid) {
				int bx = (int)std::floor(e.position.x);
				int bz = (int)std::floor(e.position.z);
				int by = (int)std::floor(e.position.y);
				for (int y = by; y > by - 5; y--) {
					if (isSolid(bx, y, bz)) {
						float ground = (float)(y + 1);
						if (e.position.y < ground) {
							e.position.y = ground;
							e.velocity.y = 0;
						}
						break;
					}
				}
			}

			// Item entity: bob animation + attraction to player + despawn
			if (e.typeId() == EntityType::ItemEntity) {
				// Gentle hover bob
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

private:
	std::unordered_map<std::string, EntityDef> m_typeDefs;
	std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
	EntityId m_nextId = 1;
};

} // namespace aicraft
