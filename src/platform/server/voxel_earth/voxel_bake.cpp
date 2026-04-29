// Bakes a directory of cached Photorealistic 3D Tiles GLBs into a single
// VoxelRegion file (~/.voxel/regions/<name>/blocks.bin).
//
//   solarium-voxel-bake --glb-dir <dir> --out <region.bin>
//                       [--voxel-size 1.0]
//                       [--origin x,y,z]
//                       [--no-fill]                   # skip interior fill
//                       [--fill-stone-depth 8]        # stone band thickness
//
// Picks a shared ECEF origin from the first .glb in the directory unless
// --origin is provided. Uses 1m voxels by default. Multiple tiles writing
// to the same cell: first tile wins.
//
// Interior fill (default ON):
//   Google 3D Tiles deliver surface meshes only — building interiors are
//   hollow. The bake follows up the voxelisation with a 6-neighbour BFS
//   flood-fill from the bbox boundary, marking every reachable empty cell
//   as "exterior". Cells that are neither original voxels nor exterior are
//   "interior" and get filled with Stone (top --fill-stone-depth blocks of
//   the column) or Dirt (deeper). Filled voxels carry alpha sentinels:
//     a=254 -> stone-fill
//     a=253 -> dirt-fill
//   so the chunk-fill classifier can pick the right block at any height
//   without the colour palette getting confused.
//
// Water columns (topmost voxel reads as blue) are skipped so lakes don't
// get buried.

#include "server/voxel_earth/glb_loader.h"
#include "server/voxel_earth/palette.h"     // kAlphaFillStone / kAlphaFillDirt
#include "server/voxel_earth/region.h"
#include "server/voxel_earth/rotate.h"
#include "server/voxel_earth/texture.h"
#include "server/voxel_earth/tile_shard.h"
#include "server/voxel_earth/voxelizer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ve = solarium::voxel_earth;
namespace fs = std::filesystem;

struct Args {
	std::string glb_dir;
	std::string glb_list;       // file with one GLB path per line; overrides --glb-dir
	std::string out_path;
	std::string tile_out;       // optional dir for per-tile .vtil shards
	int         region_lat = 0; // regional ENU frame anchor (1° grid)
	int         region_lng = 0;
	bool        have_region = false;
	float       voxel_size = 1.0f;
	bool        have_origin = false;
	std::array<double, 3> origin { 0, 0, 0 };
	bool        do_fill = true;
	int         fill_stone_depth = 8;
};

