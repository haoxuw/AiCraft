#pragma once

// Client-side cache of everything resolved per entity-type at boot:
//   * BoxModel registry (visual data — parts, pivots, baked variants),
//     keyed by model-stem (e.g. "villager", "villager#0", ...).
//   * ResolvedModel per typeId — pre-derived bodyRadius, bodyHeight,
//     eyeHeight, walkSpeed, plus a non-owning pointer at the visual.
//
// Sibling to LocalWorldManager and EntityManager on Game (does not own
// either; reads EntityDefs by reference at loadAll time). Lookups are
// O(1) hash hits — no Python calls, no per-tick def() walks.
//
// The resolved bodyRadius matches `physics.h::makeMoveParams.halfWidth`
// — `(collision_box_max.x - collision_box_min.x) * 0.5`. Driving this
// from one place keeps planner / pathfinder / collision / separation
// in sync with whatever the .py def says.

#include "client/box_model.h"
#include "client/model_loader.h"
#include "logic/entity.h"
#include "server/entity_manager.h"
#include <string>
#include <unordered_map>

namespace solarium {

struct ResolvedModel {
	std::string     typeId;                   // "villager"
	const BoxModel* visual = nullptr;         // null = no model in registry
	int             variantCount = 0;         // 0 = no variants, just plain key

	// Pre-derived from EntityDef::collision_box_min/max.
	float bodyRadius = 0.375f;   // (max.x - min.x) * 0.5  (matches physics)
	float bodyHeight = 1.8f;     //  max.y - min.y
	float eyeHeight  = 1.35f;    //  def.eye_height or bodyHeight * 0.75

	// Movement-relevant (mirror of EntityDef so callers don't reach back).
	float walkSpeed    = 0.0f;
	float runSpeed     = 0.0f;
	float jumpVelocity = 0.0f;
	bool  isLiving     = false;
};

class ModelManager {
public:
	ModelManager() = default;

	// One-shot eager load. Reads every .py under {artifactsDir}/models/base/
	// (via model_loader::loadAllModels) AND walks every typeId registered
	// in `defs` to build a ResolvedModel cache. Idempotent — calling
	// twice replaces both maps. EntityManager must already have the
	// applyLivingStats step run; resolved values reflect whatever it has.
	void loadAll(const std::string& artifactsDir, const EntityManager& defs) {
		m_models = model_loader::loadAllModels(artifactsDir);
		m_resolved.clear();
		m_resolved.reserve(64);
		defs.forEachDef([this](const std::string& id, const EntityDef& def) {
			ResolvedModel r;
			r.typeId       = id;
			r.visual       = boxModel(def.model);
			r.variantCount = model_loader::countVariants(m_models, def.model);
			r.bodyRadius   = def.bodyRadius();
			r.bodyHeight   = def.bodyHeight();
			r.eyeHeight    = def.eye_height > 0.0f
			                 ? def.eye_height
			                 : r.bodyHeight * 0.75f;
			r.walkSpeed    = def.walk_speed;
			r.runSpeed     = def.run_speed;
			r.jumpVelocity = def.jump_velocity;
			r.isLiving     = def.isLiving();
			m_resolved.emplace(id, std::move(r));
		});
	}

	// O(1) lookup of an already-resolved typeId. Null if loadAll() hasn't
	// run or the typeId wasn't present in the EntityManager at load time.
	const ResolvedModel* get(const std::string& typeId) const {
		auto it = m_resolved.find(typeId);
		return it != m_resolved.end() ? &it->second : nullptr;
	}

	// Visual-only lookup (renderer hot path). Variant key form `name#N`
	// works directly; plain `name` resolves to variant 0 when variants
	// exist (matches model_loader behaviour).
	const BoxModel* boxModel(const std::string& key) const {
		auto it = m_models.find(key);
		return it != m_models.end() ? &it->second : nullptr;
	}

	int countVariants(const std::string& key) const {
		return model_loader::countVariants(m_models, key);
	}

	size_t modelCount()    const { return m_models.size(); }
	size_t resolvedCount() const { return m_resolved.size(); }

	// Direct access for code that still wants to iterate the raw map
	// (e.g. tooling). Prefer the lookup methods above for hot paths.
	const std::unordered_map<std::string, BoxModel>& models() const {
		return m_models;
	}

private:
	std::unordered_map<std::string, BoxModel>       m_models;
	std::unordered_map<std::string, ResolvedModel>  m_resolved;
};

}  // namespace solarium
