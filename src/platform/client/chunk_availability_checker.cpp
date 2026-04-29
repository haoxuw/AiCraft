#include "client/chunk_availability_checker.h"

#include "logic/chunk.h"
#include "logic/chunk_source.h"

#include <cassert>
#include <cstdio>

namespace solarium {

void ChunkAvailabilityChecker::reset() {
	m_firstSeen.clear();
	m_asserted.clear();
}

void ChunkAvailabilityChecker::tick(ChunkPos playerCp, ChunkSource& world,
                                    float dt) {
	if (!m_enabled) return;
	m_now += dt;

	// Walk the (2*radius+1)³ cube around the player. For a chunk that's
	// loaded, drop any tracked first-seen — it's healthy. For one that's
	// not loaded, mark / promote / fire.
	const int R = m_radius;
	for (int dy = -R; dy <= R; ++dy)
	for (int dz = -R; dz <= R; ++dz)
	for (int dx = -R; dx <= R; ++dx) {
		ChunkPos cp { playerCp.x + dx, playerCp.y + dy, playerCp.z + dz };
		Chunk* c = world.getChunkIfLoaded(cp);
		if (c) {
			m_firstSeen.erase(cp);
			continue;
		}
		// Sky chunks (default = AIR) safely skip the wire — the client's
		// defaultBlock(cy) fallback yields the same visual (transparent
		// sky) and physics (entity can fall through). Don't flag those.
		// Solid defaults (DIRT under cy<0) DO need streaming so the
		// mesher draws walls when the player digs into them.
		if (world.defaultBlock(cp.y) == BLOCK_AIR) {
			m_firstSeen.erase(cp);
			continue;
		}
		auto it = m_firstSeen.find(cp);
		if (it == m_firstSeen.end()) {
			m_firstSeen[cp] = m_now;
			continue;
		}
		const float missingFor = m_now - it->second;
		if (missingFor < m_grace) continue;
		if (m_asserted.count(cp)) continue;   // shout once per (session, chunk)
		dumpAndAssert(cp, playerCp, world, missingFor);
		m_asserted.insert(cp);
	}
}

void ChunkAvailabilityChecker::dumpAndAssert(ChunkPos missing,
                                             ChunkPos playerCp,
                                             ChunkSource& world,
                                             float missingFor) const {
	std::fprintf(stderr,
		"\n[ChunkAvailability] FAILED — chunk %s within radius %d of player\n"
		"  player_chunk = (%d, %d, %d)\n"
		"  missing      = (%d, %d, %d)\n"
		"  delta        = (%d, %d, %d)\n"
		"  missing_for  = %.2fs (grace = %.2fs)\n",
		"unloaded for >grace",
		m_radius,
		playerCp.x, playerCp.y, playerCp.z,
		missing.x,  missing.y,  missing.z,
		missing.x - playerCp.x, missing.y - playerCp.y, missing.z - playerCp.z,
		missingFor, m_grace);

	// Per-cy slice grid: '.' loaded, '#' missing, '+' = the offending chunk.
	const int R = m_radius;
	for (int dy = -R; dy <= R; ++dy) {
		const int cy = playerCp.y + dy;
		std::fprintf(stderr, "  cy=%d:\n", cy);
		for (int dz = -R; dz <= R; ++dz) {
			std::fprintf(stderr, "    ");
			for (int dx = -R; dx <= R; ++dx) {
				ChunkPos cp { playerCp.x + dx, cy, playerCp.z + dz };
				const bool loaded = world.getChunkIfLoaded(cp) != nullptr;
				const bool here   = (cp == missing);
				const char ch = here ? '+' : (loaded ? '.' : '#');
				std::fputc(ch, stderr);
			}
			std::fprintf(stderr, "  (dz=%d)\n", dz);
		}
	}
	std::fprintf(stderr, "  legend: . loaded   # missing   + offending chunk\n");
	std::fflush(stderr);

	// Tripwire — a Debug build halts here with a clear stack. Release
	// builds (NDEBUG) just keep the diagnostic above.
	assert(!"ChunkAvailability: chunk missing past grace within player radius");
}

}  // namespace solarium
