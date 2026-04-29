#pragma once

// Tile shards — the on-disk unit of voxel-earth caching.
//
// One shard = a 16×16 grid of chunk-columns (= 256 m × 256 m × full-Y) sharing
// a regional ENU frame. The frame's origin is `(floor(lat), floor(lng))` —
// a 1°×1° grid — so any two bakes whose centre lat/lng falls in the same
// 1° square use the same frame and therefore write to the same tile files.
// Distortion across a 1° square is well under 0.001 % at city scales.
//
// File-name convention is the only key:
//
//   ~/.voxel/tiles/r<rlat>_<rlng>/tile_<tx>_<tz>.vtil
//
// The file name alone says where the shard goes. A reader can place any
// shard it receives by parsing the integers; copying or sharing tiles
// between machines is a plain `cp`. Header self-describes the rest
// (origin_ecef, voxel_size, voxel_count, per-column zone byte).
//
// Layout (little-endian, packed):
//
//   header (offset 0, sizeof = 320 bytes):
//     char     magic[4];           // "VTIL"
//     uint32_t version;            // 1
//     int32_t  region_lat_deg;     // floor(lat), used as ENU origin lat
//     int32_t  region_lng_deg;     // floor(lng), used as ENU origin lng
//     int32_t  tile_x;             // chunk-column tile index along world +X
//     int32_t  tile_z;             //                           ... +Z
//     uint32_t voxel_size_mm;      // typically 1000 (1 m blocks)
//     uint32_t voxel_count;        // records that follow
//     double   origin_ecef[3];     // ECEF origin of this regional frame
//     int32_t  bbox_min[3];        // tightest voxel bbox in this shard
//     int32_t  bbox_max[3];        //   inclusive, regional-frame block coords
//     uint8_t  zones[TILE_CHUNK_SIDE * TILE_CHUNK_SIDE];   // per-column zone
//     uint8_t  reserved[…];        // pad to header size
//
//   body (immediately after header):
//     VoxelRecord records[voxel_count];   // 16 bytes each, regional-frame coords
//
// `VoxelRecord` is the same struct used by VoxelRegion. Coords (x,y,z) are
// integer block positions in the regional ENU frame; for any voxel inside
// this shard they satisfy
//     tile_x * 16 * CHUNK_SIZE  <=  x  <  (tile_x + 1) * 16 * CHUNK_SIZE
//     tile_z * 16 * CHUNK_SIZE  <=  z  <  (tile_z + 1) * 16 * CHUNK_SIZE
//
// Y is unconstrained (full-column).

#include "logic/types.h"
#include "server/voxel_earth/region.h"   // VoxelRecord

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace solarium::voxel_earth {

// 16 chunks per tile side → 256-block tile width with the engine's CHUNK_SIZE=16.
constexpr int TILE_CHUNK_SIDE = 16;
constexpr int TILE_BLOCK_SIDE = TILE_CHUNK_SIDE * CHUNK_SIZE;
constexpr int TILE_COLUMNS    = TILE_CHUNK_SIDE * TILE_CHUNK_SIDE;
constexpr uint32_t TILE_SHARD_VERSION = 1;
constexpr uint32_t TILE_SHARD_HEADER_BYTES = 320;

// Map a (block_x, block_z) world coord in the regional frame to its tile coord.
inline int32_t tile_x_of(int32_t bx) {
	return bx >= 0 ? bx / TILE_BLOCK_SIDE
	               : -((-bx + TILE_BLOCK_SIDE - 1) / TILE_BLOCK_SIDE);
}
inline int32_t tile_z_of(int32_t bz) { return tile_x_of(bz); }

// Map a (block_x, block_z) to its (cx_local, cz_local) within the tile,
// where cx_local ∈ [0, TILE_CHUNK_SIDE).
inline int32_t chunk_local_x(int32_t bx) {
	const int32_t tx = tile_x_of(bx);
	const int32_t cx = bx >= 0 ? bx / CHUNK_SIZE
	                           : -((-bx + CHUNK_SIZE - 1) / CHUNK_SIZE);
	return cx - tx * TILE_CHUNK_SIDE;
}
inline int32_t chunk_local_z(int32_t bz) { return chunk_local_x(bz); }

// Snap a real lat/lng to its regional-frame anchor. Always rounds down so
// (floor(lat), floor(lng)) gives a stable cell ID.
inline int32_t region_anchor_lat(double lat) {
	return static_cast<int32_t>(std::floor(lat));
}
inline int32_t region_anchor_lng(double lng) {
	return static_cast<int32_t>(std::floor(lng));
}

struct TileShard {
	int32_t  region_lat = 0;
	int32_t  region_lng = 0;
	int32_t  tile_x     = 0;
	int32_t  tile_z     = 0;
	uint32_t voxel_size_mm = 1000;
	std::array<double, 3>  origin_ecef { 0, 0, 0 };
	std::array<int32_t, 3> bbox_min { 0, 0, 0 };
	std::array<int32_t, 3> bbox_max { 0, 0, 0 };
	std::array<uint8_t, TILE_COLUMNS> zones {};   // per-column zone byte
	std::vector<VoxelRecord>          voxels;
};

// Construct the canonical shard path under `root`. Layout:
//   <root>/r<lat>_<lng>/tile_<tx>_<tz>.vtil
std::string shard_path(const std::string& root,
                       int32_t region_lat, int32_t region_lng,
                       int32_t tile_x, int32_t tile_z);

// I/O. Round-trip safe. write_shard creates parent dirs as needed.
bool write_shard(const std::string& path, const TileShard& shard,
                 std::string* error = nullptr);
bool read_shard(const std::string& path, TileShard& shard,
                std::string* error = nullptr);

// Parse "tile_<tx>_<tz>.vtil" or full-path equivalents. Returns false if
// the basename doesn't match the schema.
bool parse_shard_filename(const std::string& path,
                          int32_t& tile_x, int32_t& tile_z);

}  // namespace solarium::voxel_earth
