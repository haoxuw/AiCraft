#pragma once

// Entity lifecycle, type registry, physics. AI lives on agent clients; this is Python-free.

#include "logic/entity.h"
#include "logic/constants.h"
#include "logic/physics.h"
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
			// Data-driven behavior: Python artifact's "behavior" field wins over
			// any C++ bootstrap value, so every Living gets a decide() loop.
			if (!s.behavior.empty()) {
				d.default_props[Prop::BehaviorId] = s.behavior;
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
		// Rule: every Living must have a default behavior so an agent client
		// runs decide() for it. Loud warning next to the spawn is easier to
		// catch than a silent "never ran AI" failure upstream.
		if (def->isLiving()) {
			auto it = def->default_props.find(Prop::BehaviorId);
			bool hasBid = it != def->default_props.end() &&
			              std::holds_alternative<std::string>(it->second) &&
			              !std::get<std::string>(it->second).empty();
			if (!hasBid)
				printf("[EntityManager] WARN: spawned living '%s' #%u with no BehaviorId "
				       "(agent will skip it — add \"behavior\": \"...\" to artifact)\n",
				       typeId.c_str(), (unsigned)id);
		}
		m_entities[id] = std::move(entity);
		// Working set for stepPhysics — Living + Item only. Structures
		// never move and never enter this list, so the per-tick walk is
		// O(active) not O(world). unordered_map's reference stability
		// guarantees the raw pointer survives rehash; only erase()
		// invalidates it, and stepPhysics compacts the active list
		// before any erase fires.
		Entity* raw = m_entities[id].get();
		if (!def->isStructure()) m_physicsActive.push_back(raw);
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
		// Phase 6 invariant (docs/28_SEATS_AND_OWNERSHIP.md): every Living spawn
		// must be attributable to a seat. Items + structures are world-scope
		// (SEAT_NONE is correct). Warn loudly on the spawn path so a stray
		// reactive-spawn callsite that forgets to thread the originator's seat
		// is easy to catch — the AgentClient would silently never adopt it.
		if (entity->def().isLiving()) {
			int owner = entity->getProp<int>(Prop::Owner, 0);
			if (owner == 0) {
				printf("[EntityManager] WARN: living '%s' #%u spawned with "
				       "Prop::Owner=0 — add originatorSeat to the spawn call\n",
				       typeId.c_str(), (unsigned)id);
			}
		}
		return id;
	}

	Entity* get(EntityId id) {
		auto it = m_entities.find(id);
		return it != m_entities.end() ? it->second.get() : nullptr;
	}

	void remove(EntityId id, EntityRemovalReason reason = EntityRemovalReason::Unspecified) {
		auto it = m_entities.find(id);
		if (it == m_entities.end()) return;
		it->second->removed = true;
		// Don't clobber a more-specific reason that a caller already set on
		// the entity (e.g. hp→0 marked Died, then a later sweep tries to
		// remove the same id). First reason wins.
		if (it->second->removalReason == (uint8_t)EntityRemovalReason::Unspecified)
			it->second->removalReason = (uint8_t)reason;
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
		// Walk only the active set — structures were never added to it.
		// Compact in place so removed entries drop out without invalidating
		// pointers we still need below for the m_entities erase pass.
		size_t writeIdx = 0;
		for (size_t readIdx = 0; readIdx < m_physicsActive.size(); ++readIdx) {
			Entity* eptr = m_physicsActive[readIdx];
			if (eptr->removed) continue;
			Entity& e = *eptr;
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
				// Rule 6: same moveAndCollide the client uses for prediction.
				MoveParams mp = makeMoveParams(
					def.collision_box_min, def.collision_box_max,
					def.gravity_scale, def.isLiving(),
					e.getProp<bool>("fly_mode", false));
				MoveResult result = moveAndCollide(isSolid, e.position, e.velocity, dt, mp, e.onGround);
				e.position = result.position;
				e.velocity = result.velocity;
				e.onGround = result.onGround;

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
				if (age > e.getProp<float>(Prop::DespawnTime, 300.0f)) {
					e.removed = true;
					if (e.removalReason == (uint8_t)EntityRemovalReason::Unspecified)
						e.removalReason = (uint8_t)EntityRemovalReason::Despawned;
				}
			}

			m_physicsActive[writeIdx++] = eptr;
		}
		m_physicsActive.resize(writeIdx);

		// Now safe to deallocate — every dangling pointer has been compacted
		// out of m_physicsActive in the loop above.
		for (auto it = m_entities.begin(); it != m_entities.end(); ) {
			if (it->second->removed) {
				it = m_entities.erase(it);
			} else ++it;
		}
	}

	// Phase 2 instrumentation — main.cpp records this per tick to validate
	// that the working set tracks living+item count, not total.
	size_t physicsActiveCount() const { return m_physicsActive.size(); }

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
	std::vector<Entity*>                                  m_physicsActive;
	EntityId m_nextId = 1;
};

} // namespace civcraft
