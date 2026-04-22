#pragma once

#include "logic/constants.h"
#include "logic/appearance.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace civcraft {

using BlockId = uint16_t;
constexpr BlockId BLOCK_AIR = 0;

enum class BlockBehavior { Passive, Active };

// Visual mesh shape (cube is default; non-cube emits explicit boxes in mesher).
enum class MeshType {
	Cube,
	Slab,         // half-height box; param2 bit 0 = top-half vs bottom-half
	Stair,        // L-step; param2 FourDir = rise direction
	CornerStair,  // L corner stair; param2 FourDir = exposed-corner compass dir
	Pillar,       // thin square column; param2 bits 0-1 = axis 0=Y, 1=X, 2=Z
	Trapdoor,     // horizontal door panel; param2 bits 0-1 = hinge edge,
	              // bit 2 = top-half (1) / bottom-half (0) for "open"
	Torch,        // thin central post; param2 0..4 = floor + 4 wall-lean dirs
	Door,         // closed: thin panel on -Z, 0.1 thick
	DoorOpen,     // open: rotated 90° to -X
	Plant,        // decoration: Bezier blades/tufts emitted by the mesher.
	              // color_side = base (root), color_top = tip. Non-solid cells
	              // sit ON TOP of their supporting block. Worldgen places them
	              // on specific surfaces (tall_grass on grass, cattail on sand
	              // near water, etc.) — mesh is generic, placement is content.
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

	// Visual variations. Entry 0 is the default (implicit if palette is empty).
	// See docs/22_APPEARANCE.md — appearance is independent of block type.
	std::vector<AppearanceEntry> appearance_palette;

	// Clamp an incoming appearance index to [0, paletteSize). An empty palette
	// is treated as a single entry (the default), so the only valid index is 0.
	uint8_t clampAppearance(uint8_t idx) const {
		size_t n = appearance_palette.empty() ? 1 : appearance_palette.size();
		return (idx < n) ? idx : 0;
	}
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
