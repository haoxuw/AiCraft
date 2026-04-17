#pragma once

// Per-chunk block type counts + hasAir flag. No sample positions.
// hasAir drives two-tier streaming (surface-chunk detection).

#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/types.h"
#include <unordered_map>
#include <string>

namespace civcraft {

struct ChunkInfo {
	struct Entry {
		int count = 0;
	};

	ChunkPos pos;
	bool hasAir = false;
	std::unordered_map<std::string, Entry> entries;

	// O(CHUNK_VOLUME) scan.
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

	// O(1) single-block update.
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

		// Conservative: hasAir stays true even if last air block was removed.
		if (newTypeId == "air" || newTypeId.empty())
			hasAir = true;
	}
};

} // namespace civcraft
