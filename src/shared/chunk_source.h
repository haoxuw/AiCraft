#pragma once

/**
 * ChunkSource — abstract interface for reading chunk data.
 *
 * Both World (server) and ClientWorld (client) implement this.
 * The Renderer and ChunkMesher use this interface instead of
 * depending on World directly, enabling the network client to
 * render terrain from cached server data.
 */

#include "shared/types.h"
#include "shared/chunk.h"
#include "shared/block_registry.h"
#include <array>

namespace agentica {

class ChunkSource {
public:
	virtual ~ChunkSource() = default;

	virtual Chunk* getChunk(ChunkPos pos) = 0;
	virtual Chunk* getChunkIfLoaded(ChunkPos pos) = 0;
	virtual BlockId getBlock(int x, int y, int z) = 0;
	virtual const BlockRegistry& blockRegistry() const = 0;

	// Batch lookup: fetch center chunk + all 26 neighbors.
	// Default implementation calls getChunkIfLoaded 27 times.
	virtual std::array<Chunk*, 27> getChunkNeighborhood(ChunkPos center) {
		std::array<Chunk*, 27> result{};
		int i = 0;
		for (int dy = -1; dy <= 1; dy++)
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++)
					result[i++] = getChunkIfLoaded({center.x + dx, center.y + dy, center.z + dz});
		return result;
	}

	// Ensure chunks exist around a position (generates if needed — server only)
	virtual void ensureChunksAround(ChunkPos center, int radius) { (void)center; (void)radius; }

	// Unload distant chunks
	virtual void unloadDistantChunks(ChunkPos center, int keepRadius) { (void)center; (void)keepRadius; }
};

} // namespace agentica
