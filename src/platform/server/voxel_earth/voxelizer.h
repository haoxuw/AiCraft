#pragma once

// CPU voxelizer — 2.5D dominant-axis scan converter, ports
// JavaCpuVoxelizer.cpuRasterize2D5 from VoxelEarth's reference implementation.
//
// Per triangle:
//   1. Pick the major axis of its normal (X, Y, or Z).
//   2. Project to the orthogonal 2D plane → scan-convert pixels.
//   3. For each covered pixel, compute depth W along the major axis at the
//      pixel center, deposit a voxel at floor(W). Optionally also at the
//      neighbor when W is close to a slab boundary, to keep thin surfaces
//      watertight.
//   4. Track the closest triangle per voxel (smallest distance²) so we can
//      colour the voxel from the right surface during the coloring pass.
//
// Coloring is barycentric UV → bilinear texture sample (see texture.h).

#include "server/voxel_earth/glb_loader.h"
#include "server/voxel_earth/texture.h"

#include <array>
#include <cstdint>
#include <vector>

namespace civcraft::voxel_earth {

struct Voxel {
	uint16_t x, y, z;     // grid coords [0, grid.x)
	uint8_t  r, g, b, a;  // rgba8
};

struct VoxelGrid {
	std::array<int, 3>     dims      { 0, 0, 0 };  // nx, ny, nz
	std::array<float, 3>   bbox_min  { 0, 0, 0 };  // ENU meters
	std::array<float, 3>   bbox_max  { 0, 0, 0 };
	float                  voxel_size = 0.0f;      // meters per voxel (uniform)
	std::vector<Voxel>     voxels;                  // sparse: only filled cells

	int total_cells() const { return dims[0] * dims[1] * dims[2]; }
};

struct VoxelizeOptions {
	int   resolution         = 128;     // longest axis is split into this many voxels
	float voxel_size_meters  = 0.0f;    // if > 0, overrides resolution
	bool  color_voxels       = true;    // sample texture at voxel center → rgba
};

// Voxelize a (post-rotate) Glb. `texture` may be empty if you don't care
// about colour (will fill voxels with grey). Returns the grid by value;
// memory is bounded by # of filled cells (sparse).
VoxelGrid voxelize(const Glb& glb,
                   const Texture& texture,
                   const VoxelizeOptions& opts = {});

}  // namespace civcraft::voxel_earth
