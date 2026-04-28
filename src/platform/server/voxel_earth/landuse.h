#pragma once

// Per-chunk landuse map, produced by `python -m voxel_earth landuse` from
// OpenStreetMap polygons and persisted next to a baked region as
// `landuse.json`.
//
// One byte per chunk → category enum. Loaded once at world template
// construction, queried during chunk generation to pick palette biases.
//
// Categories must stay in sync with src/python/voxel_earth/landuse.py
// (CATEGORIES list).

#include <cstdint>
#include <string>
#include <vector>

namespace solarium::voxel_earth {

enum class Landuse : uint8_t {
	Unknown      = 0,
	CityCenter   = 1,
	Residential  = 2,
	Industrial   = 3,
	Forest       = 4,
	Park         = 5,
	Farmland     = 6,
	Water        = 7,
	Beach        = 8,
};

inline const char* landuse_name(Landuse c) {
	switch (c) {
		case Landuse::Unknown:     return "unknown";
		case Landuse::CityCenter:  return "city_center";
		case Landuse::Residential: return "residential";
		case Landuse::Industrial:  return "industrial";
		case Landuse::Forest:      return "forest";
		case Landuse::Park:        return "park";
		case Landuse::Farmland:    return "farmland";
		case Landuse::Water:       return "water";
		case Landuse::Beach:       return "beach";
	}
	return "unknown";
}

struct LanduseGrid {
	int32_t origin_x_blocks  = 0;
	int32_t origin_z_blocks  = 0;
	int32_t chunk_size_blocks = 16;
	int32_t nx_chunks        = 0;
	int32_t nz_chunks        = 0;
	std::vector<uint8_t> data;     // size = nx_chunks * nz_chunks

	bool empty() const { return data.empty(); }

	// Look up by chunk position (chunk-coord, not block-coord). Returns
	// Landuse::Unknown for out-of-bounds chunks.
	Landuse at_chunk(int32_t cx, int32_t cz) const {
		const int32_t ix = cx - (origin_x_blocks / chunk_size_blocks);
		const int32_t iz = cz - (origin_z_blocks / chunk_size_blocks);
		if (ix < 0 || ix >= nx_chunks || iz < 0 || iz >= nz_chunks)
			return Landuse::Unknown;
		return static_cast<Landuse>(data[iz * nx_chunks + ix]);
	}

	// Look up by block position (the chunk containing this block).
	Landuse at_block(int32_t bx, int32_t bz) const {
		const int32_t cx = bx >= 0 ? bx / chunk_size_blocks
		                           : -((-bx + chunk_size_blocks - 1) / chunk_size_blocks);
		const int32_t cz = bz >= 0 ? bz / chunk_size_blocks
		                           : -((-bz + chunk_size_blocks - 1) / chunk_size_blocks);
		return at_chunk(cx, cz);
	}
};

// Parse landuse.json. Returns false on missing file or malformed JSON.
// On failure, *error (if non-null) gets a message; grid is cleared.
bool read_landuse(const std::string& path, LanduseGrid& grid,
                  std::string* error = nullptr);

}  // namespace solarium::voxel_earth
