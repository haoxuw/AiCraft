#pragma once

// OSM-backed ZoneProvider. Wraps a LanduseGrid loaded from landuse.json
// (produced by `python -m voxel_earth landuse`) and exposes a per-block
// zone lookup via the engine's ZoneProvider interface. All zone-mapping
// rules — Landuse::CityCenter → Zone::City, etc. — live in one place here.
//
// Construction takes a world-block offset (the WorldTemplate's voxel-earth
// origin, e.g. toronto.py's offset_x/offset_z) so callers can pass world
// coordinates directly to zoneAt() without translating to region space.

#include "logic/zone.h"
#include "logic/zone_provider.h"
#include "server/voxel_earth/landuse.h"

#include <string>
#include <utility>

namespace solarium::voxel_earth {

class OsmZoneProvider final : public solarium::ZoneProvider {
public:
	// Move-construct from a parsed LanduseGrid. `world_offset_*` is the
	// region-to-world block offset (added to region coords by the engine);
	// zoneAt() expects world coordinates and subtracts these internally.
	OsmZoneProvider(LanduseGrid grid,
	                int world_offset_x_blocks,
	                int world_offset_z_blocks)
		: m_grid(std::move(grid)),
		  m_offset_x(world_offset_x_blocks),
		  m_offset_z(world_offset_z_blocks) {}

	solarium::Zone zoneAt(int wx, int wz) const override;

	// Print a histogram of category counts. Called once at world load so the
	// log shows the OSM coverage for this region.
	void logHistogram(const std::string& source_path) const;

	bool empty() const { return m_grid.empty(); }
	int  nx_chunks() const { return m_grid.nx_chunks; }
	int  nz_chunks() const { return m_grid.nz_chunks; }

private:
	LanduseGrid m_grid;
	int         m_offset_x = 0;
	int         m_offset_z = 0;
};

}  // namespace solarium::voxel_earth
