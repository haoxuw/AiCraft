#pragma once

// Client's single ChunkSource: owns chunk storage, block registry, annotations.
// Populated by NetworkServer from S_CHUNK/S_BLOCK/S_ANNOTATION_SET.
// See docs/10_CLIENT_SERVER_PHYSICS.md.

#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/chunk_source.h"
#include "logic/annotation.h"
#include "logic/physics.h"
#include "server/entity_manager.h"
#include "server/builtin.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <utility>

namespace solarium {

// LocalWorld chunk-store rule (see docs/10_CLIENT_SERVER_PHYSICS.md):
//   * Writers (S_CHUNK / S_BLOCK / S_CHUNK_EVICT / S_ANNOTATION_SET) run on
//     the main thread and take a unique_lock inside each mutator.
//   * Main-thread readers (physics, mesher snapshot, render, raycast) don't
//     lock — they can't race with the main-thread writers.
//   * Off-main-thread readers (DecideWorker scan_blocks) must grab a
//     shared_lock on mutex() for the ENTIRE scan, not per-block; otherwise a
//     removeChunk mid-loop frees the Chunk they're walking.
class LocalWorld : public ChunkSource {
public:
	LocalWorld() {
		registerAllBuiltins(m_blocks, m_entityDefs);
		// Resolve default-fill BlockIds (Air, Dirt) so getBlock() falls back
		// to dirt for unloaded underground chunks instead of leaving holes.
		setDefaults(m_blocks);
	}

	// --- ChunkSource overrides ----------------------------------------------
	std::shared_mutex* mutex() override { return &m_mtx; }

	ChunkLockStats snapshotLockStatsAndReset() override {
		ChunkLockStats s;
		s.writerCount  = m_wCount.exchange(0, std::memory_order_relaxed);
		s.writerWaitNs = m_wWaitNs.exchange(0, std::memory_order_relaxed);
		s.writerHoldNs = m_wHoldNs.exchange(0, std::memory_order_relaxed);
		s.readerCount  = m_rCount.exchange(0, std::memory_order_relaxed);
		s.readerWaitNs = m_rWaitNs.exchange(0, std::memory_order_relaxed);
		s.readerHoldNs = m_rHoldNs.exchange(0, std::memory_order_relaxed);
		return s;
	}

	void recordReaderAcquire(uint64_t waitNs, uint64_t holdNs) override {
		m_rCount.fetch_add(1, std::memory_order_relaxed);
		m_rWaitNs.fetch_add(waitNs, std::memory_order_relaxed);
		m_rHoldNs.fetch_add(holdNs, std::memory_order_relaxed);
	}

	Chunk* getChunk(ChunkPos pos) override {
		auto it = m_chunks.find(pos);
		return it != m_chunks.end() ? it->second.get() : nullptr;
	}

	Chunk* getChunkIfLoaded(ChunkPos pos) override { return getChunk(pos); }

	BlockId getBlock(int x, int y, int z) override {
		auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
		ChunkPos cp = {div(x, CHUNK_SIZE), div(y, CHUNK_SIZE), div(z, CHUNK_SIZE)};
		Chunk* c = getChunk(cp);
		if (!c) return defaultBlock(cp.y);   // unloaded → AIR above 0, DIRT below
		int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		return c->get(lx, ly, lz);
	}

	const BlockRegistry& blockRegistry() const override { return m_blocks; }

	// --- Writers (main thread; take unique_lock) ----------------------------
	void setChunk(ChunkPos pos, std::unique_ptr<Chunk> chunk) {
		ScopedWrite lk(*this);
		m_chunks[pos] = std::move(chunk);
	}

	void removeChunk(ChunkPos pos) {
		ScopedWrite lk(*this);
		m_chunks.erase(pos);
		m_annotations.erase(pos);
	}

	void setBlock(int x, int y, int z, BlockId bid) {
		ScopedWrite lk(*this);
		auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
		ChunkPos cp = {div(x, CHUNK_SIZE), div(y, CHUNK_SIZE), div(z, CHUNK_SIZE)};
		auto it = m_chunks.find(cp);
		if (it != m_chunks.end()) {
			int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			it->second->set(lx, ly, lz, bid);
		}
	}

