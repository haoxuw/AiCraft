#pragma once

// Voxel RGB → CivCraft block-id (nearest colour in plain RGB).
//
// Phase 4 keeps this hardcoded. Per Rule 1, this is a candidate for moving
// into src/artifacts/blocks/ as data the modder can override; the follow-up
// will lift this list to Python.

#include "logic/block_registry.h"
#include "logic/constants.h"
#include "logic/types.h"

#include <array>
#include <cstdint>

namespace civcraft::voxel_earth {

struct PaletteEntry {
	const char* type_id;   // BlockType::* string
	uint8_t r, g, b;
};

// Toronto-friendly natural palette. First nearest wins on ties.
inline constexpr std::array<PaletteEntry, 11> kDefaultPalette = {{
	{ BlockType::Stone,         128, 128, 128 },   // gray concrete / asphalt-ish
	{ BlockType::Cobblestone,    90,  90,  90 },   // dark asphalt
	{ BlockType::Sand,          220, 200, 160 },   // tan stone / pavement
	{ BlockType::Dirt,          110,  80,  55 },   // brown dirt
	{ BlockType::Grass,          90, 140,  60 },   // green grass
	{ BlockType::Leaves,         60, 100,  40 },   // tree canopy
	{ BlockType::Wood,          170, 130,  85 },   // light wood / lumber
	{ BlockType::Log,            95,  65,  40 },   // dark roof / wood
	{ BlockType::Glass,         200, 220, 240 },   // bluish glass / sky reflection
	{ BlockType::Water,          50, 100, 180 },   // saturated blue
	{ BlockType::Snow,          240, 240, 240 },   // white roof / paint
}};

// Resolves the palette to BlockIds once. Pass the result to nearest_block_id()
// in the hot loop so we don't pay registry lookups per voxel.
struct ResolvedPalette {
	std::array<BlockId, kDefaultPalette.size()> ids{};
	BlockId fallback = BLOCK_AIR;

	void resolve(const BlockRegistry& reg) {
		for (size_t i = 0; i < kDefaultPalette.size(); ++i) {
			ids[i] = reg.getId(kDefaultPalette[i].type_id);
		}
		fallback = reg.getId(BlockType::Stone);
	}
};

inline BlockId nearest_block_id(const ResolvedPalette& pal,
                                uint8_t r, uint8_t g, uint8_t b) {
	int best_i = 0;
	int best_d = 0x7FFFFFFF;
	for (int i = 0; i < static_cast<int>(kDefaultPalette.size()); ++i) {
		const auto& p = kDefaultPalette[i];
		const int dr = static_cast<int>(r) - static_cast<int>(p.r);
		const int dg = static_cast<int>(g) - static_cast<int>(p.g);
		const int db = static_cast<int>(b) - static_cast<int>(p.b);
		const int d = dr * dr + dg * dg + db * db;
		if (d < best_d) { best_d = d; best_i = i; }
	}
	const BlockId id = pal.ids[best_i];
	return (id == BLOCK_AIR) ? pal.fallback : id;
}

}  // namespace civcraft::voxel_earth
