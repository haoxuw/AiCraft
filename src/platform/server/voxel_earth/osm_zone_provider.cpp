#include "server/voxel_earth/osm_zone_provider.h"

#include <cstdio>

namespace solarium::voxel_earth {

namespace {

// One place that maps the Python-side landuse categories onto the engine's
// Zone enum. If you add a Landuse value, add it here too.
solarium::Zone landuse_to_zone(Landuse l) {
	switch (l) {
		case Landuse::Unknown:     return solarium::Zone::Unknown;
		case Landuse::CityCenter:  return solarium::Zone::City;
		case Landuse::Residential: return solarium::Zone::Town;
		case Landuse::Industrial:  return solarium::Zone::Industrial;
		case Landuse::Forest:      return solarium::Zone::Wilderness;
		case Landuse::Park:        return solarium::Zone::Park;
		case Landuse::Farmland:    return solarium::Zone::Farmland;
		case Landuse::Water:       return solarium::Zone::Water;
		case Landuse::Beach:       return solarium::Zone::Beach;
	}
	return solarium::Zone::Unknown;
}

}  // namespace

solarium::Zone OsmZoneProvider::zoneAt(int wx, int wz) const {
	if (m_grid.empty()) return solarium::Zone::Unknown;
	const int rx = wx - m_offset_x;
	const int rz = wz - m_offset_z;
	return landuse_to_zone(m_grid.at_block(rx, rz));
}

void OsmZoneProvider::logHistogram(const std::string& source_path) const {
	if (m_grid.empty()) return;
	int hist[9] = {0};
	for (uint8_t b : m_grid.data) if (b < 9) hist[b]++;
	printf("[VoxelEarth] zones %dx%d chunks loaded from %s\n",
	       m_grid.nx_chunks, m_grid.nz_chunks, source_path.c_str());
	for (int i = 0; i < 9; ++i) {
		if (hist[i] == 0) continue;
		printf("              %-12s %d\n",
		       zoneName(landuse_to_zone(static_cast<Landuse>(i))),
		       hist[i]);
	}
}

}  // namespace solarium::voxel_earth
