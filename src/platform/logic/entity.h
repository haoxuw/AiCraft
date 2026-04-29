#pragma once

#include "logic/constants.h"
#include "logic/inventory.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <algorithm>
#include <set>
#include <memory>
#include <cstdint>

namespace solarium {

// Why an entity disappeared. Wire-stable values (S_REMOVE trailing byte, v8):
// the client branches on this to pick the right FX/SFX. Kept here in logic/ so
// the server tick loop can record a reason without depending on net/.
enum class EntityRemovalReason : uint8_t {
	Unspecified  = 0,  // legacy / unknown sender — client treats as Despawned
	Died         = 1,  // killed by combat, damage, starvation — play death SFX
	Despawned    = 2,  // natural GC (item DespawnTime, anchor destroyed) — silent
	OwnerOffline = 3,  // seat disconnected; NPC snapshotted for rejoin — puff FX, no SFX
};

// Decorator attached to a structure entity. Copied by value from the blueprint
// when the entity spawns — carries both immutable config and per-instance
// runtime state so different trees can hold independent colors / progress.
//
// Adding a new feature type: extend the enum, add config+state fields, handle
// parsing in python_bridge.cpp and dispatch in server.h's tick loop.
struct StructureFeature {
	enum class Type : uint8_t { SeasonalLeaves };
	Type type = Type::SeasonalLeaves;

	// --- SeasonalLeaves config (from blueprint; treat as read-only) ---
	// Per-season candidate appearance-palette indices into the Leaves block's
	// appearance_palette. Empty season = skip. Indexed by Season enum value.
	// See docs/22_APPEARANCE.md — the block id is preserved, only the palette
	// entry changes, so save-stateless + no SourceBlockTypeMismatch rejects.
	std::vector<uint8_t> seasonVariants[4];
	// Per-tick roll (at 1 Hz dispatcher) — probability this tree transitions
	// from the default palette into the current season's palette this tick.
	// Low values spread the transition across the season so the forest
	// turns gradually rather than all at once.
	float perTickProb = 0.005f;
	// Probability a freshly-spawned tree immediately paints into the current
	// season's palette (so an autumn-start world shows autumn on load).
	// Remaining trees transition via perTickProb over time.
	float spawnTransitionChance = 0.5f;
	// Retained for schema stability; BFS fallback is no longer used — worldgen
	// now hands precise leaf positions to the feature at spawn time.
	int   scanRadius  = 5;

	// --- SeasonalLeaves runtime state (in-memory only for v1) ---
	std::vector<glm::ivec3> leafPositions;   // discovered on first tick
	bool        scanned          = false;
	int         seasonIdxApplied = -1;       // matches Season enum; -1 = never
	int         currentAppearance = -1;      // palette index last applied; -1 = never
};

// Per-Structure runtime state. Blueprint (blocks/anchor/regen) lives in StructureBlueprintManager.
struct StructureComponent {
	std::string blueprintId;       // key into StructureBlueprintManager
	glm::ivec3  anchorPos = {0,0,0};
	float       regenTimer = 0.0f;
	// Cloned from StructureBlueprint::features at spawn; carry per-instance
	// state so two adjacent trees can hold different palettes.
	std::vector<StructureFeature> features;
};

using PropValue = std::variant<
	bool,
	int,
	float,
	std::string,
	glm::vec3
>;

// Mirrors Python ObjectMeta for entities.
struct EntityDef {
	EntityKind kind = EntityKind::Living;
	std::string string_id;      // "pig"
	std::string display_name;

	// Visual
	std::string model;          // key into client model registry
	std::string texture;
	glm::vec3 color = {1, 1, 1};

	// Ambient sound group name; empty = silent.
	std::string sound_group;
	float sound_volume = 0.3f;

	// Physics
	glm::vec3 collision_box_min = {-0.4f, 0.0f, -0.4f};
	glm::vec3 collision_box_max = { 0.4f, 0.9f,  0.4f};
	float gravity_scale = 1.0f;
	float walk_speed = 0.0f;
	float run_speed = 0.0f;
	float jump_velocity = 0.0f;

	// Living / Creature fields
	int max_hp = 0;
	float eye_height = 0.0f;     // 0 = use collision_box_max.y * 0.75
	bool playable = false;        // appears in character selection
	float pickup_range = 1.5f;   // 0 = cannot pickup
	// Pickup arc duration; picker with pickup_range>0 MUST set this — also used
	// as the timeout after which an unfulfilled pickup is treated as denied.
	float pickup_fly_duration = 0.35f;
	// Carry capacity in material-value units (see material_values.h). 0 = cannot carry.
	float inventory_capacity = 0.0f;

