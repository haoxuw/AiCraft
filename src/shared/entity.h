#pragma once

#include "shared/constants.h"
#include "shared/inventory.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <algorithm>
#include <set>
#include <memory>
#include <cstdint>

namespace modcraft {

// EntityKind, EntityId defined in constants.h

// Per-entity structure state. Allocated only for EntityKind::Structure.
// Blueprint data lives in StructureBlueprintManager (server-side); this struct
// only carries the per-instance fields needed at runtime.
struct StructureComponent {
	std::string blueprintId;       // "base:tree" — key into StructureBlueprintManager
	glm::ivec3  anchorPos = {0,0,0}; // absolute world position of the anchor block
	float       regenTimer = 0.0f;   // counts toward blueprint.regen_interval_s
};

// Property value -- anything an entity can hold
using PropValue = std::variant<
	bool,
	int,
	float,
	std::string,
	glm::vec3
>;

// Static definition of an entity type. Mirrors Python ObjectMeta for entities.
struct EntityDef {
	EntityKind kind = EntityKind::Living;
	std::string string_id;      // "base:pig"
	std::string display_name;

	// Visual
	std::string model;          // "pig.gltf" — key into client model registry
	std::string texture;        // "pig.png"
	glm::vec3 color = {1, 1, 1};

	// Audio — group name to play for ambient creature sounds, e.g. "creature_pig"
	// Leave empty to suppress ambient sounds for this entity type.
	std::string sound_group;
	float sound_volume = 0.3f;

	// Physics
	glm::vec3 collision_box_min = {-0.4f, 0.0f, -0.4f};
	glm::vec3 collision_box_max = { 0.4f, 0.9f,  0.4f};
	float gravity_scale = 1.0f;
	float walk_speed = 0.0f;
	float run_speed = 0.0f;

	// Living / Creature fields
	int max_hp = 0;
	float eye_height = 0.0f;     // eye position above feet (0 = use collision_box_max.y * 0.75)
	bool playable = false;        // true = appears in character selection menu (player skins)
	float pickup_range = 1.5f;   // max distance to pick up items (0 = cannot pickup)
	// Carry capacity, measured in material-value units (see material_values.h).
	// The sum of (count × material_value) for all items in this entity's
	// inventory must be ≤ inventory_capacity. 0 = no capacity (cannot carry).
	float inventory_capacity = 0.0f;

	// Structure fields (only meaningful when kind == EntityKind::Structure)
	// Blueprint data (block list, anchor, regen rate) lives in StructureBlueprintManager,
	// not here — per-entity blueprint state is on Entity::structure (StructureComponent).
	bool has_inventory = false;  // true for inventory-bearing structures (e.g. Chest)

	// Kind helpers
	bool isLiving()    const { return kind == EntityKind::Living; }
	bool isItem()      const { return kind == EntityKind::Item; }
	bool isStructure() const { return kind == EntityKind::Structure; }

	// Feature tags — orthogonal flags from Python artifacts (e.g. "humanoid", "hostile").
	// See FeatureTag namespace in constants.h for canonical names.
	std::vector<std::string> tags;

	bool hasTag(const std::string& tag) const {
		return std::find(tags.begin(), tags.end(), tag) != tags.end();
	}

	// Default property values (template for new instances)
	std::unordered_map<std::string, PropValue> default_props;
};

// A live entity instance in the world.
// This is the C++ runtime representation.
// Python behavior code runs via the bridge (future pybind11).
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
	float yaw = 0.0f;       // facing direction (movement-derived)
	float lookYaw = 0.0f;   // camera look yaw (independent of facing — for chunk view bias)
	float lookPitch = 0.0f; // camera look pitch (for chunk view bias)

	// --- Move destination (broadcast to clients for local physics prediction) ---
	glm::vec3 moveTarget = {0, 0, 0};  // where entity is heading
	float moveSpeed = 0.0f;            // speed toward moveTarget (0 = stopped)

	// --- Properties (dynamic key-value store) ---
	// Get a property. Returns default if not set.
	template<typename T>
	T getProp(const std::string& key, T fallback = {}) const {
		auto it = m_props.find(key);
		if (it == m_props.end()) return fallback;
		if (auto* val = std::get_if<T>(&it->second))
			return *val;
		return fallback;
	}

	// Set a property. Marks as dirty for network sync.
	void setProp(const std::string& key, PropValue value) {
		m_props[key] = std::move(value);
		m_dirty.insert(key);
	}

	// Check if a property exists
	bool hasProp(const std::string& key) const {
		return m_props.count(key) > 0;
	}

	// Get all properties (for serialization)
	const std::unordered_map<std::string, PropValue>& props() const {
		return m_props;
	}

	// --- HP shortcut ---
	int hp() const { return getProp<int>(Prop::HP, m_def->max_hp); }
	void setHp(int v) { setProp(Prop::HP, v); }
	bool alive() const { return hp() > 0; }

	// --- Structure (allocated only for EntityKind::Structure) ---
	// Per-instance state. Static blueprint lives in StructureBlueprintManager,
	// keyed by structure->blueprintId.
	std::unique_ptr<StructureComponent> structure;

	// --- Dirty tracking (for network sync) ---
	const std::set<std::string>& dirtyProps() const { return m_dirty; }
	void clearDirty() { m_dirty.clear(); }

	// --- Inventory (allocated for Living/Creature/Character kinds) ---
	std::unique_ptr<Inventory> inventory;

	// --- Eye position (for camera when possessed) ---
	glm::vec3 eyePos() const {
		float eh = m_def->eye_height;
		if (eh <= 0) eh = m_def->collision_box_max.y * 0.75f;
		return position + glm::vec3(0, eh, 0);
	}

	// --- Behavior ---
	std::string goalText;      // current goal ("Wandering", "Fleeing!")
	std::string errorText;     // last Python error traceback (empty = ok)
	bool hasError = false;     // true if behavior code has a runtime error
	bool onGround = false;     // physics: is entity standing on solid ground
	bool skipPhysics = false;  // set when clientPos accepted — client already ran physics

	// --- Server-side navigation (C_SET_GOAL / C_SET_GOAL_GROUP click-to-move) ---
	struct NavState {
		bool active = false;
		glm::vec3 longGoal  = {0, 0, 0};  // formation-adjusted destination
		glm::vec3 shortGoal = {0, 0, 0};  // current steering target (may differ when dodging)

		// Stuck detection
		float stuckTimer = 0.0f;
		glm::vec3 stuckCheckPos = {0, 0, 0};

		// Dodge state (when obstacle detected)
		float dodgeTimer = 0.0f;   // time remaining on current dodge
		int   dodgeSign  = 0;      // -1=left, +1=right, 0=straight

		void clear() { active = false; dodgeTimer = 0; dodgeSign = 0; stuckTimer = 0; }
		void setGoal(glm::vec3 g) {
			active = true; longGoal = g; shortGoal = g;
			dodgeTimer = 0; dodgeSign = 0; stuckTimer = 0;
			stuckCheckPos = {0, 0, 0};
		}
	};
	NavState nav;

	// --- Alive/active ---
	bool removed = false;           // marked for removal
	bool removalBroadcast = false;  // S_REMOVE sent to clients

private:
	EntityId m_id;
	std::string m_typeId;
	const EntityDef* m_def;
	std::unordered_map<std::string, PropValue> m_props;
	std::set<std::string> m_dirty;
};

} // namespace modcraft
