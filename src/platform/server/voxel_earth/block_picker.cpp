#include "server/voxel_earth/block_picker.h"
#include "server/voxel_earth/palette.h"   // alpha sentinels

#include "logic/constants.h"

#include <cstdio>

namespace solarium::voxel_earth {

namespace {

// Atlas — (block string-id, anchor RGB). Multiple entries per block-id are
// fine; they widen the picker's catchment for that material across colour
// space. The voxel's exact rendered colour comes from the per-voxel
// appearance variant (see voxel_palette), so these RGBs only steer
// material classification, not the displayed shade.
//
// Curated to roughly match VoxelEarth's vanilla.atlas tone-map but at the
// scale of Solarium's smaller block set. Add more entries here when a
// specific colour family keeps being mis-classified.
struct AtlasRow { const char* type_id; uint8_t r, g, b; };
constexpr AtlasRow kAtlas[] = {
	// ── Stone family (concrete, asphalt, masonry) ──
	{ BlockType::Stone,         60,  60,  62 },   // dark concrete / shadow
	{ BlockType::Stone,        110, 110, 112 },   // mid concrete
	{ BlockType::Stone,        160, 160, 162 },   // pale concrete
	{ BlockType::Stone,        205, 205, 207 },   // off-white wall

	// ── Granite ("steel" / dark metallic panels) ──
	{ BlockType::Granite,       40,  45,  55 },   // black metal
	{ BlockType::Granite,       80,  85,  95 },   // charcoal panel
	{ BlockType::Granite,      120, 125, 135 },   // mid steel

	// ── Marble (cream / warm pale stone) ──
	{ BlockType::Marble,       235, 230, 220 },

	// ── Sandstone (warm tan / yellow stone) ──
	{ BlockType::Sandstone,    195, 168, 115 },
	{ BlockType::Sandstone,    230, 200, 140 },

	// ── Cobblestone (very dark gray / asphalt) ──
	{ BlockType::Cobblestone,   50,  50,  52 },

	// ── Glass (window blues + pale tints) ──
	{ BlockType::Glass,        135, 175, 215 },
	{ BlockType::Glass,        165, 200, 230 },
	{ BlockType::Glass,        200, 220, 240 },

	// ── Wood (warm browns) ──
	{ BlockType::Wood,         165, 125,  85 },
	{ BlockType::Wood,         210, 165, 110 },

	// ── Log (dark brown / cherry) ──
	{ BlockType::Log,           90,  60,  40 },
	{ BlockType::Log,          135,  95,  60 },

	// ── Leaves (greens + autumn shades) ──
	{ BlockType::Leaves,        50, 100,  35 },   // deep summer
	{ BlockType::Leaves,       100, 150,  50 },   // bright spring
	{ BlockType::Leaves,       180, 130,  30 },   // autumn gold
	{ BlockType::Leaves,       170,  60,  30 },   // autumn red

	// ── Grass (single ground-greens entry; tint variant carries) ──
	{ BlockType::Grass,         95, 145,  55 },

	// ── Dirt (browns) ──
	{ BlockType::Dirt,         110,  80,  55 },
	{ BlockType::Dirt,          75,  55,  35 },

	// ── Sand (beach sand) ──
	{ BlockType::Sand,         225, 205, 170 },

	// ── Water (mid-blue lake) ──
	{ BlockType::Water,         50, 100, 180 },

	// ── Snow (off-white) ──
	{ BlockType::Snow,         245, 245, 250 },
};

}  // namespace

void BlockPicker::rebuild(const BlockRegistry& reg) {
	m_entries.clear();
	m_entries.reserve(sizeof(kAtlas) / sizeof(kAtlas[0]));
	for (const auto& row : kAtlas) {
		const BlockId id = reg.getId(row.type_id);
		if (id == BLOCK_AIR) continue;  // unregistered (test build) — skip
		m_entries.push_back(Entry{ id, row.r, row.g, row.b,
		                           rgbToLab(row.r, row.g, row.b) });
	}
	m_fillStone = reg.getId(BlockType::Stone);
	m_fillDirt  = reg.getId(BlockType::Dirt);
	m_fallback  = m_fillStone;
	std::printf("[BlockPicker] %zu atlas entries loaded (fill_stone=%u fill_dirt=%u)\n",
	            m_entries.size(), (unsigned)m_fillStone, (unsigned)m_fillDirt);
}

BlockId BlockPicker::pick(uint8_t r, uint8_t g, uint8_t b, uint8_t a) const {
	if (a == kAlphaFillStone) return m_fillStone;
	if (a == kAlphaFillDirt)  return m_fillDirt;
	if (m_entries.empty())    return m_fallback;
	const Lab src = rgbToLab(r, g, b);
	double best = 1e300;
	BlockId pick = m_fallback;
	for (const auto& e : m_entries) {
		const double d = deltaE2000(src, e.lab);
		if (d < best) { best = d; pick = e.bid; }
	}
	return pick;
}

}  // namespace solarium::voxel_earth
