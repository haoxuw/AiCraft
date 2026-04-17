#pragma once

// Pure block lookup over a ChunkInfo census + loaded chunk cache.

#include "logic/types.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/constants.h"
#include "server/behavior.h"  // BlockSample

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace civcraft::block_search {

// Per-chunk census: count per block type. run() is templated on the value type.
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

// Returns up to maxResults matches sorted by distance ascending.
// InfoMap: unordered_map<ChunkPos, T, ChunkPosHash> where T has `entries` map.
template <typename InfoMap>
inline std::vector<BlockSample> run(const Options& opt,
                                    const InfoMap& chunkInfoCache,
                                    const ChunkMap& chunks,
                                    const BlockRegistry& blocks,
                                    const EnsureLoadedFn& ensureChunkLoaded) {
	BlockId targetBid = blocks.getId(opt.typeId);
	if (targetBid == BLOCK_AIR && opt.typeId != "air") return {};

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

	// Distance-first, ignoring count: agent prefers nearby lone tree over far dense forest.
	std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
		return a.dist < b.dist;
	});

	// Stop at first candidate yielding results to avoid scanning distant chunks.
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

		if (!result.empty()) break;
	}

	std::sort(result.begin(), result.end(),
	          [](const BlockSample& a, const BlockSample& b) { return a.distance < b.distance; });
	return result;
}

} // namespace civcraft::block_search
