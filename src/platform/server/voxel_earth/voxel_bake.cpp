// Bakes a directory of cached Photorealistic 3D Tiles GLBs into a single
// VoxelRegion file (~/.voxel/regions/<name>/blocks.bin).
//
//   civcraft-voxel-bake --glb-dir <dir> --out <region.bin>
//                       [--voxel-size 1.0]
//                       [--origin x,y,z]
//
// Picks a shared ECEF origin from the first .glb in the directory unless
// --origin is provided. Uses 1m voxels by default. Multiple tiles writing
// to the same cell: first tile wins.

#include "server/voxel_earth/glb_loader.h"
#include "server/voxel_earth/region.h"
#include "server/voxel_earth/rotate.h"
#include "server/voxel_earth/texture.h"
#include "server/voxel_earth/voxelizer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace ve = civcraft::voxel_earth;
namespace fs = std::filesystem;

struct Args {
	std::string glb_dir;
	std::string out_path;
	float       voxel_size = 1.0f;
	bool        have_origin = false;
	std::array<double, 3> origin { 0, 0, 0 };
};

static bool parse_args(int argc, char** argv, Args& a) {
	for (int i = 1; i < argc; ++i) {
		std::string k = argv[i];
		auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };
		if (k == "--glb-dir")          { if (auto v = next()) a.glb_dir  = v; else return false; }
		else if (k == "--out")         { if (auto v = next()) a.out_path = v; else return false; }
		else if (k == "--voxel-size")  { if (auto v = next()) a.voxel_size = std::atof(v); else return false; }
		else if (k == "--origin") {
			const char* v = next();
			if (!v) return false;
			if (std::sscanf(v, "%lf,%lf,%lf", &a.origin[0], &a.origin[1], &a.origin[2]) != 3) return false;
			a.have_origin = true;
		} else if (k == "--help" || k == "-h") {
			return false;
		} else {
			std::fprintf(stderr, "unknown arg: %s\n", k.c_str());
			return false;
		}
	}
	return !a.glb_dir.empty() && !a.out_path.empty();
}

