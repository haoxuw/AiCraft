#pragma once

// Abstract chunk-read interface. Implemented by World (server) and NetworkServer
// (client cache); Renderer/ChunkMesher depend on this so they work on either side.

#include "logic/types.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include <array>
#include <shared_mutex>

namespace civcraft {

// Aggregated lock-perf snapshot. Zero-valued for sources that don't lock.
struct ChunkLockStats {
	uint64_t writerCount   = 0;
	uint64_t writerWaitNs  = 0;   // time blocked trying to acquire unique_lock
	uint64_t writerHoldNs  = 0;   // time held (measures reader stalls on us)
	uint64_t readerCount   = 0;
	uint64_t readerWaitNs  = 0;   // time blocked trying to acquire shared_lock
	uint64_t readerHoldNs  = 0;   // time held (measures writer stalls on us)
};

class ChunkSource {
public:
	virtual ~ChunkSource() = default;

	virtual Chunk* getChunk(ChunkPos pos) = 0;
	virtual Chunk* getChunkIfLoaded(ChunkPos pos) = 0;
	virtual BlockId getBlock(int x, int y, int z) = 0;
	virtual const BlockRegistry& blockRegistry() const = 0;

	// Off-main-thread long scans (e.g. DecideWorker.scan_blocks) must hold
	// a shared_lock on this mutex for the entire scan. Main-thread reads
	// don't lock because writers are on the main thread too. Returns nullptr
	// when no synchronization is needed (server-side World).
	virtual std::shared_mutex* mutex() { return nullptr; }

	// Sample + reset perf counters (no-op for sources that don't lock).
	virtual ChunkLockStats snapshotLockStatsAndReset() { return {}; }

	// Called by readers after a scan to contribute their wait/hold timing.
	// (Writers record themselves inside their scoped lock helper.)
	virtual void recordReaderAcquire(uint64_t /*waitNs*/, uint64_t /*holdNs*/) {}

	// Appearance lookup — default forwards to the underlying chunk's storage.
	// Override only if you need to intercept (e.g. tests). Returns 0 for
	// unloaded chunks or out-of-range coords.
	virtual uint8_t getAppearance(int x, int y, int z) {
		ChunkPos cp{
			(x >= 0) ? x / CHUNK_SIZE : (x - CHUNK_SIZE + 1) / CHUNK_SIZE,
			(y >= 0) ? y / CHUNK_SIZE : (y - CHUNK_SIZE + 1) / CHUNK_SIZE,
			(z >= 0) ? z / CHUNK_SIZE : (z - CHUNK_SIZE + 1) / CHUNK_SIZE,
		};
		Chunk* c = getChunkIfLoaded(cp);
		if (!c) return 0;
		int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		return c->getAppearance(lx, ly, lz);
	}

	// Center + 26 neighbors. Default: 27 getChunkIfLoaded calls.
	virtual std::array<Chunk*, 27> getChunkNeighborhood(ChunkPos center) {
		std::array<Chunk*, 27> result{};
		int i = 0;
		for (int dy = -1; dy <= 1; dy++)
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++)
					result[i++] = getChunkIfLoaded({center.x + dx, center.y + dy, center.z + dz});
		return result;
	}

	// Server-only (generates if needed).
	virtual void ensureChunksAround(ChunkPos center, int radius) { (void)center; (void)radius; }

	virtual void unloadDistantChunks(ChunkPos center, int keepRadius) { (void)center; (void)keepRadius; }
};

} // namespace civcraft