	void setAppearance(int x, int y, int z, uint8_t idx) {
		ScopedWrite lk(*this);
		auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
		ChunkPos cp = {div(x, CHUNK_SIZE), div(y, CHUNK_SIZE), div(z, CHUNK_SIZE)};
		auto it = m_chunks.find(cp);
		if (it != m_chunks.end()) {
			int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
			it->second->setAppearance(lx, ly, lz, idx);
		}
	}

	void setAnnotations(ChunkPos cp,
	                     std::vector<std::pair<glm::ivec3, Annotation>> anns) {
		ScopedWrite lk(*this);
		if (anns.empty())
			m_annotations.erase(cp);
		else
			m_annotations[cp] = std::move(anns);
	}

	void addAnnotation(ChunkPos cp, glm::ivec3 pos, const Annotation& ann) {
		ScopedWrite lk(*this);
		m_annotations[cp].push_back({pos, ann});
	}

	void removeAnnotation(ChunkPos cp, glm::ivec3 pos) {
		ScopedWrite lk(*this);
		auto it = m_annotations.find(cp);
		if (it == m_annotations.end()) return;
		auto& vec = it->second;
		vec.erase(std::remove_if(vec.begin(), vec.end(),
			[&](const auto& p) { return p.first == pos; }),
			vec.end());
		if (vec.empty()) m_annotations.erase(it);
	}

	const std::vector<std::pair<glm::ivec3, Annotation>>*
	annotationsForChunk(ChunkPos cp) const {
		auto it = m_annotations.find(cp);
		return it != m_annotations.end() ? &it->second : nullptr;
	}

	// Unloaded chunks return 0 (air).
	BlockSolidFn solidFn() const {
		return [this](int x, int y, int z) -> float {
			BlockId bid = const_cast<LocalWorld*>(this)->getBlock(x, y, z);
			const auto& bd = m_blocks.get(bid);
			return bd.solid ? bd.collision_height : 0.0f;
		};
	}

	BlockRegistry& blockRegistryMut() { return m_blocks; }
	EntityManager& entityDefs() { return m_entityDefs; }
	const EntityManager& entityDefs() const { return m_entityDefs; }
	size_t chunkCount() const { return m_chunks.size(); }

private:
	// RAII: times wait-to-acquire (from ctor entry to lock held) and hold
	// duration (from acquire to dtor), bumping atomic counters on release.
	struct ScopedWrite {
		LocalWorld& w;
		std::unique_lock<std::shared_mutex> lk;
		std::chrono::steady_clock::time_point held;
		explicit ScopedWrite(LocalWorld& wr) : w(wr) {
			auto t0 = std::chrono::steady_clock::now();
			lk = std::unique_lock<std::shared_mutex>(wr.m_mtx);
			held = std::chrono::steady_clock::now();
			uint64_t waitNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
				held - t0).count();
			w.m_wCount.fetch_add(1, std::memory_order_relaxed);
			w.m_wWaitNs.fetch_add(waitNs, std::memory_order_relaxed);
		}
		~ScopedWrite() {
			auto t1 = std::chrono::steady_clock::now();
			uint64_t holdNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
				t1 - held).count();
			w.m_wHoldNs.fetch_add(holdNs, std::memory_order_relaxed);
		}
	};

	mutable std::shared_mutex m_mtx;
	mutable std::atomic<uint64_t> m_wCount{0}, m_wWaitNs{0}, m_wHoldNs{0};
	mutable std::atomic<uint64_t> m_rCount{0}, m_rWaitNs{0}, m_rHoldNs{0};

	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunks;
	std::unordered_map<ChunkPos,
	                    std::vector<std::pair<glm::ivec3, Annotation>>,
	                    ChunkPosHash> m_annotations;
	BlockRegistry m_blocks;
	EntityManager m_entityDefs;
};

} // namespace solarium
