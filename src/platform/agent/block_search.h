#pragma once

/**
 * block_search — pure, unit-testable block lookup over a ChunkInfo index.
 *
 * The agent client holds per-chunk census data (counts only) and a cache of
 * loaded chunks. scan_blocks() from Python turns into a call here: pick
 * candidate chunks using the census, then scan real chunk data to find
 * concrete block positions.
 *
 * This module is deliberately free of AgentClient state so it can be
 * exercised directly in unit tests with synthetic inputs.
 */

#include "shared/types.h"
#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/constants.h"
#include "server/behavior.h"  // BlockSample

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace civcraft::block_search {

// Per-chunk census: exact count per block type id in a chunk.
// The real agent cache (AgentChunkInfo) and unit tests both satisfy this
// shape — anything with an `entries` map of {typeId → {count}} works because
// run() is templated on the info-map value type.
struct ChunkCensus {
	struct Entry { int count = 0; };
	std::unordered_map<std::string, Entry> entries;
};

using ChunkMap       = std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash>;
using EnsureLoadedFn = std::function<void(ChunkPos)>;

struct Options {
	std::string typeId;
	glm::vec3   searchOrigin;
	float       maxDist;
	int         maxResults;
};

// Find blocks of Options::typeId near Options::searchOrigin.
// Returns up to maxResults matches sorted by distance ascending.
// InfoMap must be unordered_map<ChunkPos, T, ChunkPosHash> where T has an
// `entries` field compatible with ChunkCensus::entries (any subclass works).
template <typename InfoMap>
inline std::vector<BlockSample> run(const Options& opt,
                                    const InfoMap& chunkInfoCache,
                                    const ChunkMap& chunks,
                                    const BlockRegistry& blocks,
                                    const EnsureLoadedFn& ensureChunkLoaded) {
	BlockId targetBid = blocks.getId(opt.typeId);
	if (targetBid == BLOCK_AIR && opt.typeId != "air") return {};

	// Step 1: pick candidate chunks from the census index.
	struct Candidate { ChunkPos pos; float dist; int count; };
	std::vector<Candidate> candidates;
	for (auto& [cp, ci] : chunkInfoCache) {
		auto eIt = ci.entries.find(opt.typeId);
		if (eIt == ci.entries.end() || eIt->second.count <= 0) continue;
		glm::vec3 cc = {
			cp.x * (float)CHUNK_SIZE + 8.0f,
			cp.y * (float)CHUNK_SIZE + 8.0f,
			cp.z * (float)CHUNK_SIZE + 8.0f,
		};
		float d = glm::length(opt.searchOrigin - cc);
		if (d > opt.maxDist + 16.0f) continue; // +16 ≈ chunk diagonal slack
		candidates.push_back({cp, d, eIt->second.count});
	}

	// Distance-first ordering: the nearest non-empty chunk wins.
	// This intentionally ignores count so the agent doesn't walk past a
	// nearby lone tree to reach a far but denser forest. Performance-wise
	// we accept "a relatively close block" rather than THE closest one.
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
		return a.dist < b.dist;
	});

	// Step 2: scan real chunk data for matching cells. Stop at the first
	// candidate chunk that yields any results — that's "relatively close"
	// enough, and it avoids loading/scanning more distant chunks.
	std::vector<BlockSample> result;
	for (auto& cand : candidates) {
		if (chunks.find(cand.pos) == chunks.end()) {
			if (ensureChunkLoaded) ensureChunkLoaded(cand.pos);
		}
		auto chunkIt = chunks.find(cand.pos);
		if (chunkIt == chunks.end() || !chunkIt->second) continue;
		const Chunk& chunk = *chunkIt->second;

		for (int ly = CHUNK_SIZE - 1; ly >= 0 && (int)result.size() < opt.maxResults; ly--) {
			for (int lz = 0; lz < CHUNK_SIZE && (int)result.size() < opt.maxResults; lz++) {
				for (int lx = 0; lx < CHUNK_SIZE && (int)result.size() < opt.maxResults; lx++) {
					if (chunk.get(lx, ly, lz) != targetBid) continue;
					int wx = cand.pos.x * CHUNK_SIZE + lx;
					int wy = cand.pos.y * CHUNK_SIZE + ly;
					int wz = cand.pos.z * CHUNK_SIZE + lz;
					float dist = glm::length(opt.searchOrigin -
						glm::vec3(wx + 0.5f, wy + 0.5f, wz + 0.5f));
					if (dist <= opt.maxDist)
						result.push_back({opt.typeId, wx, wy, wz, dist});
				}
			}
		}

		// First non-empty chunk wins — don't load more chunks than needed.
		if (!result.empty()) break;
	}

	std::sort(result.begin(), result.end(),
	          [](const BlockSample& a, const BlockSample& b) { return a.distance < b.distance; });
	return result;
}

} // namespace civcraft::block_search
