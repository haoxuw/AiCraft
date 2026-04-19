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
#include <memory>
#include <unordered_map>
#include <vector>
#include <utility>

namespace civcraft {

class LocalWorld : public ChunkSource {
public:
	LocalWorld() {
		registerAllBuiltins(m_blocks, m_entityDefs);
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
		if (!c) return BLOCK_AIR;
		int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		return c->get(lx, ly, lz);
	}

	const BlockRegistry& blockRegistry() const override { return m_blocks; }

	void setChunk(ChunkPos pos, std::unique_ptr<Chunk> chunk) {
		m_chunks[pos] = std::move(chunk);
	}

	void removeChunk(ChunkPos pos) {
		m_chunks.erase(pos);
		m_annotations.erase(pos);
	}

	void setBlock(int x, int y, int z, BlockId bid) {
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
		if (anns.empty())
			m_annotations.erase(cp);
		else
			m_annotations[cp] = std::move(anns);
	}

	void addAnnotation(ChunkPos cp, glm::ivec3 pos, const Annotation& ann) {
		m_annotations[cp].push_back({pos, ann});
	}

	void removeAnnotation(ChunkPos cp, glm::ivec3 pos) {
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
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunks;
	std::unordered_map<ChunkPos,
	                    std::vector<std::pair<glm::ivec3, Annotation>>,
	                    ChunkPosHash> m_annotations;
	BlockRegistry m_blocks;
	EntityManager m_entityDefs;
};

} // namespace civcraft
