#pragma once

// A Zone tags a (chunk-coarse) place in the world with a gameplay category —
// "the player is in a Park" — so HUD, audio, AI, and content systems can
// branch on it without each query reaching back into terrain noise / OSM
// polygons / village stamps. Zones are immutable for v1: assigned once at
// chunk generation, sent once on stream-in, never updated at runtime.
//
// Source of truth for *where each zone comes from* is ZoneProvider — the
// engine never inspects OSM polygons or Perlin output directly to derive a
// zone, it asks the template's provider.
//
// 10 entries (fits in 4 bits, kept in u8 for future headroom). Matches the
// taxonomy in src/python/voxel_earth/landuse.py for the OSM provider.

#include <cstdint>

namespace solarium {

enum class Zone : uint8_t {
	Unknown    = 0,   // no data; default for non-zoned templates
	Wilderness = 1,   // forest / wood / dense vegetation
	Park       = 2,   // managed green space (paths, lawn)
	Water      = 3,   // lake / river / ocean surface
	Beach      = 4,   // sand at water's edge
	Farmland   = 5,   // open fields / agriculture
	Town       = 6,   // low residential / suburb
	City       = 7,   // dense urban / downtown
	Industrial = 8,   // factories / warehouses
	Village    = 9,   // engine-stamped village footprint
};

inline const char* zoneName(Zone z) {
	switch (z) {
		case Zone::Unknown:    return "Unknown";
		case Zone::Wilderness: return "Wilderness";
		case Zone::Park:       return "Park";
		case Zone::Water:      return "Water";
		case Zone::Beach:      return "Beach";
		case Zone::Farmland:   return "Farmland";
		case Zone::Town:       return "Town";
		case Zone::City:       return "City";
		case Zone::Industrial: return "Industrial";
		case Zone::Village:    return "Village";
	}
	return "Unknown";
}

}  // namespace solarium
