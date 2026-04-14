#pragma once

/**
 * ChunkInfo — lightweight per-chunk block census (counts only).
 *
 * Each entry maps a block type ID to the exact count of that type
 * in the chunk. No sample positions — use real chunk data for that.
 *
 * hasAir flag: true if the chunk contains any air blocks. Used by
 * the two-tier chunk streaming system to identify surface chunks.
 */

#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/types.h"
#include <unordered_map>
#include <string>

namespace civcraft {

struct ChunkInfo {
	struct Entry {
		int count = 0;
	};

	ChunkPos pos;
	bool hasAir = false;  // true if this chunk contains any air blocks
	std::unordered_map<std::string, Entry> entries;

	// Build ChunkInfo by scanning all blocks in a chunk. O(CHUNK_VOLUME).
	static ChunkInfo build(ChunkPos cp, const Chunk& chunk, const BlockRegistry& reg) {
		ChunkInfo ci;
		ci.pos = cp;
		for (int ly = 0; ly < CHUNK_SIZE; ly++)
			for (int lz = 0; lz < CHUNK_SIZE; lz++)
				for (int lx = 0; lx < CHUNK_SIZE; lx++) {
					BlockId bid = chunk.get(lx, ly, lz);
					const std::string& typeId = reg.get(bid).string_id;
					ci.entries[typeId].count++;
					if (bid == BLOCK_AIR || typeId == "air" || typeId.empty())
						ci.hasAir = true;
				}
		return ci;
	}

	// Update when a single block changes. O(1).
	void applyBlockChange(glm::ivec3 wPos,
	                      const std::string& oldTypeId,
	                      const std::string& newTypeId) {
		if (oldTypeId == newTypeId) return;

		auto oldIt = entries.find(oldTypeId);
		if (oldIt != entries.end()) {
			oldIt->second.count--;
			if (oldIt->second.count <= 0)
				entries.erase(oldIt);
		}

		entries[newTypeId].count++;

		// Update hasAir flag
		if (newTypeId == "air" || newTypeId.empty())
			hasAir = true;
		// Note: hasAir stays true even if last air removed (conservative)
	}
};

} // namespace civcraft
