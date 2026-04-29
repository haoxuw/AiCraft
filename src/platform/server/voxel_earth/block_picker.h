#pragma once

// BlockPicker — voxel RGB → BlockId via CIEDE2000 nearest over an atlas.
//
// Replaces the old multi-anchor / height-band table in palette.h. The atlas
// is just a list of (BlockId, representative RGB) entries; the picker
// converts each entry's RGB to Lab once and then runs CIEDE2000 against
// every voxel that comes through chunk-fill. Multiple entries per BlockId
// are allowed — Stone might appear three times at different shades, all
// three lock to the same BlockId but their REP RGBs span more of colour
// space so the picker has finer-grained anchors. The actual rendered
// colour of the placed block comes from the per-voxel appearance variant
// (see voxel_palette.h) — the picker only chooses the *material*.
//
// Empty rule of thumb: stone could be any colour because the variant
// tint can multiply its base by anything; the atlas's "stone gray" entry
// is just the picker's anchor for "approximately neutral" voxels.
//
// Alpha sentinels (interior fill from the bake, see palette.h) bypass
// the CIEDE2000 path and route to a fixed BlockId.

#include "logic/block_registry.h"
#include "logic/colorspace.h"
#include "logic/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace solarium::voxel_earth {

class BlockPicker {
public:
	struct Entry {
		BlockId  bid;
		uint8_t  r, g, b;
		Lab      lab;
	};

	// Resolve the hardcoded atlas against the BlockRegistry. Call once
	// per world boot, after registerAllBuiltins. Entries whose block
	// string-id isn't registered are skipped silently.
	void rebuild(const BlockRegistry& reg);

	// Pick a BlockId for a voxel. `a` is the alpha byte from the bake:
	//   a = 254 → fill-stone (interior, top of column band)
	//   a = 253 → fill-dirt  (interior, deeper band)
	//   else (typically 255) → CIEDE2000 nearest in the atlas.
	BlockId pick(uint8_t r, uint8_t g, uint8_t b, uint8_t a) const;

	size_t entryCount() const { return m_entries.size(); }

private:
	std::vector<Entry> m_entries;
	BlockId m_fillStone = BLOCK_AIR;
	BlockId m_fillDirt  = BLOCK_AIR;
	BlockId m_fallback  = BLOCK_AIR;
};

}  // namespace solarium::voxel_earth
