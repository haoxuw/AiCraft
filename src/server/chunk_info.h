#pragma once

/**
 * ChunkInfo — per-chunk block census + sample positions.
 *
 * counts  — exact count per block type, built during chunk generation
 *            and updated incrementally on block changes (O(1)).
 *
 * samples — all world positions per block type, discovered via stride scan:
 *            passes: stride 8 → 4 → 2 → 1
 *            within each pass, Y values sorted by |worldY| (closest to 0 first)
 *            Positions already visited in a prior stride pass are skipped.
 *            Result: blocks near the surface (Y≈0) appear first in samples[],
 *            giving behaviors a good representative set without full scans.
 *            K is unlimited — all block positions are eventually recorded.
 *
 * See docs/29_CHUNK_INFO.md.
 */

#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/types.h"
#include <glm/vec3.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

namespace modcraft {

struct ChunkInfo {
	struct Entry {
		int count = 0;
		std::vector<glm::ivec3> samples;  // all positions, stride-scan order (|Y| smallest first)
	};

	ChunkPos pos;
	std::unordered_map<std::string, Entry> entries;

	// Build ChunkInfo via stride scan after chunk generation.
	// Stride passes: 8 → 4 → 2 → 1. Within each pass, Y values are ordered
	// by |worldY| (closest to absolute Y=0 first). Already-visited positions
	// (from coarser stride passes) are skipped to avoid duplicates.
	//
	// Counts and samples are both populated here — no separate scan needed.
	static ChunkInfo buildStride(ChunkPos cp, const Chunk& chunk, const BlockRegistry& reg) {
		ChunkInfo ci;
		ci.pos = cp;

		// Local Y values sorted by |worldY| = |cp.y * CHUNK_SIZE + ly|, ascending
		// (ties broken by ly value). Surface chunks have ly=4..11 closest to 0;
		// deep chunks have highest ly closest to 0; high mountain chunks lowest ly.
		std::vector<int> ySorted;
		ySorted.reserve(CHUNK_SIZE);
		for (int ly = 0; ly < CHUNK_SIZE; ly++)
			ySorted.push_back(ly);
		std::sort(ySorted.begin(), ySorted.end(), [&](int a, int b) {
			int wa = std::abs(cp.y * CHUNK_SIZE + a);
			int wb = std::abs(cp.y * CHUNK_SIZE + b);
			return wa != wb ? wa < wb : a < b;
		});

		// Track which (lx,ly,lz) have been visited in a prior stride pass
		// to avoid duplicate sample entries. Using a flat bitmask (4096 bits = 512 bytes).
		std::vector<bool> visited(CHUNK_VOLUME, false);
		auto idx = [](int lx, int ly, int lz) { return ly * CHUNK_SIZE * CHUNK_SIZE + lz * CHUNK_SIZE + lx; };

		static const int strides[] = {8, 4, 2, 1};
		for (int stride : strides) {
			for (int ly : ySorted) {
				for (int lz = 0; lz < CHUNK_SIZE; lz += stride) {
					for (int lx = 0; lx < CHUNK_SIZE; lx += stride) {
						int i = idx(lx, ly, lz);
						if (visited[i]) continue;
						visited[i] = true;

						BlockId bid = chunk.get(lx, ly, lz);
						const std::string& typeId = reg.get(bid).string_id;
						auto& e = ci.entries[typeId];
						e.count++;
						e.samples.push_back({
							cp.x * CHUNK_SIZE + lx,
							cp.y * CHUNK_SIZE + ly,
							cp.z * CHUNK_SIZE + lz,
						});
					}
				}
			}
		}
		return ci;
	}

	// Update when a single block changes. O(1) for count; O(samples) to remove old pos.
	// wPos: world position. oldTypeId/newTypeId: type strings.
	void applyBlockChange(glm::ivec3 wPos,
	                      const std::string& oldTypeId,
	                      const std::string& newTypeId) {
		if (oldTypeId == newTypeId) return;

		// Decrement old type
		auto oldIt = entries.find(oldTypeId);
		if (oldIt != entries.end()) {
			oldIt->second.count--;
			auto& s = oldIt->second.samples;
			s.erase(std::remove(s.begin(), s.end(), wPos), s.end());
			if (oldIt->second.count <= 0)
				entries.erase(oldIt);
		}

		// Increment new type — append at end (no re-sort needed for deltas)
		auto& e = entries[newTypeId];
		e.count++;
		e.samples.push_back(wPos);
	}
};

} // namespace modcraft
