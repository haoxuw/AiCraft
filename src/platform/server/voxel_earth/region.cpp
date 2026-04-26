#include "server/voxel_earth/region.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace civcraft::voxel_earth {

namespace {

inline uint64_t pack_key(int32_t x, int32_t y, int32_t z) {
	// Mix axes into 64 bits — works for any region under ~2M voxels per axis,
	// which is way past anything we'll fit in RAM.
	const uint64_t ux = static_cast<uint32_t>(x);
	const uint64_t uy = static_cast<uint32_t>(y);
	const uint64_t uz = static_cast<uint32_t>(z);
	return (ux * 0x9E3779B97F4A7C15ULL) ^ (uy * 0xBF58476D1CE4E5B9ULL) ^ (uz * 0x94D049BB133111EBULL);
}

#pragma pack(push, 1)
struct OnDiskHeader {
	char     magic[4];          // "VEAR"
	uint32_t version;
	double   origin[3];
	uint32_t voxel_size_mm;
	int32_t  bbox_min[3];
	int32_t  bbox_max[3];
	uint32_t count;
	uint32_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(OnDiskHeader) == 68, "header layout drifted");
static_assert(sizeof(VoxelRecord) == 16, "VoxelRecord layout drifted");

}  // namespace

void VoxelRegion::build_index() {
	index.clear();
	index.reserve(voxels.size() * 2);
	for (uint32_t i = 0; i < voxels.size(); ++i) {
		const auto& v = voxels[i];
		index.emplace(pack_key(v.x, v.y, v.z), i);
	}
}

const VoxelRecord* VoxelRegion::lookup(int32_t x, int32_t y, int32_t z) const {
	auto it = index.find(pack_key(x, y, z));
	if (it == index.end()) return nullptr;
	const auto& v = voxels[it->second];
	if (v.x != x || v.y != y || v.z != z) return nullptr;  // hash collision
	return &v;
}

bool write_region(const std::string& path, const VoxelRegion& region, std::string* error) {
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) {
		if (error) *error = "cannot open " + path + " for write";
		return false;
	}
	OnDiskHeader h{};
	std::memcpy(h.magic, "VEAR", 4);
	h.version       = 1;
	h.origin[0]     = region.origin_ecef[0];
	h.origin[1]     = region.origin_ecef[1];
	h.origin[2]     = region.origin_ecef[2];
	h.voxel_size_mm = region.voxel_size_mm;
	for (int i = 0; i < 3; ++i) h.bbox_min[i] = region.bbox_min[i];
	for (int i = 0; i < 3; ++i) h.bbox_max[i] = region.bbox_max[i];
	h.count         = static_cast<uint32_t>(region.voxels.size());
	h.reserved      = 0;
	f.write(reinterpret_cast<const char*>(&h), sizeof(h));
	if (!region.voxels.empty()) {
		f.write(reinterpret_cast<const char*>(region.voxels.data()),
		        static_cast<std::streamsize>(region.voxels.size() * sizeof(VoxelRecord)));
	}
	return f.good();
}

bool read_region(const std::string& path, VoxelRegion& region, std::string* error) {
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		if (error) *error = "cannot open " + path + " for read";
		return false;
	}
	OnDiskHeader h{};
	f.read(reinterpret_cast<char*>(&h), sizeof(h));
	if (!f || std::memcmp(h.magic, "VEAR", 4) != 0 || h.version != 1) {
		if (error) *error = "not a VEAR v1 file";
		return false;
	}
	region.origin_ecef = { h.origin[0], h.origin[1], h.origin[2] };
	region.voxel_size_mm = h.voxel_size_mm;
	region.bbox_min = { h.bbox_min[0], h.bbox_min[1], h.bbox_min[2] };
	region.bbox_max = { h.bbox_max[0], h.bbox_max[1], h.bbox_max[2] };
	region.voxels.resize(h.count);
	if (h.count > 0) {
		f.read(reinterpret_cast<char*>(region.voxels.data()),
		       static_cast<std::streamsize>(h.count * sizeof(VoxelRecord)));
		if (!f) {
			if (error) *error = "truncated voxel records";
			return false;
		}
	}
	return true;
}

}  // namespace civcraft::voxel_earth
