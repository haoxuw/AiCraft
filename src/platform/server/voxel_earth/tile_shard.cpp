#include "server/voxel_earth/tile_shard.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace solarium::voxel_earth {

namespace {

#pragma pack(push, 1)
struct OnDiskHeader {
	char     magic[4];          // "VTIL"
	uint32_t version;
	int32_t  region_lat;
	int32_t  region_lng;
	int32_t  tile_x;
	int32_t  tile_z;
	uint32_t voxel_size_mm;
	uint32_t voxel_count;
	double   origin[3];
	int32_t  bbox_min[3];
	int32_t  bbox_max[3];
	uint8_t  zones[TILE_COLUMNS];
	int32_t  column_top_y[TILE_COLUMNS];   // v2: synth-fill metadata
	// Field byte count:
	//   4(magic) + 4(version) + 16(region+tile coords) + 4+4(sizes) +
	//   24(origin) + 12+12(bbox) + 256(zones) + 1024(column_top_y) = 1360
	// Pad to a round 1408 so we have 48 bytes of v3 headroom.
	uint8_t  reserved[1408 - (4+4+16+4+4+24+12+12+TILE_COLUMNS+TILE_COLUMNS*4)];
};
#pragma pack(pop)

static_assert(sizeof(OnDiskHeader) == 1408, "tile shard header size drift");
static_assert(sizeof(VoxelRecord) == 16, "VoxelRecord layout drifted");

}  // namespace

std::string shard_path(const std::string& root,
                       int32_t region_lat, int32_t region_lng,
                       int32_t tile_x, int32_t tile_z) {
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%s/r%d_%d/tile_%d_%d.vtil",
	              root.c_str(), region_lat, region_lng, tile_x, tile_z);
	return buf;
}

bool write_shard(const std::string& path, const TileShard& shard,
                 std::string* error) {
	std::error_code ec;
	std::filesystem::create_directories(
		std::filesystem::path(path).parent_path(), ec);
	// fine if dir already exists; only fail if we genuinely can't open output
	std::ofstream f(path + ".part", std::ios::binary | std::ios::trunc);
	if (!f) {
		if (error) *error = "cannot open " + path + ".part for write";
		return false;
	}
	OnDiskHeader h{};
	std::memcpy(h.magic, "VTIL", 4);
	h.version       = TILE_SHARD_VERSION;
	h.region_lat    = shard.region_lat;
	h.region_lng    = shard.region_lng;
	h.tile_x        = shard.tile_x;
	h.tile_z        = shard.tile_z;
	h.voxel_size_mm = shard.voxel_size_mm;
	h.voxel_count   = static_cast<uint32_t>(shard.voxels.size());
	for (int i = 0; i < 3; ++i) h.origin[i]   = shard.origin_ecef[i];
	for (int i = 0; i < 3; ++i) h.bbox_min[i] = shard.bbox_min[i];
	for (int i = 0; i < 3; ++i) h.bbox_max[i] = shard.bbox_max[i];
	std::memcpy(h.zones, shard.zones.data(), TILE_COLUMNS);
	for (int i = 0; i < TILE_COLUMNS; ++i) h.column_top_y[i] = shard.column_top_y[i];
	f.write(reinterpret_cast<const char*>(&h), sizeof(h));
	if (!shard.voxels.empty()) {
		f.write(reinterpret_cast<const char*>(shard.voxels.data()),
		        static_cast<std::streamsize>(shard.voxels.size() * sizeof(VoxelRecord)));
	}
	if (!f.good()) {
		if (error) *error = "write failed for " + path;
		return false;
	}
	f.close();
	std::filesystem::rename(path + ".part", path, ec);
	if (ec) {
		if (error) *error = "rename failed: " + ec.message();
		return false;
	}
	return true;
}

bool read_shard(const std::string& path, TileShard& shard, std::string* error) {
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		if (error) *error = "cannot open " + path + " for read";
		return false;
	}
	// v1 shards have a smaller header (384 bytes, no column_top_y). Peek
	// the magic + version first and only read the fields that exist.
	char m[4];
	uint32_t version;
	f.read(m, 4);
	f.read(reinterpret_cast<char*>(&version), 4);
	if (!f || std::memcmp(m, "VTIL", 4) != 0 ||
	    (version != 1 && version != TILE_SHARD_VERSION)) {
		if (error) *error = "not a VTIL v1/v2 file: " + path;
		return false;
	}
	OnDiskHeader h{};
	std::memcpy(h.magic, m, 4);
	h.version = version;
	if (version == TILE_SHARD_VERSION) {
		f.read(reinterpret_cast<char*>(&h) + 8, sizeof(h) - 8);
	} else {
		// v1: header was 384 bytes total → fields[8..384) are 376 bytes
		// of legacy layout. column_top_y stays at COLUMN_TOP_Y_NONE.
		f.read(reinterpret_cast<char*>(&h) + 8, 384 - 8);
	}
	if (!f) {
		if (error) *error = "truncated VTIL header: " + path;
		return false;
	}
	shard.region_lat    = h.region_lat;
	shard.region_lng    = h.region_lng;
	shard.tile_x        = h.tile_x;
	shard.tile_z        = h.tile_z;
	shard.voxel_size_mm = h.voxel_size_mm;
	shard.origin_ecef   = { h.origin[0], h.origin[1], h.origin[2] };
	shard.bbox_min      = { h.bbox_min[0], h.bbox_min[1], h.bbox_min[2] };
	shard.bbox_max      = { h.bbox_max[0], h.bbox_max[1], h.bbox_max[2] };
	std::memcpy(shard.zones.data(), h.zones, TILE_COLUMNS);
	if (version == TILE_SHARD_VERSION) {
		for (int i = 0; i < TILE_COLUMNS; ++i)
			shard.column_top_y[i] = h.column_top_y[i];
	}
	shard.voxels.resize(h.voxel_count);
	if (h.voxel_count > 0) {
		f.read(reinterpret_cast<char*>(shard.voxels.data()),
		       static_cast<std::streamsize>(h.voxel_count * sizeof(VoxelRecord)));
		if (!f) {
			if (error) *error = "truncated voxel records: " + path;
			return false;
		}
	}
	return true;
}

bool parse_shard_filename(const std::string& path,
                          int32_t& tile_x, int32_t& tile_z) {
	const auto base = std::filesystem::path(path).filename().string();
	int tx = 0, tz = 0;
	if (std::sscanf(base.c_str(), "tile_%d_%d.vtil", &tx, &tz) != 2)
		return false;
	tile_x = tx;
	tile_z = tz;
	return true;
}

}  // namespace solarium::voxel_earth
