#pragma once

#include "shared/constants.h"
#include "shared/inventory.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <set>
#include <memory>
#include <cstdint>

namespace aicraft {

using EntityId = uint32_t;
constexpr EntityId ENTITY_NONE = 0;

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
	std::string string_id;      // "base:pig"
	std::string display_name;
	std::string category;       // "animal", "item", "npc", "player"

	// Visual
	std::string model;          // "pig.gltf"
	std::string texture;        // "pig.png"
	glm::vec3 color = {1, 1, 1};

	// Physics
	glm::vec3 collision_box_min = {-0.4f, 0.0f, -0.4f};
	glm::vec3 collision_box_max = { 0.4f, 0.9f,  0.4f};
	float gravity_scale = 1.0f;
	float walk_speed = 0.0f;
	float run_speed = 0.0f;

	// Living
	int max_hp = 0;
	float eye_height = 0.0f;     // eye position above feet (0 = use collision_box_max.y * 0.75)
	bool has_inventory = false;   // true = allocate Inventory on spawn

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
		if (def.has_inventory)
			inventory = std::make_unique<Inventory>();
	}

	// --- Identity ---
	EntityId id() const { return m_id; }
	const std::string& typeId() const { return m_typeId; }
	const EntityDef& def() const { return *m_def; }

	// --- Position & Physics ---
	glm::vec3 position = {0, 0, 0};
	glm::vec3 velocity = {0, 0, 0};
	float yaw = 0.0f;
	float pitch = 0.0f;

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

	// --- Dirty tracking (for network sync) ---
	const std::set<std::string>& dirtyProps() const { return m_dirty; }
	void clearDirty() { m_dirty.clear(); }

	// --- Inventory (allocated only for entities with has_inventory) ---
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

	// --- Alive/active ---
	bool removed = false;  // marked for removal

private:
	EntityId m_id;
	std::string m_typeId;
	const EntityDef* m_def;
	std::unordered_map<std::string, PropValue> m_props;
	std::set<std::string> m_dirty;
};

} // namespace aicraft
