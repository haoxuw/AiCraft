#pragma once

// VillageRegistry — one entry per village ever sited. Phase 4 of the seats
// overhaul (see docs/28_SEATS_AND_OWNERSHIP.md): each seat gets its own
// village, so placement is no longer a pure function of the world seed.
//
// The registry remembers every village ever sited, including those whose
// owning seat has been garbage-collected ("Despawned"). Despawned records
// still reserve their footprint so fresh sitings can't land on top of old
// ones — world history stays consistent even after GC.
//
// Persisted to {worldPath}/villages.bin by world_save.h.

#include "server/seat_registry.h"
#include <cstdint>
#include <limits>
#include <vector>
#include <glm/glm.hpp>

namespace solarium {

using VillageId = uint32_t;
constexpr VillageId VILLAGE_NONE = 0;

struct VillageRecord {
	enum class Status : uint8_t {
		Live      = 0,  // owning seat is live or offline — footprint sacrosanct
		Despawned = 1,  // seat GC'd long ago, but the footprint still blocks new siting
	};

	VillageId  id       = VILLAGE_NONE;
	SeatId     ownerSeat = SEAT_NONE;
	glm::ivec2 centerXZ  = {0, 0};
	Status     status    = Status::Live;
};

class VillageRegistry {
public:
	// Add a brand-new live village for this seat at this center. Caller is
	// responsible for passing a valid center from the siter.
	VillageRecord& allocate(SeatId ownerSeat, glm::ivec2 centerXZ) {
		m_records.push_back({m_nextId++, ownerSeat, centerXZ, VillageRecord::Status::Live});
		return m_records.back();
	}

	// Replay one record from disk. Does not reassign ids — preserves the id
	// that was persisted, so cross-references (e.g. future "village_id" props
	// on entities) survive a reload.
	void loadEntry(const VillageRecord& r) {
		m_records.push_back(r);
		if (r.id >= m_nextId) m_nextId = r.id + 1;
	}

	// Distance-squared to the nearest *any-status* village. Despawned records
	// count — their footprint is still reserved.
	// Returns a huge number if the registry is empty.
	int64_t nearestDistSqXZ(glm::ivec2 p) const {
		int64_t best = std::numeric_limits<int64_t>::max();
		for (auto& r : m_records) {
			int64_t dx = (int64_t)p.x - r.centerXZ.x;
			int64_t dz = (int64_t)p.y - r.centerXZ.y;
			int64_t d2 = dx*dx + dz*dz;
			if (d2 < best) best = d2;
		}
		return best;
	}

	const std::vector<VillageRecord>& all() const { return m_records; }
	std::vector<VillageRecord>&       all()       { return m_records; }
	size_t size() const { return m_records.size(); }

	VillageRecord* find(VillageId id) {
		for (auto& r : m_records) if (r.id == id) return &r;
		return nullptr;
	}

	VillageRecord* findBySeat(SeatId seat) {
		// Returns the *first* Live record owned by this seat. One seat → one
		// village in v1; this is a linear scan because the registry is small.
		for (auto& r : m_records) {
			if (r.ownerSeat == seat && r.status == VillageRecord::Status::Live)
				return &r;
		}
		return nullptr;
	}

private:
	std::vector<VillageRecord> m_records;
	VillageId                  m_nextId = 1;  // 0 is the sentinel
};

} // namespace solarium
