#pragma once

// Alpha sentinels used by the bake's interior-fill pass to tag voxels
// it injects (so the BlockPicker can route them to a known BlockId
// without a CIEDE2000 colour lookup). Original GLB-derived voxels
// always have a == 255.
//
// Block selection itself moved out of this file: voxel-RGB → BlockId
// happens in BlockPicker (see block_picker.h) using CIEDE2000 over an
// auto-anchored atlas. The legacy multi-anchor / height-band logic
// that used to live here is gone; per-voxel exact colour is preserved
// by the appearance-variant tint (voxel_palette.h).

#include <cstdint>

namespace solarium::voxel_earth {

inline constexpr uint8_t kAlphaFillStone = 254;
inline constexpr uint8_t kAlphaFillDirt  = 253;

}  // namespace solarium::voxel_earth