int main(int argc, char** argv) {
	Args a;
	if (!parse_args(argc, argv, a)) {
		std::fprintf(stderr,
			"usage: %s --glb-dir <dir> --out <region.bin>\n"
			"            [--voxel-size 1.0] [--origin x,y,z]\n",
			argv[0]);
		return 2;
	}

	std::vector<fs::path> glbs;
	for (const auto& e : fs::directory_iterator(a.glb_dir)) {
		if (e.is_regular_file() && e.path().extension() == ".glb") glbs.push_back(e.path());
	}
	std::sort(glbs.begin(), glbs.end());
	if (glbs.empty()) {
		std::fprintf(stderr, "no .glb files in %s\n", a.glb_dir.c_str());
		return 1;
	}
	std::printf("baking %zu tiles from %s\n", glbs.size(), a.glb_dir.c_str());

	if (!a.have_origin) {
		ve::Glb first;
		std::string err;
		if (!ve::load_glb(glbs.front().string(), first, &err)) {
			std::fprintf(stderr, "failed to read first tile for origin: %s\n", err.c_str());
			return 1;
		}
		a.origin = ve::origin_from_root(first);
		std::printf("origin (from first tile): %.3f, %.3f, %.3f\n",
		            a.origin[0], a.origin[1], a.origin[2]);
	}

	// Combined voxels keyed by (x, y, z) — first writer wins per cell.
	std::unordered_map<uint64_t, ve::VoxelRecord> combined;
	combined.reserve(glbs.size() * 4096);

	auto pack = [](int32_t x, int32_t y, int32_t z) -> uint64_t {
		return (static_cast<uint64_t>(static_cast<uint32_t>(x))      ) ^
		       (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 21) ^
		       (static_cast<uint64_t>(static_cast<uint32_t>(z)) << 42);
	};

	int32_t gmin_x =  INT32_MAX, gmin_y =  INT32_MAX, gmin_z =  INT32_MAX;
	int32_t gmax_x = -INT32_MAX, gmax_y = -INT32_MAX, gmax_z = -INT32_MAX;
	size_t  total_filled = 0;

	const auto t_start = std::chrono::steady_clock::now();
	for (size_t i = 0; i < glbs.size(); ++i) {
		const auto& path = glbs[i];
		ve::Glb glb;
		std::string err;
		if (!ve::load_glb(path.string(), glb, &err)) {
			std::fprintf(stderr, "[skip] %s: %s\n", path.filename().string().c_str(), err.c_str());
			continue;
		}
		ve::rotate_to_local_enu(glb, a.origin);

		ve::Texture tex;
		ve::decode_image(glb.texture0, tex);  // may fail; voxelize will fall back to grey

		ve::VoxelizeOptions vopts;
		vopts.voxel_size_meters = a.voxel_size;
		vopts.color_voxels      = true;
		const ve::VoxelGrid grid = ve::voxelize(glb, tex, vopts);
		if (grid.voxels.empty()) continue;

		// Map this tile's grid voxels to global block coords:
		//   world_pos = floor(grid.bbox_min + (gx + 0.5) * vs)
		const float vs = grid.voxel_size;
		for (const auto& v : grid.voxels) {
			const float wx = grid.bbox_min[0] + (static_cast<float>(v.x) + 0.5f) * vs;
			const float wy = grid.bbox_min[1] + (static_cast<float>(v.y) + 0.5f) * vs;
			const float wz = grid.bbox_min[2] + (static_cast<float>(v.z) + 0.5f) * vs;
			const int32_t bx = static_cast<int32_t>(std::floor(wx));
			const int32_t by = static_cast<int32_t>(std::floor(wy));
			const int32_t bz = static_cast<int32_t>(std::floor(wz));
			const uint64_t key = pack(bx, by, bz);
			auto [it, inserted] = combined.emplace(key, ve::VoxelRecord{ bx, by, bz, v.r, v.g, v.b, v.a });
			if (!inserted) continue;
			gmin_x = std::min(gmin_x, bx); gmin_y = std::min(gmin_y, by); gmin_z = std::min(gmin_z, bz);
			gmax_x = std::max(gmax_x, bx); gmax_y = std::max(gmax_y, by); gmax_z = std::max(gmax_z, bz);
			total_filled++;
		}
		if ((i + 1) % 10 == 0 || i + 1 == glbs.size()) {
			std::printf("  [%3zu/%zu] cells=%zu\n", i + 1, glbs.size(), total_filled);
		}
	}
	const auto t_end = std::chrono::steady_clock::now();
	const double t_secs = std::chrono::duration<double>(t_end - t_start).count();

	if (combined.empty()) {
		std::fprintf(stderr, "no voxels produced\n");
		return 1;
	}

	ve::VoxelRegion region;
	region.origin_ecef = a.origin;
	region.voxel_size_mm = static_cast<uint32_t>(std::round(a.voxel_size * 1000.0f));
	region.bbox_min = { gmin_x, gmin_y, gmin_z };
	region.bbox_max = { gmax_x, gmax_y, gmax_z };
	region.voxels.reserve(combined.size());
	for (auto& [_, rec] : combined) region.voxels.push_back(rec);
	std::sort(region.voxels.begin(), region.voxels.end(),
	          [](const ve::VoxelRecord& a, const ve::VoxelRecord& b) {
		if (a.y != b.y) return a.y < b.y;
		if (a.z != b.z) return a.z < b.z;
		return a.x < b.x;
	});

	fs::create_directories(fs::path(a.out_path).parent_path());
	std::string werr;
	if (!ve::write_region(a.out_path, region, &werr)) {
		std::fprintf(stderr, "write failed: %s\n", werr.c_str());
		return 1;
	}

	std::printf("\n");
	std::printf("voxels:      %zu\n", region.voxels.size());
	std::printf("bbox(blocks):min (%d, %d, %d)  max (%d, %d, %d)\n",
	            gmin_x, gmin_y, gmin_z, gmax_x, gmax_y, gmax_z);
	std::printf("span:        (%d, %d, %d) blocks\n",
	            gmax_x - gmin_x + 1, gmax_y - gmin_y + 1, gmax_z - gmin_z + 1);
	std::printf("voxel size:  %.3f m\n", a.voxel_size);
	std::printf("file:        %s (%lld bytes)\n",
	            a.out_path.c_str(),
	            static_cast<long long>(fs::file_size(a.out_path)));
	std::printf("elapsed:     %.2fs\n", t_secs);
	return 0;
}
