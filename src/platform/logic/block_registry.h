#pragma once

#include "logic/constants.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace civcraft {

using BlockId = uint16_t;
constexpr BlockId BLOCK_AIR = 0;

enum class BlockBehavior { Passive, Active };

// Visual mesh shape (cube is default; non-cube emits explicit boxes in mesher).
enum class MeshType {
	Cube,
	Stair,     // L-step; param2 FourDir = rise direction (0=+Z, 1=+X, 2=-Z, 3=-X)
	Door,      // closed: thin panel on -Z, 0.1 thick
	DoorOpen,  // open: rotated 90° to -X
};

// None = param2 unused; FourDir = bits 0-1 facing (0=+Z, 1=+X, 2=-Z, 3=-X).
enum class Param2Type { None, FourDir };

struct BlockDef {
	std::string string_id;
	std::string display_name;

	glm::vec3 color_top    = {1, 0, 1};
	glm::vec3 color_side   = {1, 0, 1};
	glm::vec3 color_bottom = {1, 0, 1};

	bool solid = true;
	bool transparent = false;

	std::string drop;         // empty = drops itself
	int stack_max = 64;
	int light_emission = 0;

	std::string sound_place;
	std::string sound_dig;
	std::string sound_footstep;

	BlockBehavior behavior = BlockBehavior::Passive;
	std::unordered_map<std::string, int> default_state;
	std::string behavior_class;

	// Collision height in block units: 1.0=full, 0.5=slab.
	float collision_height = 1.0f;

	MeshType mesh_type = MeshType::Cube;      // visual only, not physics
	Param2Type param2type = Param2Type::None;
	bool surface_glow = false;                // client-only animated effect
};

struct BlockState {
	BlockId type;
	glm::ivec3 pos;
	std::unordered_map<std::string, int> state;
};

class BlockRegistry {
public:
	BlockId registerBlock(const BlockDef& def) {
		BlockId id = (BlockId)m_defs.size();
		m_defs.push_back(def);
		m_nameToId[def.string_id] = id;
		return id;
	}

	const BlockDef& get(BlockId id) const {
		if (id >= m_defs.size()) return m_defs[0]; // fallback to Air
		return m_defs[id];
	}

	BlockId getId(const std::string& stringId) const {
		auto it = m_nameToId.find(stringId);
		return it != m_nameToId.end() ? it->second : BLOCK_AIR;
	}

	const BlockDef* find(const std::string& stringId) const {
		auto it = m_nameToId.find(stringId);
		return it != m_nameToId.end() ? &m_defs[it->second] : nullptr;
	}

	size_t count() const { return m_defs.size(); }

private:
	std::vector<BlockDef> m_defs;
	std::unordered_map<std::string, BlockId> m_nameToId;
};

constexpr float BLOCK_FACE_SHADE[6] = {
	0.80f, 0.80f, 1.00f, 0.50f, 0.90f, 0.90f
};

} // namespace civcraft
