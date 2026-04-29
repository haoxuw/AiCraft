#pragma once

// TileCache — lazy-loading reader for VTIL shards.
//
// Owns a directory of .vtil files (~/.voxel/tiles/r<lat>_<lng>/) and pulls
// the appropriate shard into RAM when a chunk inside it is requested.
// Shards stay cached until evicted by the LRU cap. Each shard buckets its
// voxels by (cx_local, cy, cz_local) so chunk-fill iterates only the
// voxels for the requested chunk — no per-cell hash lookups.
//
// Construction takes the shared root and the regional anchor (floor(lat),
// floor(lng)). A TileCache is bound to ONE regional frame, matching what
// the world template was launched with. Mixing frames in one world isn't
// supported — use a different TileCache instance per region instead.

#include "logic/types.h"
#include "server/voxel_earth/region.h"
#include "server/voxel_earth/tile_shard.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace solarium::voxel_earth {

class TileCache {
public:
	TileCache(std::string root, int region_lat, int region_lng,
	          size_t cache_capacity = 32);

	// Bbox of all voxels across every shard touched so far. NOT a true
	// directory scan — opens the smallest amount of metadata it can to
	// answer spawn/preload questions. Returns false if the cache is empty
	// (no shards visited yet AND no shards on disk).
	bool overallBbox(std::array<int32_t, 3>& min,
	                 std::array<int32_t, 3>& max);

	// Visit every voxel inside the inclusive regional-block box
	//   [rxMin..rxMax] × [ryMin..ryMax] × [rzMin..rzMax]
	// Loads any tile shards whose XZ extent intersects the box (typically
	// one tile when the box is a single chunk; up to four if the world's
	// chunk grid is offset so the box straddles a tile boundary).
	template <class F>
	void forEachInBox(int32_t rxMin, int32_t ryMin, int32_t rzMin,
	                  int32_t rxMax, int32_t ryMax, int32_t rzMax, F cb) {
		const int32_t txMin = tile_x_of(rxMin), txMax = tile_x_of(rxMax);
		const int32_t tzMin = tile_z_of(rzMin), tzMax = tile_z_of(rzMax);
		for (int32_t tx = txMin; tx <= txMax; ++tx)
		for (int32_t tz = tzMin; tz <= tzMax; ++tz) {
			Tile* t = getTile(tx, tz);
			if (!t) continue;
			// Per-column buckets — a chunk-aligned box hits ≤1 column.
			for (auto& [colKey, indices] : t->byColumn) {
				const int32_t cxl =  (int32_t)(uint32_t)(colKey & 0xFFFFFFFFu);
				const int32_t czl =  (int32_t)(uint32_t)(colKey >> 32);
				const int32_t col_x0 = (tx * TILE_CHUNK_SIDE + cxl) * CHUNK_SIZE;
				const int32_t col_z0 = (tz * TILE_CHUNK_SIDE + czl) * CHUNK_SIZE;
				if (col_x0 + CHUNK_SIZE - 1 < rxMin || col_x0 > rxMax) continue;
				if (col_z0 + CHUNK_SIZE - 1 < rzMin || col_z0 > rzMax) continue;
				for (size_t idx : indices) {
					const VoxelRecord& v = t->shard.voxels[idx];
					if (v.x < rxMin || v.x > rxMax) continue;
					if (v.y < ryMin || v.y > ryMax) continue;
					if (v.z < rzMin || v.z > rzMax) continue;
					cb(v);
				}
			}
		}
	}

	// Per-column landuse byte for the chunk-column at world (block) (wx, wz).
	// Returns 0 (Unknown) if no shard is loaded for this column.
	uint8_t zoneAtBlock(int32_t wx, int32_t wz);

	bool empty() const { return m_emptyOnDisk && m_tiles.empty(); }
	size_t tilesLoaded() const { return m_tiles.size(); }
	const std::string& root() const { return m_root; }
	int regionLat() const { return m_regionLat; }
	int regionLng() const { return m_regionLng; }

private:
	struct Tile {
		TileShard shard;
		std::unordered_map<uint64_t, std::vector<size_t>> byColumn;
	};

	static uint64_t packTile(int32_t tx, int32_t tz) {
		return (uint64_t)(uint32_t)tx | ((uint64_t)(uint32_t)tz << 32);
	}
	static uint64_t packColumn(int32_t cxl, int32_t czl) {
		return (uint64_t)(uint32_t)cxl | ((uint64_t)(uint32_t)czl << 32);
	}

	Tile* getTile(int32_t tx, int32_t tz);
	void evictIfFull();

	std::string m_root;
	int         m_regionLat;
	int         m_regionLng;
	size_t      m_cap;

	std::unordered_map<uint64_t, std::unique_ptr<Tile>> m_tiles;
	// LRU order: front = most recent.
	std::list<uint64_t> m_lru;
	std::unordered_map<uint64_t, std::list<uint64_t>::iterator> m_lruIter;

	// True once we've scanned the directory and found no shards (so callers
	// don't pay the disk hit on every empty lookup).
	bool m_emptyOnDisk = false;
	bool m_scannedDir  = false;
};

}  // namespace solarium::voxel_earth
