#pragma once

// SeatRegistry — maps persistent client UUID ↔ stable SeatId. Seats are the
// durable ownership handle across sessions: entities belong to a Seat, not a
// transient entity id. See docs/28_SEATS_AND_OWNERSHIP.md.
//
// SeatId 0 is a sentinel ("no seat"). The first real seat is 1.
//
// Persisted to {worldPath}/seats.bin by world_save.h. In-memory only for
// ephemeral servers (e.g. TestServer, headless tests).

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace solarium {

using SeatId = uint32_t;
constexpr SeatId SEAT_NONE = 0;

class SeatRegistry {
public:
	struct ClaimResult {
		SeatId id = SEAT_NONE;
		bool   isNew = false;
	};

	// Returns the existing seat for this uuid, or allocates a new one.
	ClaimResult claim(const std::string& uuid) {
		auto it = m_seats.find(uuid);
		if (it != m_seats.end()) return {it->second, false};
		SeatId id = m_nextSeatId++;
		m_seats[uuid] = id;
		return {id, true};
	}

	// Read-only lookup; returns SEAT_NONE if this uuid has never claimed.
	SeatId lookup(const std::string& uuid) const {
		auto it = m_seats.find(uuid);
		return it != m_seats.end() ? it->second : SEAT_NONE;
	}

	size_t size() const { return m_seats.size(); }

	// Persistence hooks. loadFromDisk replays what saveWorld wrote; caller is
	// responsible for calling it before any claim() so nextSeatId stays correct.
	using Map = std::unordered_map<std::string, SeatId>;
	const Map& all() const { return m_seats; }

	void loadEntry(const std::string& uuid, SeatId id) {
		if (id == SEAT_NONE || uuid.empty()) return;
		m_seats[uuid] = id;
		if (id >= m_nextSeatId) m_nextSeatId = id + 1;
	}

private:
	Map     m_seats;          // uuid → seatId
	SeatId  m_nextSeatId = 1; // 0 is sentinel
};

} // namespace solarium
