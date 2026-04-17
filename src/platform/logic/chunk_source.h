#pragma once

// Abstract chunk-read interface. Implemented by World (server) and NetworkServer
// (client cache); Renderer/ChunkMesher depend on this so they work on either side.

#include "logic/types.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include <array>

namespace civcraft {

class ChunkSource {
public:
	virtual ~ChunkSource() = default;

	virtual Chunk* getChunk(ChunkPos pos) = 0;
	virtual Chunk* getChunkIfLoaded(ChunkPos pos) = 0;
	virtual BlockId getBlock(int x, int y, int z) = 0;
	virtual const BlockRegistry& blockRegistry() const = 0;

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
