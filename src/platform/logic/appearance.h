#pragma once

// Per-BlockDef appearance palette. A block cell stores an 8-bit index into
// BlockDef::appearance_palette; each entry holds the visual variation (tint,
// pattern) applied by the client mesher. Appearance is independent of block
// type — see docs/22_APPEARANCE.md for the full invariant model.

#include <glm/vec3.hpp>
#include <string>

namespace civcraft {

struct AppearanceEntry {
	glm::vec3 tint{1.f, 1.f, 1.f};  // multiplied into the block's face colors
	std::string pattern;            // optional named pattern (empty = solid)
};

} // namespace civcraft
