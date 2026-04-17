#pragma once

// Entity lifecycle, type registry, physics. AI lives on agent clients; this is Python-free.

#include "logic/entity.h"
#include "logic/constants.h"
#include "logic/physics.h"
#include "logic/entity_physics.h"
#include "debug/move_stuck_log.h"
#include "server/server_tuning.h"
#include "logic/action.h"
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <glm/trigonometric.hpp>

namespace civcraft {

class EntityManager {
public:
	void registerType(const EntityDef& def) {
		m_typeDefs[def.string_id] = def;
	}

	const EntityDef* getTypeDef(const std::string& typeId) const {
		auto it = m_typeDefs.find(typeId);
		return it != m_typeDefs.end() ? &it->second : nullptr;
	}

	template <class Fn>
	void forEachDef(Fn&& fn) const {
		for (auto& [id, def] : m_typeDefs) fn(id, def);
	}

	// Overlay Python-sourced stats on top of C++-registered EntityDefs.
	// Duck-typed: Stats must expose `id`, `walk_speed`, `run_speed`, `eye_height`,
	// `gravity`, `has_box`, `box_min_{x,y,z}`, `box_max_{x,y,z}`.
	// NaN scalars mean "keep builtin default". Avoids coupling this header to
	// ArtifactRegistry (which lives in logic/).
	template <class Stats>
	void applyLivingStats(const std::vector<Stats>& stats) {
		int overridden = 0;
		for (auto& s : stats) {
			auto it = m_typeDefs.find(s.id);
			if (it == m_typeDefs.end()) continue;
			EntityDef& d = it->second;
			bool any = false;
			auto setIfFinite = [&](float v, float& dst) {
				if (std::isfinite(v)) { dst = v; any = true; }
			};
			setIfFinite(s.walk_speed, d.walk_speed);
			setIfFinite(s.run_speed,  d.run_speed);
			setIfFinite(s.eye_height, d.eye_height);
			setIfFinite(s.gravity,    d.gravity_scale);
			if (s.has_box) {
				d.collision_box_min = {s.box_min_x, s.box_min_y, s.box_min_z};
				d.collision_box_max = {s.box_max_x, s.box_max_y, s.box_max_z};
				any = true;
			}
			if (any) overridden++;
		}
		if (overridden > 0)
			printf("[EntityManager] Applied Python stats to %d entity types\n", overridden);
	}

	// Call after registerAllBuiltins() + ArtifactRegistry::loadAll().
	void mergeArtifactTags(const std::vector<std::pair<std::string, std::vector<std::string>>>& tagsByType) {
		int merged = 0;
		for (auto& [typeId, tags] : tagsByType) {
			auto it = m_typeDefs.find(typeId);
			if (it != m_typeDefs.end()) {
				it->second.tags = tags;
				merged++;
			}
		}
		// Auto-tag every playable living with "playable" so AI behaviors can
		// target "any character a user might be controlling" without hardcoding
		// a "player" type id (which no longer exists).
		for (auto& [id, def] : m_typeDefs) {
			if (def.isLiving() && def.playable &&
			    std::find(def.tags.begin(), def.tags.end(), "playable") == def.tags.end()) {
				def.tags.push_back("playable");
			}
		}
		if (merged > 0)
			printf("[EntityManager] Merged feature tags for %d entity types\n", merged);
	}

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

	void stepPhysics(float dt, const BlockSolidFn& isSolid) {
		for (auto it = m_entities.begin(); it != m_entities.end(); ) {
			if (it->second->removed) {
				it = m_entities.erase(it);
			} else ++it;
		}

		for (auto& [id, entity] : m_entities) {
			auto& e = *entity;
			const auto& def = e.def();

			if (e.hasProp(Prop::Age))
				e.setProp(Prop::Age, e.getProp<float>(Prop::Age) + dt);

			float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
			if (hSpeed > 0.01f) {
				float dist = e.getProp<float>(Prop::WalkDistance, 0.0f);
				e.setProp(Prop::WalkDistance, dist + hSpeed * dt);
			}

			// skipPhysics: clientPos applied this tick; re-running moveAndCollide
			// would double gravity → failed jumps + sluggish input.
			if (e.skipPhysics) {
				e.skipPhysics = false;
			} else {
				glm::vec3 preVel = e.velocity;
				glm::vec3 prePos = e.position;
				auto result = stepEntityPhysics(e, e.velocity, isSolid, dt);

				// Input horiz != 0 but output ≈ 0 → collision clamp ("walks in place").
				float inHoriz  = std::sqrt(preVel.x * preVel.x + preVel.z * preVel.z);
				float outHoriz = std::sqrt(result.velocity.x * result.velocity.x +
				                            result.velocity.z * result.velocity.z);
				if (inHoriz > 0.5f && outHoriz < 0.05f) {
					char detail[192];
					snprintf(detail, sizeof(detail),
						"pos=(%.2f,%.2f,%.2f) velIn=(%.2f,%.2f) velOut=(%.2f,%.2f) box=[%.2f,%.2f]",
						prePos.x, prePos.y, prePos.z,
						preVel.x, preVel.z, result.velocity.x, result.velocity.z,
						def.collision_box_min.x, def.collision_box_max.x);
					logMoveStuck(e.id(), "Clamp",
						"server collision clamped horizontal velocity to ~0 "
						"(entity walking into geometry — 'walks in place' symptom)",
						detail);
				}
			}

			// Freeze item entities on ground; despawn after DespawnTime.
			if (e.typeId() == ItemName::ItemEntity) {
				if (e.onGround) {
					e.velocity = {0, 0, 0};
				}
				float age = e.getProp<float>(Prop::Age);
				if (age > e.getProp<float>(Prop::DespawnTime, 300.0f))
					e.removed = true;
			}
		}
	}

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

} // namespace civcraft
