// Stand-alone smoke test for the voxel_earth GLB loader. Loads one cached
// Photorealistic 3D Tile (e.g. ~/.voxel/google/glb/<obb-sha>.glb) and prints
// mesh stats. Built on demand via:
//
//   cmake --build build -j1 --target solarium-voxel-smoke
//   ./build/solarium-voxel-smoke ~/.voxel/google/glb/<sha>.glb

#include "server/voxel_earth/glb_loader.h"
#include "server/voxel_earth/rotate.h"
#include "server/voxel_earth/texture.h"
#include "server/voxel_earth/voxelizer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace ve = solarium::voxel_earth;

static void print_bbox(const ve::Glb& g, const char* label) {
	float min_x =  std::numeric_limits<float>::infinity();
	float min_y =  std::numeric_limits<float>::infinity();
	float min_z =  std::numeric_limits<float>::infinity();
	float max_x = -std::numeric_limits<float>::infinity();
	float max_y = -std::numeric_limits<float>::infinity();
	float max_z = -std::numeric_limits<float>::infinity();
	for (const auto& m : g.meshes) {
		for (const auto& p : m.positions) {
			min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
			min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
			min_z = std::min(min_z, p.z); max_z = std::max(max_z, p.z);
		}
	}
	std::printf("%-12s min (%.2f, %.2f, %.2f)  max (%.2f, %.2f, %.2f)  span (%.2f, %.2f, %.2f)\n",
	            label,
	            min_x, min_y, min_z,
	            max_x, max_y, max_z,
	            max_x - min_x, max_y - min_y, max_z - min_z);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <tile.glb> [origin_x origin_y origin_z]\n", argv[0]);
		return 2;
	}

	ve::Glb glb;
	std::string err;
	if (!ve::load_glb(argv[1], glb, &err)) {
		std::fprintf(stderr, "load failed: %s\n", err.c_str());
		return 1;
	}

	size_t total_v = 0, total_t = 0;
	bool has_uvs = false, has_normals = false;
	for (const auto& m : glb.meshes) {
		total_v += m.positions.size();
		total_t += m.indices.size() / 3;
		has_uvs     = has_uvs     || !m.uvs.empty();
		has_normals = has_normals || !m.normals.empty();
	}

	std::printf("file:        %s\n", argv[1]);
	std::printf("meshes:      %zu  vertices: %zu  triangles: %zu\n",
	            glb.meshes.size(), total_v, total_t);
	std::printf("uvs/normals: %s / %s\n", has_uvs ? "yes" : "no", has_normals ? "yes" : "no");
	std::printf("texture0:    %s, %zu bytes\n",
	            glb.texture0.format.empty() ? "<none>" : glb.texture0.format.c_str(),
	            glb.texture0.bytes.size());
	std::printf("draco:       %s\n", glb.uses_draco ? "yes (decoded)" : "no");
	std::printf("root T:      (%.3f, %.3f, %.3f)\n",
	            glb.root_translation[0], glb.root_translation[1], glb.root_translation[2]);
	print_bbox(glb, "bbox(mesh)");

	std::array<double, 3> origin;
	if (argc >= 5) {
		origin = { std::atof(argv[2]), std::atof(argv[3]), std::atof(argv[4]) };
	} else {
		origin = ve::origin_from_root(glb);
	}
	std::printf("origin:      (%.3f, %.3f, %.3f)\n", origin[0], origin[1], origin[2]);

	ve::rotate_to_local_enu(glb, origin);
	print_bbox(glb, "bbox(ENU)");

	// --- Phase 3: decode texture + voxelize ---
	ve::Texture tex;
	std::string terr;
	if (!ve::decode_image(glb.texture0, tex, &terr)) {
		std::printf("texture decode: skipped (%s)\n", terr.c_str());
	} else {
		std::printf("texture:     %dx%d, %zu bytes RGBA\n", tex.width, tex.height, tex.rgba.size());
	}

	const int resolution = (argc >= 6) ? std::atoi(argv[5]) : 64;
	ve::VoxelizeOptions vopts;
	vopts.resolution = resolution;
	const ve::VoxelGrid grid = ve::voxelize(glb, tex, vopts);

	std::printf("grid:        dims=(%d,%d,%d) voxel_size=%.3fm  filled=%zu / %d (%.1f%%)\n",
	            grid.dims[0], grid.dims[1], grid.dims[2], grid.voxel_size,
	            grid.voxels.size(), grid.total_cells(),
	            grid.total_cells() ? (100.0 * grid.voxels.size() / grid.total_cells()) : 0.0);

	// Per-Y-slab counts as a quick sanity profile (low Y = ground, high Y = roof).
	std::vector<int> per_y(grid.dims[1], 0);
	uint64_t r_acc = 0, g_acc = 0, b_acc = 0;
	for (const auto& v : grid.voxels) {
		if (v.y < per_y.size()) per_y[v.y]++;
		r_acc += v.r; g_acc += v.g; b_acc += v.b;
	}
	if (!grid.voxels.empty()) {
		std::printf("avg color:   rgb(%llu, %llu, %llu)\n",
		            static_cast<unsigned long long>(r_acc / grid.voxels.size()),
		            static_cast<unsigned long long>(g_acc / grid.voxels.size()),
		            static_cast<unsigned long long>(b_acc / grid.voxels.size()));
	}
	std::printf("per-Y-slab voxel counts (top to bottom):\n");
	const int max_count = per_y.empty() ? 1 : *std::max_element(per_y.begin(), per_y.end());
	for (int y = grid.dims[1] - 1; y >= 0; --y) {
		const int c = per_y[y];
		const int bar = max_count > 0 ? (40 * c / max_count) : 0;
		std::printf("  y=%3d %5d  %.*s\n", y, c, bar, "########################################");
	}

	return 0;
}
