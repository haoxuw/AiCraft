#pragma once

#include "shared/constants.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace agentica {

using BlockId = uint16_t;
constexpr BlockId BLOCK_AIR = 0;

enum class BlockBehavior { Passive, Active };

// Visual mesh shape for a block. Cube = full 1x1x1 box (default).
// Non-cube shapes are emitted as explicit box geometry in the mesher.
enum class MeshType {
	Cube,      // full unit cube (default)
	Stair,     // bottom slab (0..0.5 full) + back step (0.5..1, z=0.5..1)
	Door,      // thin vertical panel on -Z face (closed, 0.1 thick)
	DoorOpen,  // thin vertical panel on -X face (open, rotated 90°)
};

struct BlockDef {
	std::string string_id;
	std::string display_name;
	std::string category;

	glm::vec3 color_top    = {1, 0, 1};
	glm::vec3 color_side   = {1, 0, 1};
	glm::vec3 color_bottom = {1, 0, 1};

	bool solid = true;
	bool transparent = false;

	float hardness = 1.0f;
	std::string tool_group;
	std::string drop;
	int stack_max = 64;
	int light_emission = 0;

	std::unordered_map<std::string, int> groups;

	std::string sound_place;
	std::string sound_dig;
	std::string sound_footstep;

	BlockBehavior behavior = BlockBehavior::Passive;
	std::unordered_map<std::string, int> default_state;
	std::string behavior_class;

	// Physics collision height in block units.
	// 1.0 = full block (default), 0.5 = half-height (stairs/slabs).
	float collision_height = 1.0f;

	// Visual mesh shape (does not affect physics).
	MeshType mesh_type = MeshType::Cube;
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
		if (id >= m_defs.size()) return m_defs[0]; // fallback to Air if out of range
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

	// registerBuiltins() removed -- use builtin/builtin.h instead.

private:
	std::vector<BlockDef> m_defs;
	std::unordered_map<std::string, BlockId> m_nameToId;
};

constexpr float BLOCK_FACE_SHADE[6] = {
	0.80f, 0.80f, 1.00f, 0.50f, 0.90f, 0.90f
};

} // namespace agentica
