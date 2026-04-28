#pragma once

// Voxel RGB → Solarium block-id (nearest colour in plain RGB).
//
// Two-tier palette: classification first picks from kBuildingPalette
// (always available); kGroundPalette is only added when the voxel is
// within kGroundHeightBlocks of the region floor. This stops mid-air
// beige walls from being labelled "Sand", brown panels as "Dirt", etc.
//
// Tuning: edit the RGB values below and rebuild — no need to re-bake
// the region file. Classification runs at chunk-load time, so changes
// take effect on the next world (re)connect.
//
// See docs/VOXEL_EARTH.md for the design rationale and tuning tips.
//
// Per Rule 1 (Python is the game), this is a candidate for moving into
// src/artifacts/blocks/voxel_earth_palette.py once the palette stabilises.

#include "logic/block_registry.h"
#include "logic/constants.h"
#include "logic/types.h"

#include <array>
#include <cstdint>

namespace solarium::voxel_earth {

struct PaletteEntry {
	const char* type_id;   // BlockType::* string
	uint8_t r, g, b;
};

// ─── Tunable palette ────────────────────────────────────────────────

// Allowed at every height. Covers the vast majority of urban voxels:
// walls, roofs, asphalt, glass, exposed wood. The classifier picks the
// nearest entry in plain (linear) RGB.
//
// Stone, Granite ("steel"), and Glass have multiple anchors so their
// catchment areas in colour space are wide — this is what makes a city
// look mostly gray/dark/blue with occasional accents, instead of every
// wall fighting over a single canonical RGB.
inline constexpr std::array<PaletteEntry, 15> kBuildingPalette = {{
	// ── Stone (concrete) — wide gray range ──
	{ BlockType::Stone,        110, 110, 110 },   // dark concrete
	{ BlockType::Stone,        145, 145, 145 },   // mid concrete
	{ BlockType::Stone,        180, 178, 175 },   // pale concrete (off-white walls)

	// ── Granite ("steel") — wide dark-metallic range ──
	{ BlockType::Granite,       55,  58,  65 },   // charcoal panel / black metal
	{ BlockType::Granite,       90,  92, 100 },   // dark steel
	{ BlockType::Granite,      120, 122, 132 },   // mid steel / blued concrete

	// ── Glass — wide bluish window range ──
	{ BlockType::Glass,        135, 175, 215 },   // blue glass
	{ BlockType::Glass,        160, 195, 230 },   // mid window glass
	{ BlockType::Glass,        185, 215, 240 },   // sky-reflected glass

	// ── Single-anchor accents ──
	{ BlockType::Marble,       235, 230, 220 },   // white / cream stone
	{ BlockType::Sandstone,    195, 168, 115 },   // genuine yellow-tan only
	{ BlockType::Cobblestone,   70,  70,  72 },   // very dark asphalt
	{ BlockType::Wood,         165, 125,  85 },   // warm wood cladding
	{ BlockType::Log,           95,  65,  40 },   // dark wood / brown roof
	{ BlockType::Leaves,        60, 100,  40 },   // tree canopy
}};

// Only allowed within kGroundHeightBlocks of the region floor. Keeps
// natural materials at their natural elevation — a brown voxel at y+30
// becomes Log, the same colour at y+1 can still become Dirt.
inline constexpr std::array<PaletteEntry, 5> kGroundPalette = {{
	{ BlockType::Dirt,         110,  80,  55 },
	{ BlockType::Grass,         90, 140,  60 },
	{ BlockType::Sand,         230, 215, 170 },   // actual sand-yellow
	{ BlockType::Water,         50, 100, 180 },
	{ BlockType::Snow,         245, 245, 250 },
}};

// Vertical band (in blocks above region floor) where ground entries are
// candidates. Above this band only the building palette is considered.
//
// 8 blocks ≈ 8 metres at the default 1 m voxel — enough to span the gap
// between the lake bed (region floor) and the lake surface / beaches /
// downtown street level when the bake includes water.
inline constexpr int kGroundHeightBlocks = 8;

// ─── Resolution + classification ────────────────────────────────────

// Resolves the palette to BlockIds once. Pass the result to nearest_block_id()
// in the hot loop so we don't pay registry lookups per voxel.
struct ResolvedPalette {
	std::array<BlockId, kBuildingPalette.size()> building_ids{};
	std::array<BlockId, kGroundPalette.size()>   ground_ids{};
	BlockId fallback   = BLOCK_AIR;
	BlockId fill_stone = BLOCK_AIR;   // alpha sentinel 254 → this BlockId
	BlockId fill_dirt  = BLOCK_AIR;   // alpha sentinel 253 → this BlockId

	void resolve(const BlockRegistry& reg) {
		for (size_t i = 0; i < kBuildingPalette.size(); ++i)
			building_ids[i] = reg.getId(kBuildingPalette[i].type_id);
		for (size_t i = 0; i < kGroundPalette.size(); ++i)
			ground_ids[i] = reg.getId(kGroundPalette[i].type_id);
		fallback   = reg.getId(BlockType::Stone);
		fill_stone = reg.getId(BlockType::Stone);
		fill_dirt  = reg.getId(BlockType::Dirt);
	}
};

// Alpha sentinels written by solarium-voxel-bake's interior-fill pass:
// these voxels bypass the colour classifier and go straight to a fixed
// BlockId. Original GLB-derived voxels always have a=255.
inline constexpr uint8_t kAlphaFillStone = 254;
inline constexpr uint8_t kAlphaFillDirt  = 253;

inline BlockId nearest_block_id(const ResolvedPalette& pal,
                                uint8_t r, uint8_t g, uint8_t b,
                                int height_above_ground_blocks) {
	int best_d = 0x7FFFFFFF;
	BlockId best = pal.fallback;
	auto consider = [&](const PaletteEntry& p, BlockId id) {
		const int dr = static_cast<int>(r) - static_cast<int>(p.r);
		const int dg = static_cast<int>(g) - static_cast<int>(p.g);
		const int db = static_cast<int>(b) - static_cast<int>(p.b);
		const int d  = dr * dr + dg * dg + db * db;
		if (d < best_d) { best_d = d; best = id; }
	};
	for (size_t i = 0; i < kBuildingPalette.size(); ++i)
		consider(kBuildingPalette[i], pal.building_ids[i]);
	if (height_above_ground_blocks <= kGroundHeightBlocks) {
		for (size_t i = 0; i < kGroundPalette.size(); ++i)
			consider(kGroundPalette[i], pal.ground_ids[i]);
	}
	return (best == BLOCK_AIR) ? pal.fallback : best;
}

// Top-level voxel→BlockId. Honours alpha sentinels written by the bake
// (fill voxels), falls back to colour classification for original voxels.
inline BlockId block_for_voxel(const ResolvedPalette& pal,
                               uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                               int height_above_ground_blocks) {
	if (a == kAlphaFillStone) return pal.fill_stone;
	if (a == kAlphaFillDirt)  return pal.fill_dirt;
	return nearest_block_id(pal, r, g, b, height_above_ground_blocks);
}

}  // namespace solarium::voxel_earth
