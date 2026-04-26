#pragma once

// VoxelRegion — a baked, self-contained slice of voxel earth on disk.
//
// Format (little-endian):
//   header (68 bytes):
//     magic[4]   = "VEAR"
//     version    = 1   (uint32)
//     origin[3]  = ECEF X, Y, Z used during baking (double)
//     vsize_mm   = voxel edge in millimeters (uint32)  ← e.g. 1000 for 1m voxels
//     bbox_min[3]= int32 world-block coords (1 voxel = 1 cell)
//     bbox_max[3]= int32 world-block coords  (inclusive)
//     count      = number of records (uint32)
//   records (count × 16 bytes):
//     x, y, z    = int32 world-block coords (within bbox)
//     r, g, b, a = uint8
//
// Coords are integer voxel cells, with the implicit understanding that one
// cell maps to one CivCraft block. The world template (next phase) chooses
// where in CivCraft world space the region's (0,0,0) lands, by adding a
// translation offset before lookup.

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace civcraft::voxel_earth {

struct VoxelRecord {
	int32_t x, y, z;
	uint8_t r, g, b, a;
};

struct VoxelRegion {
	std::array<double, 3>   origin_ecef { 0, 0, 0 };
	uint32_t                voxel_size_mm = 1000;
	std::array<int32_t, 3>  bbox_min { 0, 0, 0 };
	std::array<int32_t, 3>  bbox_max { 0, 0, 0 };
	std::vector<VoxelRecord> voxels;

	// Sparse map keyed by packed (x, y, z) → index into `voxels`. Built lazily;
	// callers that only stream the file (e.g. the bake tool) won't pay for it.
	std::unordered_map<uint64_t, uint32_t> index;

	void build_index();
	const VoxelRecord* lookup(int32_t x, int32_t y, int32_t z) const;
};

// Returns true on success. On failure, *error (if non-null) gets a message.
bool write_region(const std::string& path, const VoxelRegion& region, std::string* error = nullptr);
bool read_region (const std::string& path, VoxelRegion& region,        std::string* error = nullptr);

}  // namespace civcraft::voxel_earth
