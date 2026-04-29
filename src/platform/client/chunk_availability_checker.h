#pragma once

// ChunkAvailabilityChecker — client-side invariant guard.
//
// Watches the chunks the player is *currently expected to be standing
// inside or next to* and asserts when one stays unloaded longer than a
// grace window. Intended to catch a regression where the streaming
// pipeline has been silently dropping chunks (server skipping a chunk
// it shouldn't have, distance-priority queue starving, tile-cache
// returning empty results, …) — instead of the symptom showing up as
// a visible hole the player walks into, it shows up as an in-game
// assert + a dump describing exactly which chunk is missing and what
// loaded around it.
//
// Usage (one instance per Game, ticked from the play-loop):
//
//     ChunkAvailabilityChecker m_chunkChecker { /*radius=*/2,
//                                                /*grace=*/3.0f };
//     ...
//     m_chunkChecker.tick(playerChunkPos, m_server->chunks(), dt);
//
// `radius` is the chunk-coord distance we *require* to be loaded around
// the player (for collision + adjacent rendering). 2 covers the entity's
// 3³ physics neighbourhood plus one ring of margin so a fast-moving
// player can't skip through a hole on a 60 Hz tick. Grace is in seconds
// — chunks have to be in the "expected but missing" set for that long
// before we shout, to absorb normal stream-in latency.
//
// On a violation: writes a diagnostic to stderr (player pos + chunk
// coord + per-(cx,cz) loaded-grid for each cy in the radius) and trips
// `assert(false)` so a Debug build halts immediately. After the dump
// the chunk is suppressed for the rest of the session — no log spam.
//
// OOP-shaped on purpose: the checker owns its own state (last-seen
// timestamps, asserted set), the Game just hands it the player pos and
// a ChunkSource. No globals, no #ifdefs, no debug-only conditional
// compile — `assert()` self-disables in NDEBUG.

#include "logic/types.h"

#include <unordered_map>
#include <unordered_set>

namespace solarium {

class ChunkSource;

class ChunkAvailabilityChecker {
public:
	ChunkAvailabilityChecker(int requiredRadius = 2, float graceSec = 3.0f)
		: m_radius(requiredRadius), m_grace(graceSec) {}

	// Toggle the watchdog on/off. Off during connecting / loading-screen,
	// on once the player is actually playing — see Game::enterPlaying.
	void setEnabled(bool on) { m_enabled = on; if (!on) reset(); }
	bool enabled() const     { return m_enabled; }

	// Hard-clear all bookkeeping. Call on world swap / reconnect / death
	// so stale "missing for 8s" entries from the old world don't fire.
	void reset();

	// Drive the watchdog. `playerCp` = chunk that contains the player's
	// feet. `now` = monotonic seconds since the checker was constructed
	// (we add `dt` ourselves so callers don't have to track it).
	void tick(ChunkPos playerCp, ChunkSource& world, float dt);

	int   radius() const { return m_radius; }
	float grace()  const { return m_grace; }
	size_t suppressedCount() const { return m_asserted.size(); }

private:
	struct PosHash {
		size_t operator()(ChunkPos p) const noexcept {
			return std::hash<int>()(p.x) ^
			       (std::hash<int>()(p.y) << 1) ^
			       (std::hash<int>()(p.z) << 2);
		}
	};

	void dumpAndAssert(ChunkPos missing, ChunkPos playerCp, ChunkSource& world,
	                   float missingFor) const;

	int   m_radius   = 2;
	float m_grace    = 3.0f;
	bool  m_enabled  = false;
	float m_now      = 0.0f;

	// chunk-pos → wall-clock time we first noticed it as expected but unloaded.
	std::unordered_map<ChunkPos, float, PosHash> m_firstSeen;
	// chunk-pos → already shouted about; suppress further dumps.
	std::unordered_set<ChunkPos, PosHash>        m_asserted;
};

}  // namespace solarium