	// Structure-only (kind == EntityKind::Structure).
	bool has_inventory = false;  // e.g. Chest

	// Kind helpers
	bool isLiving()    const { return kind == EntityKind::Living; }
	bool isItem()      const { return kind == EntityKind::Item; }
	bool isStructure() const { return kind == EntityKind::Structure; }

	// Half the XZ collision box — the entity's body radius for separation,
	// pickup, hit-test, LOS-Walk corner clearance. Single source matches
	// physics.h::makeMoveParams.halfWidth and ModelManager's cached value.
	float bodyRadius() const {
		return (collision_box_max.x - collision_box_min.x) * 0.5f;
	}
	// Body height — used by separation's wall probe and combat reach.
	float bodyHeight() const {
		return collision_box_max.y - collision_box_min.y;
	}

	// Orthogonal flags from Python artifacts. Canonical names: FeatureTag in constants.h.
	std::vector<std::string> tags;

	bool hasTag(const std::string& tag) const {
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}

	std::unordered_map<std::string, PropValue> default_props;
};

// Live entity instance. Python behaviors run via pybind bridge on agent clients.
class Entity {
public:
	Entity(EntityId id, const std::string& typeId, const EntityDef& def)
		: m_id(id), m_typeId(typeId), m_def(&def)
		, m_props(def.default_props) {
		if (def.isLiving() || (def.isStructure() && def.has_inventory))
			inventory = std::make_unique<Inventory>();
		if (def.isStructure())
			structure = std::make_unique<StructureComponent>();
	}

	// --- Identity ---
	EntityId id() const { return m_id; }
	const std::string& typeId() const { return m_typeId; }
	const EntityDef& def() const { return *m_def; }

	// --- Position & Physics ---
	glm::vec3 position = {0, 0, 0};
	glm::vec3 velocity = {0, 0, 0};
	float yaw = 0.0f;       // facing (movement-derived)
	float lookYaw = 0.0f;   // camera look yaw, independent of facing (for chunk view bias)
	float lookPitch = 0.0f;

	// Broadcast to clients for local physics prediction.
	glm::vec3 moveTarget = {0, 0, 0};
	float moveSpeed = 0.0f;            // 0 = stopped

	// --- Properties ---
	template<typename T>
	T getProp(const std::string& key, T fallback = {}) const {
		auto it = m_props.find(key);
		if (it == m_props.end()) return fallback;
		if (auto* val = std::get_if<T>(&it->second))
			return *val;
		return fallback;
	}

	// Marks dirty for network sync.
	void setProp(const std::string& key, PropValue value) {
		m_props[key] = std::move(value);
		m_dirty.insert(key);
	}

	bool hasProp(const std::string& key) const {
		return m_props.count(key) > 0;
	}

	const std::unordered_map<std::string, PropValue>& props() const {
		return m_props;
	}

	int hp() const { return getProp<int>(Prop::HP, m_def->max_hp); }
	void setHp(int v) { setProp(Prop::HP, v); }
	bool alive() const { return hp() > 0; }

	// Per-instance; blueprint lives in StructureBlueprintManager keyed by blueprintId.
	std::unique_ptr<StructureComponent> structure;

	const std::set<std::string>& dirtyProps() const { return m_dirty; }
	void clearDirty() { m_dirty.clear(); }

	std::unique_ptr<Inventory> inventory;

	// Eye/camera position when possessed.
	glm::vec3 eyePos() const {
		float eh = m_def->eye_height;
		if (eh <= 0) eh = m_def->collision_box_max.y * 0.75f;
		return position + glm::vec3(0, eh, 0);
	}

	// Behavior
	std::string goalText;      // e.g. "Wandering", "Fleeing!"
	std::string errorText;     // last Python traceback; empty = ok
	bool hasError = false;
	bool onGround = false;
	bool skipPhysics = false;  // clientPos accepted — client already ran physics
	// Phase 3 sleep/wake — stepPhysics stops walking this entity when it
	// settles (vel≈0 ∧ onGround) and removes it from the active set.
	// EntityManager::wake() flips it back true and re-pushes; sources are
	// Move resolution + onBlockChange + spawn.
	bool physicsAwake = true;

	bool removed = false;
	bool removalBroadcast = false;  // S_REMOVE sent
	uint8_t removalReason = 0;      // EntityRemovalReason wire value; set by whoever set `removed`

private:
	EntityId m_id;
	std::string m_typeId;
	const EntityDef* m_def;
	std::unordered_map<std::string, PropValue> m_props;
	std::set<std::string> m_dirty;
};

} // namespace solarium
