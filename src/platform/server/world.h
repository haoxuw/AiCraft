#pragma once

#include "logic/types.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/annotation.h"
#include "server/chunk_info.h"
#include "server/entity_manager.h"
#include "server/world_template.h"
#include "logic/action.h"
#include "logic/chunk_source.h"
#include "server/builtin.h"
#include <glm/vec3.hpp>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <array>
#include <functional>
#include <random>
#include <cstdint>

namespace solarium {

// World-scale: how many real-world meters one block edge represents. Used by
// any system that needs to convert engine units (blocks/s, blocks) into a
// fixed-scale physical quantity (m/s, m). For now a placeholder; a separate
// world-scale project will replace this with a per-template value.
inline constexpr float kBlockMeters = 1.0f;

// Per-instance state for active blocks (TNT fuse, wheat stage, wire power).
struct BlockStateKey {
	int x, y, z;
	bool operator==(const BlockStateKey& o) const { return x==o.x && y==o.y && z==o.z; }
};
struct BlockStateKeyHash {
	size_t operator()(const BlockStateKey& k) const {
		return std::hash<int>()(k.x) ^ (std::hash<int>()(k.y) << 10) ^ (std::hash<int>()(k.z) << 20);
	}
};
using BlockStateMap = std::unordered_map<std::string, int>;
using ActiveBlockMap = std::unordered_map<BlockStateKey, BlockStateMap, BlockStateKeyHash>;
using AnnotationMap = std::unordered_map<BlockStateKey, Annotation, BlockStateKeyHash>;

class World : public ChunkSource {
public:
	BlockRegistry blocks;
	EntityManager entities;
	ActionProposalQueue proposals; // drained in Phase 1

	// Feet position, one block above SpawnPoint floor. Set once at init from
	// WorldTemplate::preferredSpawn(). Players spawn facing +Z.
	glm::vec3 spawnPoint{0.f, 0.f, 0.f};

	World(int seed = 42, std::shared_ptr<WorldTemplate> tmpl = nullptr, int templateIndex = 0)
		: m_seed(seed), m_templateIndex(templateIndex),
		  m_template(tmpl ? tmpl : std::make_shared<ConfigurableWorldTemplate>("artifacts/worlds/base/village.py")) {
		registerAllBuiltins(blocks, entities);
		// Templates with region-derived appearance palettes (voxel_earth) need
		// to install per-block tint tables now that the registry is populated.
		m_template->onBlockRegistryReady(blocks);
	}

	WorldTemplate& getTemplate() { return *m_template; }

	// For spawn placement.
	float surfaceHeight(float x, float z) {
		return m_template->surfaceHeight(m_seed, x, z);
	}

	const BlockRegistry& blockRegistry() const override { return blocks; }

	Chunk* getChunk(ChunkPos pos) override {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_chunks.find(pos);
		if (it != m_chunks.end())
			return it->second.get();
		return generateChunk(pos);
	}

	// Non-generating existence check.
	bool hasChunk(ChunkPos pos) {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_chunks.count(pos) > 0;
	}

	// Perf telemetry — loaded chunk count sampled each tick.
	size_t loadedChunkCount() {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_chunks.size();
	}

	// Meshing lookup — avoids generating chunks outside render range.
	Chunk* getChunkIfLoaded(ChunkPos pos) override {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_chunks.find(pos);
		return (it != m_chunks.end()) ? it->second.get() : nullptr;
	}

	// Always present when the chunk exists (built in generateChunk).
	ChunkInfo* getChunkInfo(ChunkPos pos) {
		auto it = m_chunkInfos.find(pos);
		return it != m_chunkInfos.end() ? &it->second : nullptr;
	}

	// Fetch center + 26 neighbors under one lock. Center at index 13;
	// index = (dy+1)*9 + (dz+1)*3 + (dx+1), dx/dy/dz in {-1,0,1}. Null = unloaded.
	std::array<Chunk*, 27> getChunkNeighborhood(ChunkPos center) {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::array<Chunk*, 27> result{};
		int i = 0;
		for (int dy = -1; dy <= 1; dy++)
			for (int dz = -1; dz <= 1; dz++)
				for (int dx = -1; dx <= 1; dx++) {
					ChunkPos np = {center.x + dx, center.y + dy, center.z + dz};
					auto it = m_chunks.find(np);
					result[i++] = (it != m_chunks.end()) ? it->second.get() : nullptr;
					}
		return result;
	}

