#pragma once

#include "logic/types.h"
#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/annotation.h"
#include "server/chunk_info.h"
#include "server/entity_manager.h"
#include "server/world_template.h"
#include "logic/action.h"
#include "shared/chunk_source.h"
#include "content/builtin.h"
#include <glm/vec3.hpp>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <array>
#include <functional>
#include <random>
#include <cstdint>

namespace civcraft {

// Per-instance state for active blocks (TNT fuse, wheat stage, wire power, etc.)
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
	ActionProposalQueue proposals; // proposals queued by input/behaviors, drained in Phase 1

	// Canonical spawn position — feet position one block above the SpawnPoint floor block.
	// Set once at server init from WorldTemplate::preferredSpawn().
	// New players always spawn here, facing +Z toward the stairs.
	glm::vec3 spawnPoint{0.f, 0.f, 0.f};

	World(int seed = 42, std::shared_ptr<WorldTemplate> tmpl = nullptr, int templateIndex = 1)
		: m_seed(seed), m_templateIndex(templateIndex),
		  m_template(tmpl ? tmpl : std::make_shared<ConfigurableWorldTemplate>("artifacts/worlds/base/village.py")) {
		registerAllBuiltins(blocks, entities);
	}

	WorldTemplate& getTemplate() { return *m_template; }

	// Get the surface height at world XZ for spawn placement
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

	// True if a chunk has been generated/loaded (no generation side-effect).
	bool hasChunk(ChunkPos pos) {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_chunks.count(pos) > 0;
	}

	// Non-generating lookup for meshing (avoids generating chunks outside render range)
	Chunk* getChunkIfLoaded(ChunkPos pos) override {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_chunks.find(pos);
		return (it != m_chunks.end()) ? it->second.get() : nullptr;
	}

	// Get ChunkInfo for a chunk (built during generateChunk, always present if chunk exists).
	ChunkInfo* getChunkInfo(ChunkPos pos) {
		auto it = m_chunkInfos.find(pos);
		return it != m_chunkInfos.end() ? &it->second : nullptr;
	}

	// Batch lookup: fetch center chunk + all 26 neighbors in a single lock.
	// Returns array of 27 Chunk* (center at index 13). Null if not loaded.
	// Index = (dy+1)*9 + (dz+1)*3 + (dx+1) for dx,dy,dz in {-1,0,1}
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

	// Unload chunks far from center to free memory
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
			activeBlocks.erase({pos.x, pos.y, pos.z}); // clean up any active block state
			// Also drop annotations belonging to this chunk so memory doesn't grow
			// unbounded when players wander far. They'll be regenerated on revisit.
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

	// Active block state storage
	ActiveBlockMap activeBlocks;

	// Sparse annotation storage: world-space block pos → Annotation.
	// Annotations are render-only adornments (flowers, vines, cobwebs). They
	// are attached to the block at `pos` and removed automatically when that
	// block is cleared. See shared/annotation.h.
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

	// Collect annotations whose host-block pos falls inside the given chunk.
	// Used by the server to stream annotations alongside S_CHUNK.
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

	// Set state for an active block when placed
	void setBlockState(int x, int y, int z, const BlockStateMap& state) {
		activeBlocks[{x, y, z}] = state;
	}

	// Remove active block state when block is broken
	void removeBlockState(int x, int y, int z) {
		activeBlocks.erase({x, y, z});
	}

	// Get state for an active block (returns nullptr if not active)
	BlockStateMap* getBlockState(int x, int y, int z) {
		auto it = activeBlocks.find({x, y, z});
		return it != activeBlocks.end() ? &it->second : nullptr;
	}

	// Annotation spawn rules — populated at server startup from Python
	// annotation artifacts. Each rule says "place annotation `typeId` on top
	// of any block with string_id in `onBlocks`, with probability `chance`."
	// Applied during generateChunk() for every newly-generated chunk.
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

	// Callback for block mutations during active block ticking
	using BlockMutationCB = std::function<void(int x, int y, int z, BlockId newType)>;

	// Tick active blocks (TNT, wheat, etc.)
	void tickActiveBlocks(float dt, BlockMutationCB onMutate = nullptr) {
		std::vector<BlockStateKey> toRemove;
		std::vector<std::pair<BlockStateKey, BlockStateMap>> toAdd;

		// Collect mutations to avoid modifying map during iteration
		struct Explosion { int cx, cy, cz, radius; };
		std::vector<Explosion> explosions;

		for (auto& [pos, state] : activeBlocks) {
			BlockId bid = getBlock(pos.x, pos.y, pos.z);
			const BlockDef& def = blocks.get(bid);

			if (def.string_id == BlockType::TNT) {
				// TNT: if lit, count down fuse. When fuse hits 0, explode.
				if (state[Prop::Lit] > 0) {
					state[Prop::FuseTicks]--;
					if (state[Prop::FuseTicks] <= 0) {
						explosions.push_back({pos.x, pos.y, pos.z, 3});
						toRemove.push_back(pos);
					}
				}
			} else if (def.string_id == BlockType::Wheat) {
				// Wheat: grow over time
				int stage = state[Prop::GrowthStage];
				int maxStage = state[Prop::MaxStage];
				if (stage < maxStage) {
					state["tick_acc"] += 1;
					if (state["tick_acc"] >= 600) { // ~30s per stage at 20 tps
						state["tick_acc"] = 0;
						state[Prop::GrowthStage] = stage + 1;
					}
				}
			} else if (def.string_id == BlockType::Wire) {
				// Wire: propagate signal from neighbors, decay by 1
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
				// NAND gate: read two inputs from adjacent blocks, output NAND
				int maxPower = 15;
				// Inputs from -X and +X neighbors, output to +Z neighbor
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
				// NAND: output high unless both inputs are high
				bool a = state[Prop::InputA] > 0;
				bool b = state[Prop::InputB] > 0;
				state[Prop::Output] = (a && b) ? 0 : maxPower;
			}
		}

		for (auto& pos : toRemove) activeBlocks.erase(pos);

		// Process explosions
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
							// Chain: if another TNT, light it
							if (eDef.string_id == BlockType::TNT) {
								auto* tntState = getBlockState(bx, by, bz);
								if (tntState) {
									(*tntState)[Prop::Lit] = 1;
									(*tntState)[Prop::FuseTicks] = 20; // shorter fuse for chain
								}
							} else if (eDef.string_id != BlockType::Water) {
								// Remove non-water blocks
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

	// Iterate all loaded chunks (for saving)
	void forEachChunk(std::function<void(ChunkPos, const Chunk&)> fn) {
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto& [pos, chunk] : m_chunks) fn(pos, *chunk);
	}

private:
	Chunk* generateChunk(ChunkPos pos) {
		auto chunk = std::make_unique<Chunk>();
		m_template->generate(*chunk, pos, m_seed, blocks);
		auto* ptr = chunk.get();
		m_chunks[pos] = std::move(chunk);
		// Build ChunkInfo — counts + hasAir flag, O(CHUNK_VOLUME).
		m_chunkInfos[pos] = ChunkInfo::build(pos, *ptr, blocks);
		scatterAnnotations(pos, *ptr);
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
	int m_templateIndex = 1;
	std::shared_ptr<WorldTemplate> m_template;
	std::mutex m_mutex;
	std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunks;
	std::unordered_map<ChunkPos, ChunkInfo, ChunkPosHash> m_chunkInfos;
};

} // namespace civcraft
