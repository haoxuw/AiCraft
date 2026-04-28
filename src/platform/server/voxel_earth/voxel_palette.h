#pragma once

// Voxel-earth appearance palette: 256 source-RGB centroids derived once per
// region by median-cut over the GLB-derived voxels. Lets every voxel keep its
// original Google 3D Tiles colour as a per-cell tint while the BlockId
// (Stone, Glass, Wood, …) is picked independently from height + landuse +
// alpha sentinels. Compare:
//
//   before:  RGB → BlockId (one of 15) → block.color is the ONLY colour the
//            cell can show. A yellow voxel forced to Stone reads grey.
//   after:   BlockId from material rules; tint = source RGB normalised by
//            block.color_side. A yellow voxel routed to Stone STILL READS
//            YELLOW because the mesher does color *= palette[idx].tint.
//
// Per BlockDef has its own normalised palette so the same appearance index
// across every voxel-earth block produces the same final visual RGB. Index 0
// is reserved as identity {1,1,1} so interior-fill voxels (which have no
// meaningful source RGB) render with the block's natural colour.
//
// See docs/22_APPEARANCE.md for the appearance layer model and
// docs/VOXEL_EARTH.md for the bake → world pipeline.

#include "logic/appearance.h"
#include "logic/block_registry.h"
#include "server/voxel_earth/region.h"

#include <array>
#include <cstdint>
#include <glm/vec3.hpp>
#include <vector>

namespace solarium::voxel_earth {

constexpr int kAppearancePaletteSize = 256;
constexpr int kIdentityIndex         = 0;   // tint = (1,1,1); reserved for fill / unknown

struct VoxelPalette {
	// 256 source-RGB centroids in [0,255]. Index 0 is identity-grey,
	// indices 1..255 come from median-cut over the region's voxels.
	std::array<std::array<uint8_t, 3>, kAppearancePaletteSize> centroids{};
	bool built = false;

	// Build via median-cut over `region.voxels`, skipping voxels whose alpha
	// matches kAlphaFillStone / kAlphaFillDirt (interior fill — no real RGB).
	// Deterministic given the same region: same voxels in → same palette out.
	void build(const VoxelRegion& region);

	// Linear nearest-search in plain RGB. Returns an index in [1, 256) — never
	// 0, which is reserved for identity. Roughly 5 ns per call on a modern CPU.
	uint8_t nearest(uint8_t r, uint8_t g, uint8_t b) const;

	// Build a 256-entry AppearanceEntry palette for a block whose face base
	// colour is `block_base` (typically `BlockDef::color_side`). Each entry's
	// tint = centroid_rgb / block_base, so mesher's `color *= tint` restores
	// the centroid colour. Entry 0 is identity {1,1,1} (passthrough).
	std::vector<AppearanceEntry> as_palette_for(const glm::vec3& block_base) const;
};

}  // namespace solarium::voxel_earth
