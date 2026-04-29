#include "server/voxel_earth/tile_cache.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace solarium::voxel_earth {

namespace {
inline std::string regionDir(const std::string& root, int rlat, int rlng) {
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%s/r%d_%d", root.c_str(), rlat, rlng);
	return buf;
}
}  // namespace

TileCache::TileCache(std::string root, int region_lat, int region_lng,
                     size_t cache_capacity)
	: m_root(std::move(root)),
	  m_regionLat(region_lat),
	  m_regionLng(region_lng),
	  m_cap(cache_capacity) {}

TileCache::Tile* TileCache::getTile(int32_t tx, int32_t tz) {
	const uint64_t key = packTile(tx, tz);
	auto it = m_tiles.find(key);
	if (it != m_tiles.end()) {
		// LRU touch.
		auto litIt = m_lruIter.find(key);
		if (litIt != m_lruIter.end()) {
			m_lru.erase(litIt->second);
			m_lru.push_front(key);
			litIt->second = m_lru.begin();
		}
		return it->second.get();
	}

	// Miss: try to load from disk. If the region dir is empty (already
	// scanned), short-circuit to avoid stat per query.
	if (m_emptyOnDisk) return nullptr;

	const std::string path = shard_path(m_root, m_regionLat, m_regionLng, tx, tz);
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		// First-miss diagnostic: do we have ANY shards in this region?
		if (!m_scannedDir) {
			m_scannedDir = true;
			const std::string dir = regionDir(m_root, m_regionLat, m_regionLng);
			std::error_code ec2;
			std::filesystem::directory_iterator d(dir, ec2);
			if (ec2 || std::filesystem::begin(d) == std::filesystem::end(d)) {
				m_emptyOnDisk = true;
			}
		}
		return nullptr;
	}

	auto t = std::make_unique<Tile>();
	std::string err;
	if (!read_shard(path, t->shard, &err)) {
		std::fprintf(stderr, "[TileCache] read_shard failed: %s\n", err.c_str());
		return nullptr;
	}
	// Index voxels by (cx_local, cz_local).
	t->byColumn.reserve(TILE_COLUMNS);
	for (size_t i = 0; i < t->shard.voxels.size(); ++i) {
		const auto& v = t->shard.voxels[i];
		const int32_t cxl = chunk_local_x(v.x);
		const int32_t czl = chunk_local_z(v.z);
		t->byColumn[packColumn(cxl, czl)].push_back(i);
	}

	evictIfFull();
	m_lru.push_front(key);
	m_lruIter[key] = m_lru.begin();
	auto* raw = t.get();
	m_tiles.emplace(key, std::move(t));
	return raw;
}

void TileCache::evictIfFull() {
	while (m_tiles.size() >= m_cap && !m_lru.empty()) {
		const uint64_t k = m_lru.back();
		m_lru.pop_back();
		m_lruIter.erase(k);
		m_tiles.erase(k);
	}
}

bool TileCache::overallBbox(std::array<int32_t, 3>& min,
                            std::array<int32_t, 3>& max) {
	bool any = false;
	for (auto& [_, t] : m_tiles) {
		const auto& sh = t->shard;
		if (!any) {
			min = sh.bbox_min;
			max = sh.bbox_max;
			any = true;
		} else {
			for (int i = 0; i < 3; ++i) {
				if (sh.bbox_min[i] < min[i]) min[i] = sh.bbox_min[i];
				if (sh.bbox_max[i] > max[i]) max[i] = sh.bbox_max[i];
			}
		}
	}
	if (any) return true;
	// Cache is empty — try to read any single shard's bbox by walking the
	// region dir. This is best-effort; spawn picker really wants a tighter
	// answer once chunks start streaming.
	const std::string dir = regionDir(m_root, m_regionLat, m_regionLng);
	std::error_code ec;
	for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
		if (!e.is_regular_file()) continue;
		int32_t tx = 0, tz = 0;
		if (!parse_shard_filename(e.path().string(), tx, tz)) continue;
		TileShard sh;
		std::string err;
		if (!read_shard(e.path().string(), sh, &err)) continue;
		min = sh.bbox_min;
		max = sh.bbox_max;
		any = true;
		break;
	}
	return any;
}

uint8_t TileCache::zoneAtBlock(int32_t wx, int32_t wz) {
	const int32_t tx = tile_x_of(wx);
	const int32_t tz = tile_z_of(wz);
	Tile* t = getTile(tx, tz);
	if (!t) return 0;
	const int32_t cxl = chunk_local_x(wx);
	const int32_t czl = chunk_local_z(wz);
	const size_t idx = (size_t)czl * TILE_CHUNK_SIDE + (size_t)cxl;
	if (idx >= TILE_COLUMNS) return 0;
	return t->shard.zones[idx];
}

}  // namespace solarium::voxel_earth
