#pragma once

// ZoneProvider — the engine's only path to ask "what kind of place is at
// (wx, wz)?". Strategy interface so each WorldTemplate can plug in its own
// data source without leaking source-specific types into the engine:
//
//   OsmZoneProvider       — reads landuse.json (voxel_earth)
//   WorldgenZoneProvider  — derived from terrain noise + village stamp
//                           (Phase B; not yet implemented)
//   NullZoneProvider      — always Zone::Unknown; default for templates that
//                           have no zone data
//
// Lookup is by world block coords; providers internally floor-divide by
// CHUNK_SIZE if they store data per-chunk-column. Implementations must be
// thread-safe for read (no mutation after construction) so chunk-gen workers
// can call zoneAt() without a lock.

#include "logic/zone.h"

namespace solarium {

class ZoneProvider {
public:
	virtual ~ZoneProvider() = default;
	virtual Zone zoneAt(int wx, int wz) const = 0;
};

class NullZoneProvider final : public ZoneProvider {
public:
	Zone zoneAt(int /*wx*/, int /*wz*/) const override { return Zone::Unknown; }

	static const NullZoneProvider& instance() {
		static NullZoneProvider s;
		return s;
	}
};

}  // namespace solarium