	BlockId getBlock(int wx, int wy, int wz) override {
		ChunkPos cp = worldToChunk(wx, wy, wz);
		Chunk* chunk = getChunk(cp);
		if (!chunk) return BLOCK_AIR;
		int lx = ((wx % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int ly = ((wy % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int lz = ((wz % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		return chunk->get(lx, ly, lz);
	}

	void ensureChunksAround(ChunkPos center, int radius) override {
		for (int y = center.y - 2; y <= center.y + 2; y++)
			for (int z = center.z - radius; z <= center.z + radius; z++)
				for (int x = center.x - radius; x <= center.x + radius; x++)
					getChunk({x, y, z});
	}

	void unloadDistantChunks(ChunkPos center, int keepRadius) override {
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<ChunkPos> toRemove;
		int maxDistSq = (keepRadius + 3) * (keepRadius + 3);
		for (auto& [pos, chunk] : m_chunks) {
			int dx = pos.x - center.x, dz = pos.z - center.z;
			if (dx*dx + dz*dz > maxDistSq) {
				toRemove.push_back(pos);
			}
		}
		for (auto& pos : toRemove) {
			m_chunks.erase(pos);
			activeBlocks.erase({pos.x, pos.y, pos.z});
			// Drop annotations too — regenerated on revisit.
			int x0 = pos.x * CHUNK_SIZE, y0 = pos.y * CHUNK_SIZE, z0 = pos.z * CHUNK_SIZE;
			int x1 = x0 + CHUNK_SIZE, y1 = y0 + CHUNK_SIZE, z1 = z0 + CHUNK_SIZE;
			for (auto it = annotations.begin(); it != annotations.end(); ) {
				if (it->first.x >= x0 && it->first.x < x1 &&
				    it->first.y >= y0 && it->first.y < y1 &&
				    it->first.z >= z0 && it->first.z < z1) it = annotations.erase(it);
				else ++it;
			}
		}
	}

	ActiveBlockMap activeBlocks;

	// Render-only adornments (flowers, vines, cobwebs) attached to host block;
	// cleared when the block is cleared. See shared/annotation.h.
	AnnotationMap annotations;

	void setAnnotation(int x, int y, int z, const Annotation& a) {
		if (a.empty()) { annotations.erase({x,y,z}); return; }
		annotations[{x,y,z}] = a;
	}
	void removeAnnotation(int x, int y, int z) { annotations.erase({x,y,z}); }
	const Annotation* getAnnotation(int x, int y, int z) const {
		auto it = annotations.find({x,y,z});
		return it != annotations.end() ? &it->second : nullptr;
	}

	// Host-block positions inside this chunk (streamed alongside S_CHUNK).
	std::vector<std::pair<glm::ivec3, Annotation>> annotationsInChunk(ChunkPos cp) const {
		std::vector<std::pair<glm::ivec3, Annotation>> out;
		int x0 = cp.x * CHUNK_SIZE, y0 = cp.y * CHUNK_SIZE, z0 = cp.z * CHUNK_SIZE;
		int x1 = x0 + CHUNK_SIZE, y1 = y0 + CHUNK_SIZE, z1 = z0 + CHUNK_SIZE;
		for (auto& [k, a] : annotations) {
			if (k.x >= x0 && k.x < x1 && k.y >= y0 && k.y < y1 && k.z >= z0 && k.z < z1)
				out.push_back({glm::ivec3{k.x, k.y, k.z}, a});
		}
		return out;
	}

	// Appearance mutator — see docs/22_APPEARANCE.md invariant I1. Clamped
	// against the block's palette (I4). Returns the old appearance index so the
	// caller can emit onBlockChange without an extra read.
	uint8_t setAppearance(int x, int y, int z, uint8_t idx) {
		ChunkPos cp = worldToChunk(x, y, z);
		Chunk* c = getChunk(cp);
		if (!c) return 0;
		int lx = ((x % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int ly = ((y % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int lz = ((z % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		BlockId bid = c->get(lx, ly, lz);
		uint8_t clamped = blocks.get(bid).clampAppearance(idx);
		uint8_t old = c->getAppearance(lx, ly, lz);
		c->setAppearance(lx, ly, lz, clamped);
		return old;
	}

	void setBlockState(int x, int y, int z, const BlockStateMap& state) {
		activeBlocks[{x, y, z}] = state;
	}

	void removeBlockState(int x, int y, int z) {
		activeBlocks.erase({x, y, z});
	}

	// nullptr if not active.
	BlockStateMap* getBlockState(int x, int y, int z) {
		auto it = activeBlocks.find({x, y, z});
		return it != activeBlocks.end() ? &it->second : nullptr;
	}

	// Loaded from Python annotation artifacts at startup; applied per chunk.
	struct AnnotationSpawnRule {
		std::string typeId;
		AnnotationSlot slot = AnnotationSlot::Top;
		std::vector<std::string> onBlocks; // block string_ids
		float chance = 0.0f;
	};
	std::vector<AnnotationSpawnRule> annotationSpawnRules;

	void addAnnotationSpawnRule(const AnnotationSpawnRule& r) {
		annotationSpawnRules.push_back(r);
	}

	using BlockMutationCB = std::function<void(int x, int y, int z, BlockId newType)>;

	void tickActiveBlocks(float dt, BlockMutationCB onMutate = nullptr) {
		std::vector<BlockStateKey> toRemove;
		std::vector<std::pair<BlockStateKey, BlockStateMap>> toAdd;

		// Collect mutations first to avoid modifying map during iteration.
		struct Explosion { int cx, cy, cz, radius; };
		std::vector<Explosion> explosions;

		for (auto& [pos, state] : activeBlocks) {
			BlockId bid = getBlock(pos.x, pos.y, pos.z);
			const BlockDef& def = blocks.get(bid);

			if (def.string_id == BlockType::TNT) {
				if (state[Prop::Lit] > 0) {
					state[Prop::FuseTicks]--;
					if (state[Prop::FuseTicks] <= 0) {
						explosions.push_back({pos.x, pos.y, pos.z, 3});
						toRemove.push_back(pos);
					}
				}
			} else if (def.string_id == BlockType::Wheat) {
				int stage = state[Prop::GrowthStage];
				int maxStage = state[Prop::MaxStage];
				if (stage < maxStage) {
					state["tick_acc"] += 1;
					if (state["tick_acc"] >= 600) { // ~30s @20tps
						state["tick_acc"] = 0;
						state[Prop::GrowthStage] = stage + 1;
					}
				}
			} else if (def.string_id == BlockType::Wire) {
				// Propagate strongest neighbor signal, decay by 1.
				int maxPower = state[Prop::MaxPower];
				int bestNeighbor = 0;
				int offsets[][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}};
				for (auto& o : offsets) {
					int nx = pos.x+o[0], ny = pos.y+o[1], nz = pos.z+o[2];
					auto* ns = getBlockState(nx, ny, nz);
					if (ns) {
						auto it = ns->find(Prop::Power);
						if (it != ns->end() && it->second > bestNeighbor)
							bestNeighbor = it->second;
						auto oit = ns->find(Prop::Output);
						if (oit != ns->end() && oit->second > bestNeighbor)
							bestNeighbor = oit->second;
					}
				}
				int newPower = std::max(0, bestNeighbor - 1);
				newPower = std::min(newPower, maxPower);
				state[Prop::Power] = newPower;
			} else if (def.string_id == BlockType::NANDGate) {
				// Inputs at -X/+X, output at +Z.
				int maxPower = 15;
				auto readPower = [&](int nx, int ny, int nz) -> int {
					auto* ns = getBlockState(nx, ny, nz);
					if (!ns) return 0;
					auto it = ns->find(Prop::Power);
					if (it != ns->end()) return it->second;
					auto oit = ns->find(Prop::Output);
					if (oit != ns->end()) return oit->second;
					return 0;
				};
				state[Prop::InputA] = readPower(pos.x-1, pos.y, pos.z);
				state[Prop::InputB] = readPower(pos.x+1, pos.y, pos.z);
				bool a = state[Prop::InputA] > 0;
				bool b = state[Prop::InputB] > 0;
				state[Prop::Output] = (a && b) ? 0 : maxPower;
			}
		}

		for (auto& pos : toRemove) activeBlocks.erase(pos);

		for (auto& exp : explosions) {
			int r = exp.radius;
			for (int dy = -r; dy <= r; dy++)
				for (int dz = -r; dz <= r; dz++)
					for (int dx = -r; dx <= r; dx++) {
						if (dx*dx + dy*dy + dz*dz > r*r) continue;
						int bx = exp.cx + dx, by = exp.cy + dy, bz = exp.cz + dz;
						BlockId existing = getBlock(bx, by, bz);
						if (existing != BLOCK_AIR) {
							const BlockDef& eDef = blocks.get(existing);
							// Chain: light neighboring TNT with a shorter fuse.
							if (eDef.string_id == BlockType::TNT) {
								auto* tntState = getBlockState(bx, by, bz);
								if (tntState) {
									(*tntState)[Prop::Lit] = 1;
									(*tntState)[Prop::FuseTicks] = 20;
								}
							} else if (eDef.string_id != BlockType::Water) {
								ChunkPos cp = worldToChunk(bx, by, bz);
								Chunk* c = getChunk(cp);
								if (c) {
									c->set(((bx%16)+16)%16, ((by%16)+16)%16, ((bz%16)+16)%16, BLOCK_AIR);
									if (onMutate) onMutate(bx, by, bz, BLOCK_AIR);
								}
								removeBlockState(bx, by, bz);
							}
						}
					}
		}
	}

	static ChunkPos worldToChunk(int wx, int wy, int wz) {
		auto div = [](int a, int b) { return (a >= 0) ? a / b : (a - b + 1) / b; };
		return {div(wx, CHUNK_SIZE), div(wy, CHUNK_SIZE), div(wz, CHUNK_SIZE)};
	}

	int seed() const { return m_seed; }
	int templateIndex() const { return m_templateIndex; }

	// For saving.
	void forEachChunk(std::function<void(ChunkPos, const Chunk&)> fn) {
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto& [pos, chunk] : m_chunks) fn(pos, *chunk);
	}

	// Drained once per server tick so the server can spawn structure entities
	// (trees, etc.) for anything worldgen placed since the last tick.
	// Swap semantics: returns the queue contents, leaves the internal list empty.
	std::vector<PendingStructureSpawn> drainPendingStructureSpawns() {
		std::lock_guard<std::mutex> lock(m_pendingMutex);
		std::vector<PendingStructureSpawn> out;
		out.swap(m_pendingStructureSpawns);
		return out;
	}

private:
	Chunk* generateChunk(ChunkPos pos) {
		auto chunk = std::make_unique<Chunk>();
		std::vector<PendingStructureSpawn> pending;
		m_template->generate(*chunk, pos, m_seed, blocks, &pending);
		auto* ptr = chunk.get();
		m_chunks[pos] = std::move(chunk);
		// Build ChunkInfo — counts + hasAir flag, O(CHUNK_VOLUME).
		m_chunkInfos[pos] = ChunkInfo::build(pos, *ptr, blocks);
		scatterAnnotations(pos, *ptr);
		if (!pending.empty()) {
			std::lock_guard<std::mutex> plock(m_pendingMutex);
			for (auto& p : pending) m_pendingStructureSpawns.push_back(std::move(p));
		}
		return ptr;
	}

	// Run every AnnotationSpawnRule against the top surface of this chunk.
	// Deterministic per (seed, pos) — a chunk re-generated with the same seed
	// gets the same flower layout.
	void scatterAnnotations(ChunkPos cp, const Chunk& chunk) {
		if (annotationSpawnRules.empty()) return;
		// Per-chunk RNG seeded from (seed, cx, cy, cz) so placement is stable.
		uint64_t h = (uint64_t)m_seed * 0x9E3779B97F4A7C15ull;
		h ^= (uint64_t)cp.x * 0xBF58476D1CE4E5B9ull;
		h ^= (uint64_t)cp.y * 0x94D049BB133111EBull;
		h ^= (uint64_t)cp.z * 0xC2B2AE3D27D4EB4Full;
		std::mt19937 rng((uint32_t)(h ^ (h >> 32)));
		std::uniform_real_distribution<float> roll(0.0f, 1.0f);

		int ox = cp.x * CHUNK_SIZE, oy = cp.y * CHUNK_SIZE, oz = cp.z * CHUNK_SIZE;
		for (int lz = 0; lz < CHUNK_SIZE; lz++) {
			for (int lx = 0; lx < CHUNK_SIZE; lx++) {
				// Top-down scan for the uppermost solid block in this column.
				for (int ly = CHUNK_SIZE - 1; ly >= 0; ly--) {
					BlockId bid = chunk.get(lx, ly, lz);
					if (bid == BLOCK_AIR) continue;
					// Block above must be AIR for a Top annotation.
					if (ly + 1 < CHUNK_SIZE && chunk.get(lx, ly + 1, lz) != BLOCK_AIR) break;
					const std::string& sid = blocks.get(bid).string_id;
					for (auto& rule : annotationSpawnRules) {
						if (rule.slot != AnnotationSlot::Top) continue;
						bool match = false;
						for (auto& b : rule.onBlocks) if (b == sid) { match = true; break; }
						if (!match) continue;
						if (roll(rng) >= rule.chance) continue;
						annotations[{ox + lx, oy + ly, oz + lz}] = Annotation{rule.typeId, rule.slot};
						break; // one annotation per block
					}
					break;
				}
			}
		}
	}

	int m_seed;
	int m_templateIndex = 0;
	std::shared_ptr<WorldTemplate> m_template;
	std::mutex m_mutex;
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunks;
	std::unordered_map<ChunkPos, ChunkInfo, ChunkPosHash> m_chunkInfos;

	// Structure-entity spawns queued by worldgen (see generate()'s out-param).
	// Owned separately with its own mutex to avoid re-entering EntityManager
	// while m_mutex (chunk lock) is held.
	std::vector<PendingStructureSpawn> m_pendingStructureSpawns;
	std::mutex m_pendingMutex;
};

} // namespace solarium