static bool parse_args(int argc, char** argv, Args& a) {
	for (int i = 1; i < argc; ++i) {
		std::string k = argv[i];
		auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };
		if (k == "--glb-dir")          { if (auto v = next()) a.glb_dir  = v; else return false; }
		else if (k == "--glb-list")    { if (auto v = next()) a.glb_list = v; else return false; }
		else if (k == "--out")         { if (auto v = next()) a.out_path = v; else return false; }
		else if (k == "--voxel-size")  { if (auto v = next()) a.voxel_size = std::atof(v); else return false; }
		else if (k == "--origin") {
			const char* v = next();
			if (!v) return false;
			if (std::sscanf(v, "%lf,%lf,%lf", &a.origin[0], &a.origin[1], &a.origin[2]) != 3) return false;
			a.have_origin = true;
		}
		else if (k == "--no-fill")          { a.do_fill = false; }
		else if (k == "--fill-stone-depth") { if (auto v = next()) a.fill_stone_depth = std::atoi(v); else return false; }
		else if (k == "--tile-out")         { if (auto v = next()) a.tile_out = v; else return false; }
		else if (k == "--region-lat") {
			if (auto v = next()) { a.region_lat = std::atoi(v); a.have_region = true; } else return false;
		}
		else if (k == "--region-lng") {
			if (auto v = next()) { a.region_lng = std::atoi(v); a.have_region = true; } else return false;
		}
		else if (k == "--help" || k == "-h") {
			return false;
		} else {
			std::fprintf(stderr, "unknown arg: %s\n", k.c_str());
			return false;
		}
	}
	if (a.out_path.empty()) return false;
	if (a.glb_dir.empty() && a.glb_list.empty()) return false;
	return true;
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
	if (!a.glb_list.empty()) {
		// Per-location filter: one GLB path per line. Skips Toronto's 5729
		// tiles when we asked for Wonderland's 199 — without this the bbox
		// spans both bakes and the interior-fill bitmaps blow past 27 GB.
		std::ifstream f(a.glb_list);
		if (!f) {
			std::fprintf(stderr, "cannot open --glb-list %s\n", a.glb_list.c_str());
			return 1;
		}
		std::string line;
		while (std::getline(f, line)) {
			// trim
			while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
			size_t i = 0;
			while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
			if (i == line.size() || line[i] == '#') continue;
			glbs.emplace_back(line.substr(i));
		}
		if (glbs.empty()) {
			std::fprintf(stderr, "--glb-list %s contained no paths\n", a.glb_list.c_str());
			return 1;
		}
		std::printf("baking %zu tiles from %s\n", glbs.size(), a.glb_list.c_str());
	} else {
		for (const auto& e : fs::directory_iterator(a.glb_dir)) {
			if (e.is_regular_file() && e.path().extension() == ".glb") glbs.push_back(e.path());
		}
		std::sort(glbs.begin(), glbs.end());
		if (glbs.empty()) {
			std::fprintf(stderr, "no .glb files in %s\n", a.glb_dir.c_str());
			return 1;
		}
		std::printf("baking %zu tiles from %s\n", glbs.size(), a.glb_dir.c_str());
	}

	if (!a.have_origin) {
		// If a previous bake already populated this regional tile dir,
		// inherit its ECEF origin. That keeps voxels from successive bakes
		// in the same regional frame so their tiles overlap correctly and
		// the writer can union into existing shards. Without this, every
		// bake picks its own first-GLB origin → cross-bake merge gives
		// stuttered voxels at tile-boundary collisions.
		bool inherited = false;
		if (a.have_region && !a.tile_out.empty()) {
			const std::string region_dir =
				a.tile_out + "/r" + std::to_string(a.region_lat) +
				"_" + std::to_string(a.region_lng);
			std::error_code ec;
			for (auto& entry :
			     fs::directory_iterator(region_dir, ec)) {
				if (!entry.is_regular_file()) continue;
				if (entry.path().extension() != ".vtil") continue;
				ve::TileShard sh;
				std::string serr;
				if (ve::read_shard(entry.path().string(), sh, &serr)) {
					a.origin = sh.origin_ecef;
					inherited = true;
					std::printf("origin (inherited from %s): %.3f, %.3f, %.3f\n",
					            entry.path().filename().string().c_str(),
					            a.origin[0], a.origin[1], a.origin[2]);
				}
				break;  // any shard works — they all share the regional origin
			}
		}
		if (!inherited) {
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

	// ── Interior fill pass ──────────────────────────────────────────────
	// Flood-fill exterior from the bbox boundary, then write Stone/Dirt
	// voxels into every empty interior cell so players can't fall through
	// hollow buildings.
	size_t fill_stone_count = 0;
	size_t fill_dirt_count  = 0;
	if (a.do_fill) {
		const auto t_fill0 = std::chrono::steady_clock::now();
		const int64_t nx = (int64_t)gmax_x - gmin_x + 1;
		const int64_t ny = (int64_t)gmax_y - gmin_y + 1;
		const int64_t nz = (int64_t)gmax_z - gmin_z + 1;
		const int64_t total_cells = nx * ny * nz;
		const int64_t words = (total_cells + 63) / 64;

		std::printf("\ninterior fill: bbox %lldx%lldx%lld = %.2fGcells, %lluMB/bitmap\n",
		            (long long)nx, (long long)ny, (long long)nz,
		            (double)total_cells / 1e9,
		            (unsigned long long)((words * 8) / (1024*1024)));

		std::vector<uint64_t> bmFilled(words, 0);
		std::vector<uint64_t> bmExterior(words, 0);

		auto idx = [&](int x, int y, int z) -> int64_t {
			return (int64_t)(x - gmin_x)
			     + nx * ((int64_t)(y - gmin_y) + ny * (int64_t)(z - gmin_z));
		};
		auto getbit = [&](const std::vector<uint64_t>& bm, int64_t i) -> bool {
			return (bm[i >> 6] >> (i & 63)) & 1ull;
		};
		auto setbit = [&](std::vector<uint64_t>& bm, int64_t i) {
			bm[i >> 6] |= (uint64_t)1 << (i & 63);
		};

		// 1. Mark filled.
		for (auto& kv : combined) setbit(bmFilled, idx(kv.second.x, kv.second.y, kv.second.z));

		// 2. BFS flood-fill exterior from boundary. std::deque<> rather than a
		// vector-as-stack so memory tracks the active frontier (~surface area)
		// instead of growing to the worst-case path length: with DFS the
		// vector capacity blows past 2 GB on a 1700×630×2300 bbox and the
		// kernel OOM-kills the bake.
		std::deque<int64_t> queue;
		auto pushIfAir = [&](int x, int y, int z) {
			if (x < gmin_x || x > gmax_x ||
			    y < gmin_y || y > gmax_y ||
			    z < gmin_z || z > gmax_z) return;
			int64_t i = idx(x, y, z);
			if (getbit(bmFilled, i) || getbit(bmExterior, i)) return;
			setbit(bmExterior, i);
			queue.push_back(i);
		};

		// Seed all six faces.
		for (int y = gmin_y; y <= gmax_y; ++y)
			for (int z = gmin_z; z <= gmax_z; ++z) {
				pushIfAir(gmin_x, y, z); pushIfAir(gmax_x, y, z);
			}
		for (int x = gmin_x; x <= gmax_x; ++x)
			for (int z = gmin_z; z <= gmax_z; ++z) {
				pushIfAir(x, gmin_y, z); pushIfAir(x, gmax_y, z);
			}
		for (int x = gmin_x; x <= gmax_x; ++x)
			for (int y = gmin_y; y <= gmax_y; ++y) {
				pushIfAir(x, y, gmin_z); pushIfAir(x, y, gmax_z);
			}

		size_t bfs_visits = 0;
		size_t bfs_peak_q = 0;
		while (!queue.empty()) {
			int64_t i = queue.front(); queue.pop_front();
			int64_t r = i;
			int x = gmin_x + (int)(r % nx); r /= nx;
			int y = gmin_y + (int)(r % ny); r /= ny;
			int z = gmin_z + (int)r;
			pushIfAir(x - 1, y, z); pushIfAir(x + 1, y, z);
			pushIfAir(x, y - 1, z); pushIfAir(x, y + 1, z);
			pushIfAir(x, y, z - 1); pushIfAir(x, y, z + 1);
			if (queue.size() > bfs_peak_q) bfs_peak_q = queue.size();
			if ((++bfs_visits & ((1 << 24) - 1)) == 0)
				std::printf("  bfs visited %.0fM (queue=%.1fM peak=%.1fM)\n",
				            (double)bfs_visits / 1e6,
				            (double)queue.size() / 1e6,
				            (double)bfs_peak_q / 1e6);
		}
		std::printf("  bfs done: visits=%zu peak_queue=%zu\n", bfs_visits, bfs_peak_q);

		// 3. Per-(x,z) topmost voxel + water-column detection.
		auto packxz = [](int32_t x, int32_t z) -> uint64_t {
			return ((uint64_t)(uint32_t)x) | ((uint64_t)(uint32_t)z << 32);
		};
		struct ColTop { int32_t y_top; bool is_water; };
		std::unordered_map<uint64_t, ColTop> column_top;
		column_top.reserve(combined.size());
		for (auto& kv : combined) {
			const auto& rec = kv.second;
			auto k = packxz(rec.x, rec.z);
			auto it = column_top.find(k);
			const bool blue = (int)rec.b > std::max((int)rec.r, (int)rec.g) + 20;
			if (it == column_top.end()) column_top.emplace(k, ColTop{ rec.y, blue });
			else if (rec.y > it->second.y_top) it->second = { rec.y, blue };
		}

		// 4. Insert fill voxels for interior cells.
		constexpr uint8_t kStoneR = 145, kStoneG = 145, kStoneB = 145, kStoneA = 254;
		constexpr uint8_t kDirtR  = 110, kDirtG  =  80, kDirtB  =  55, kDirtA  = 253;
		size_t skipped_water_columns = 0;
		for (auto& [k, ct] : column_top) {
			if (ct.is_water) { ++skipped_water_columns; continue; }
			int32_t cx = (int32_t)(uint32_t)(k & 0xFFFFFFFFull);
			int32_t cz = (int32_t)(uint32_t)(k >> 32);
			for (int y = gmin_y; y < ct.y_top; ++y) {
				int64_t i = idx(cx, y, cz);
				if (getbit(bmFilled, i) || getbit(bmExterior, i)) continue;
				const int depth = ct.y_top - y;
				ve::VoxelRecord rec;
				rec.x = cx; rec.y = y; rec.z = cz;
				if (depth <= a.fill_stone_depth) {
					rec.r = kStoneR; rec.g = kStoneG; rec.b = kStoneB; rec.a = kStoneA;
					++fill_stone_count;
				} else {
					rec.r = kDirtR; rec.g = kDirtG; rec.b = kDirtB; rec.a = kDirtA;
					++fill_dirt_count;
				}
				combined.emplace(pack(cx, y, cz), rec);
				gmin_y = std::min(gmin_y, y);
			}
		}
		const auto t_fill1 = std::chrono::steady_clock::now();
		const double t_fill = std::chrono::duration<double>(t_fill1 - t_fill0).count();
		std::printf("interior fill: stone=%zu dirt=%zu, water columns skipped=%zu, %.2fs\n",
		            fill_stone_count, fill_dirt_count, skipped_water_columns, t_fill);

		// 5. Empty-column ground extrapolation. Google Photorealistic 3D Tiles
		//    only delivers mesh where there's geometric detail; flat asphalt /
		//    grass / lake-edge tiles often produce ZERO voxels for whole (x,z)
		//    columns inside the bake bbox. The result is visible holes ("void
		//    next to the player"). Walk every column inside the bbox and, for
		//    columns with no voxels, inject a Dirt record at the median ground
		//    elevation so the world has a floor everywhere.
		int64_t med_sum = 0; size_t med_n = 0;
		for (auto& [k, ct] : column_top) {
			if (ct.is_water) continue;
			med_sum += ct.y_top;
			++med_n;
		}
		const int32_t ground_y = med_n ? (int32_t)(med_sum / (int64_t)med_n) : gmin_y;
		size_t synth_ground_count = 0;
		for (int32_t cx = gmin_x; cx <= gmax_x; ++cx) {
			for (int32_t cz = gmin_z; cz <= gmax_z; ++cz) {
				if (column_top.count(packxz(cx, cz))) continue;  // already has content
				ve::VoxelRecord rec;
				rec.x = cx; rec.y = ground_y; rec.z = cz;
				rec.r = kDirtR; rec.g = kDirtG; rec.b = kDirtB; rec.a = kDirtA;
				combined.emplace(pack(cx, ground_y, cz), rec);
				++synth_ground_count;
			}
		}
		std::printf("ground synth: %zu empty columns filled at y=%d (median surface)\n",
		            synth_ground_count, ground_y);
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

	// Optional: also emit per-tile VTIL shards. Each tile holds 16×16
	// chunk-columns of voxels (= 256 m × 256 m × full-Y). Independent files,
	// keyed by their (tile_x, tile_z) in the file name — copy any subset
	// across machines and the engine slots them in by name. v1 uses the
	// bake's own ECEF origin; the shared-frame migration (commit 4) will
	// switch to floor(lat)/floor(lng) anchors so two bakes whose centres
	// land in the same 1° square write to the SAME shards.
	if (!a.tile_out.empty()) {
		const auto t_sh0 = std::chrono::steady_clock::now();
		std::unordered_map<uint64_t, ve::TileShard> tiles;
		auto packTile = [](int32_t tx, int32_t tz) -> uint64_t {
			return ((uint64_t)(uint32_t)tx) | ((uint64_t)(uint32_t)tz << 32);
		};
		// Skip interior-fill voxels in the shard pass — the engine
		// synthesises them on chunk-fill from the column_top_y metadata
		// we record below. Real GLB-derived voxels (a == 255) are the
		// only ones that need to ride along on disk + on the wire.
		for (const auto& v : region.voxels) {
			const bool is_fill = (v.a == ve::kAlphaFillStone) ||
			                     (v.a == ve::kAlphaFillDirt);
			const int32_t tx = ve::tile_x_of(v.x);
			const int32_t tz = ve::tile_z_of(v.z);
			auto& sh = tiles[packTile(tx, tz)];
			if (sh.voxels.empty() && sh.bbox_min == sh.bbox_max
			    && sh.bbox_min[0] == 0 && sh.bbox_min[1] == 0 && sh.bbox_min[2] == 0) {
				sh.region_lat    = a.region_lat;
				sh.region_lng    = a.region_lng;
				sh.tile_x        = tx;
				sh.tile_z        = tz;
				sh.voxel_size_mm = region.voxel_size_mm;
				sh.origin_ecef   = region.origin_ecef;
				sh.bbox_min      = { v.x, v.y, v.z };
				sh.bbox_max      = { v.x, v.y, v.z };
			} else {
				if (v.x < sh.bbox_min[0]) sh.bbox_min[0] = v.x;
				if (v.y < sh.bbox_min[1]) sh.bbox_min[1] = v.y;
				if (v.z < sh.bbox_min[2]) sh.bbox_min[2] = v.z;
				if (v.x > sh.bbox_max[0]) sh.bbox_max[0] = v.x;
				if (v.y > sh.bbox_max[1]) sh.bbox_max[1] = v.y;
				if (v.z > sh.bbox_max[2]) sh.bbox_max[2] = v.z;
			}
			// Update per-column top_y for synth-fill (covers BOTH real
			// voxels and the BFS-emitted fill — fill is dropped from the
			// shard's voxel array, but its y still defines the column's
			// surface for engine-side reconstruction).
			const int32_t cxl = ve::chunk_local_x(v.x);
			const int32_t czl = ve::chunk_local_z(v.z);
			const size_t  ci  = (size_t)czl * ve::TILE_CHUNK_SIDE + (size_t)cxl;
			if (ci < ve::TILE_COLUMNS &&
			    (sh.column_top_y[ci] == ve::COLUMN_TOP_Y_NONE ||
			     v.y > sh.column_top_y[ci])) {
				sh.column_top_y[ci] = v.y;
			}
			if (!is_fill) sh.voxels.push_back(v);
		}
		// Cross-bake merge: if a shard for this tile already exists on
		// disk, union the existing voxels with the new ones (new wins on
		// (x, y, z) collision). The shared regional ECEF origin (above)
		// guarantees the coord frames match, so the union is a plain
		// dedupe on the voxel key.
		size_t written = 0, merged_existing = 0;
		for (auto& [_, sh] : tiles) {
			const std::string p = ve::shard_path(a.tile_out,
			                                     sh.region_lat, sh.region_lng,
			                                     sh.tile_x, sh.tile_z);
			ve::TileShard prev;
			std::string rerr;
			if (ve::read_shard(p, prev, &rerr)) {
				// Index this bake's voxels for O(1) collision check.
				std::unordered_set<uint64_t> mineKeys;
				mineKeys.reserve(sh.voxels.size());
				for (const auto& v : sh.voxels) mineKeys.insert(pack(v.x, v.y, v.z));
				size_t kept = 0;
				for (const auto& v : prev.voxels) {
					if (mineKeys.count(pack(v.x, v.y, v.z))) continue;  // new wins
					sh.voxels.push_back(v);
					if (v.x < sh.bbox_min[0]) sh.bbox_min[0] = v.x;
					if (v.y < sh.bbox_min[1]) sh.bbox_min[1] = v.y;
					if (v.z < sh.bbox_min[2]) sh.bbox_min[2] = v.z;
					if (v.x > sh.bbox_max[0]) sh.bbox_max[0] = v.x;
					if (v.y > sh.bbox_max[1]) sh.bbox_max[1] = v.y;
					if (v.z > sh.bbox_max[2]) sh.bbox_max[2] = v.z;
					++kept;
				}
				// Carry over any zone bytes the prior bake recorded that
				// this bake didn't (this bake leaves zones=0; landuse pass
				// will fill them in a later step).
				for (size_t i = 0; i < ve::TILE_COLUMNS; ++i)
					if (sh.zones[i] == 0 && prev.zones[i] != 0)
						sh.zones[i] = prev.zones[i];
				// Take the max of column_top_y so a taller bake's surface
				// wins over a shorter overlapping bake. Either side may be
				// COLUMN_TOP_Y_NONE for columns it didn't touch.
				for (size_t i = 0; i < ve::TILE_COLUMNS; ++i) {
					if (prev.column_top_y[i] == ve::COLUMN_TOP_Y_NONE) continue;
					if (sh.column_top_y[i] == ve::COLUMN_TOP_Y_NONE ||
					    prev.column_top_y[i] > sh.column_top_y[i])
						sh.column_top_y[i] = prev.column_top_y[i];
				}
				++merged_existing;
				(void)kept;
			}
			std::string serr;
			if (!ve::write_shard(p, sh, &serr)) {
				std::fprintf(stderr, "shard write failed: %s\n", serr.c_str());
				return 1;
			}
			++written;
		}
		const auto t_sh1 = std::chrono::steady_clock::now();
		const double t_sh = std::chrono::duration<double>(t_sh1 - t_sh0).count();
		std::printf("tile shards: %zu files in %s (%zu merged with existing, %.2fs)\n",
		            written, a.tile_out.c_str(), merged_existing, t_sh);
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
