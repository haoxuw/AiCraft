#pragma once

#include "logic/types.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/constants.h"
#include "server/noise.h"
#include "server/world_gen_config.h"
#include "server/python_bridge.h"
#include "server/world_accessibility.h"
#include <string>
#include <cmath>
#include <unordered_map>
#include <cstdio>

namespace civcraft {

// A structure placed by worldgen (e.g. a tree trunk) that the server should
// also spawn as an entity. Collected during chunk generation, drained after
// the chunk lock is released (avoids re-entering EntityManager under the lock).
struct PendingStructureSpawn {
	std::string blueprintId;         // e.g. "tree" — matches artifacts/structures/*.py
	std::string entityType;          // e.g. StructureName::Tree — registered EntityDef
	glm::ivec3  anchorPos = {0,0,0}; // usually the trunk/root base
	// Exact leaf positions this tree planted — handed to the SeasonalLeaves
	// feature so each tree paints only its own canopy, not overlapping neighbors.
	std::vector<glm::ivec3> leafPositions;
};

class WorldTemplate {
public:
	virtual ~WorldTemplate() = default;

	virtual std::string name()        const = 0;
	virtual std::string description() const = 0;

	// pendingSpawns (optional): populated with structure entities that should
	// be spawned after the chunk is stored. Pass nullptr to discard.
	virtual void generate(Chunk& chunk, ChunkPos cpos, int seed,
	                      const BlockRegistry& blocks,
	                      std::vector<PendingStructureSpawn>* pendingSpawns = nullptr) = 0;

	virtual float surfaceHeight(int seed, float x, float z) const = 0;
	virtual glm::vec3 preferredSpawn(int seed) const = 0;
	// spawnPos provided so chest can be relative to it.
	virtual glm::vec3 chestPosition(int seed, glm::vec3 spawnPos) const = 0;
	// Returns {spawnPos.x, spawnPos.z} if no village.
	virtual glm::ivec2 villageCenter(int seed) const = 0;
	virtual const WorldPyConfig& pyConfig() const = 0;

	// {-1,-1} if no barn.
	virtual glm::ivec2 barnCenter(int seed) const { return {-1, -1}; }
	// Avoid column-scanning for animals inside the barn.
	virtual int barnFloorY(int seed) const { return -1; }

	// In house order; villager[i] → chest[i]. Empty if no village.
	virtual std::vector<glm::vec3> houseChestPositions(int seed) const { return {}; }

	// Trident tower deck center. y<0 = no monument (server sentinel).
	virtual glm::vec3 monumentPosition(int seed) const { (void)seed; return {0, -1, 0}; }
};

// Driven by a Python config: flat or Perlin terrain, optional village,
// spawn portal, trees. FlatWorldTemplate/VillageWorldTemplate are aliases.
class ConfigurableWorldTemplate : public WorldTemplate {
public:
	explicit ConfigurableWorldTemplate(const std::string& pyPath) {
		loadWorldConfig(pyPath, m_py);
		m_tp.continentScale     = m_py.continentScale;
		m_tp.continentAmplitude = m_py.continentAmplitude;
		m_tp.hillScale          = m_py.hillScale;
		m_tp.hillAmplitude      = m_py.hillAmplitude;
		m_tp.detailScale        = m_py.detailScale;
		m_tp.detailAmplitude    = m_py.detailAmplitude;
		m_tp.microScale         = m_py.microScale;
		m_tp.microAmplitude     = m_py.microAmplitude;
	}

	std::string name()        const override { return m_py.name.empty() ? "World" : m_py.name; }
	std::string description() const override { return m_py.description; }

	const WorldPyConfig& pyConfig() const override { return m_py; }

	float surfaceHeight(int seed, float x, float z) const override {
		if (m_py.terrainType == "flat") return m_py.surfaceY;
		return naturalTerrainHeight(seed, x, z, m_tp);
	}

	glm::vec3 preferredSpawn(int seed) const override {
		// Portal world: spawn at platform (groundY + kPlatH), arch-interior center.
		// Portal-less: directly on top of bare spawn block.
		float gY, sx, sz;
		if (m_py.terrainType == "flat") {
			gY = m_py.surfaceY;
			sx = m_py.spawnSearchX;
			sz = m_py.spawnSearchZ;
		} else {
			auto anchor = findAnchor(seed);
			sx = anchor.x;
			sz = anchor.y;
			gY = m_py.hasPortal
				? (float)portalGroundY(seed)
				: naturalTerrainHeight(seed, anchor.x, anchor.y, m_tp);
		}
		int bx = (int)std::round(sx);
		int bz = (int)std::round(sz);
		if (!m_py.hasPortal) {
			return {(float)bx + 0.5f, gY + 1.0f, (float)bz + 0.5f};
		}
		// +0.5 X/Z: block center. -1.0 Z: spawn block sits at dz=-1 of floor.
		return {(float)bx + 0.5f, gY + kPlatH, (float)bz - 0.5f};
	}

	glm::ivec2 villageCenter(int seed) const override {
		// No village → mobs spawn around player anchor (not distant villageOffset)
		// so the dog spawns next to the player in test worlds.
		float offX = m_py.hasVillage ? m_py.villageOffsetX : 0.0f;
		float offZ = m_py.hasVillage ? m_py.villageOffsetZ : 0.0f;
		if (m_py.terrainType == "flat") {
			return {(int)m_py.spawnSearchX + (int)offX,
			        (int)m_py.spawnSearchZ + (int)offZ};
		}
		auto anchor = findAnchor(seed);
		return {(int)anchor.x + (int)offX,
		        (int)anchor.y + (int)offZ};
	}

	glm::vec3 monumentPosition(int seed) const override {
		return monumentPositionAt(seed, villageCenter(seed));
	}

	// Phase 4: explicit-center variant used by the per-seat VillageStamper.
	glm::vec3 monumentPositionAt(int seed, glm::ivec2 vc) const {
		// Anchor at deck center (my+21) so flame FX wraps body + trident above.
		if (!m_py.hasVillage) return {0, -1, 0};
		int my = (int)std::round(groundHeight(seed, (float)vc.x, (float)vc.y)) + 1;
		return {(float)vc.x + 0.5f, (float)(my + 21), (float)vc.y + 0.5f};
	}

	glm::vec3 chestPosition(int seed, glm::vec3 spawnPos) const override {
		if (m_py.hasVillage && !m_py.houses.empty()) {
			auto vc = villageCenter(seed);
			const auto& h0 = m_py.houses[0];
			int hcx = vc.x + h0.cx, hcz = vc.y + h0.cz;
			int floorY = structureFloorY(seed, hcx, hcz, h0.w, h0.d);
			float hx = (float)(hcx + h0.w - 3);
			float hz = (float)(hcz + 1);
			return {hx, (float)floorY, hz};
		}
		// Fallback: offset from spawn.
		float sy = surfaceHeight(seed, spawnPos.x + m_py.chestOffsetX,
		                         spawnPos.z + m_py.chestOffsetZ) + 1.0f;
		return {spawnPos.x + m_py.chestOffsetX, sy, spawnPos.z + m_py.chestOffsetZ};
	}

	glm::ivec2 barnCenter(int seed) const override {
		return barnCenterAt(villageCenter(seed));
	}

	// Phase 4: explicit-center variant used by the per-seat VillageStamper.
	glm::ivec2 barnCenterAt(glm::ivec2 vc) const {
		if (!m_py.hasVillage) return {-1, -1};
		for (const auto& h : m_py.houses) {
			if (h.type == "barn")
				return {vc.x + h.cx + h.w / 2, vc.y + h.cz + h.d / 2};
		}
		return {-1, -1};
	}

	// actualSurfaceY() column-scans can latch onto sparse roof blocks — use this.
	int barnFloorY(int seed) const {
		return barnFloorYAt(seed, villageCenter(seed));
	}

	int barnFloorYAt(int seed, glm::ivec2 vc) const {
		if (!m_py.hasVillage) return -1;
		for (const auto& h : m_py.houses) {
			if (h.type == "barn")
				return structureFloorY(seed, vc.x + h.cx, vc.y + h.cz, h.w, h.d);
		}
		return -1;
	}

	// {minGroundY, maxGroundY} over footprint; floor = maxGroundY+1 avoids hillside burial.
	std::pair<int,int> footprintHeightRange(int seed,
	                                         int hcx, int hcz, int w, int d) const {
		int minY = INT_MAX, maxY = INT_MIN;
		for (int dx = 0; dx < w; dx++)
			for (int dz = 0; dz < d; dz++) {
				int y = (int)std::round(groundHeight(seed, (float)(hcx+dx), (float)(hcz+dz)));
				if (y < minY) minY = y;
				if (y > maxY) maxY = y;
			}
		if (minY == INT_MAX) minY = maxY = 4;
		return {minY, maxY};
	}

	// floorY = maxGroundY + 1 (always above hillside peak).
	int structureFloorY(int seed, int hcx, int hcz, int w, int d) const {
		return footprintHeightRange(seed, hcx, hcz, w, d).second + 1;
	}

	// 17-wide platform + stair on +Z. Max terrain so arch never lands in a ditch.
	int portalGroundY(int seed) const {
		if (m_py.terrainType == "flat") return (int)m_py.surfaceY;
		auto anchor = findAnchor(seed);
		int px = (int)std::round(anchor.x);
		int pz = (int)std::round(anchor.y);
		return footprintHeightRange(seed, px - 8, pz - 5, 17, 14).second;
	}

	// In house order (matches bedPositions()); uses footprint-max floor.
	std::vector<glm::vec3> houseChestPositions(int seed) const override {
		return houseChestPositionsAt(seed, villageCenter(seed));
	}

	// Phase 4: explicit-center variant used by the per-seat VillageStamper.
	std::vector<glm::vec3> houseChestPositionsAt(int seed, glm::ivec2 vc) const {
		if (!m_py.hasVillage || m_py.houses.empty()) return {};
		std::vector<glm::vec3> chests;
		for (const auto& h : m_py.houses) {
			if (h.type == "barn") continue;
			int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
			int floorY = structureFloorY(seed, hcx, hcz, h.w, h.d);
			float hx = (float)(hcx + h.w - 3);
			float hz = (float)(hcz + 1);
			chests.push_back({hx, (float)floorY, hz});
		}
		return chests;
	}

	// Phase 4 public hook for VillageStamper: stamp all village blocks (houses,
	// tower blocks, paths, farms, pens) into *one chunk* at an explicit center.
	// Caller iterates chunks overlapping the village footprint.
	void generateVillageInChunk(Chunk& chunk, ChunkPos cpos, int seed,
	                            glm::ivec2 vc, const BlockRegistry& blocks) {
		if (!m_py.hasVillage) return;
		BlockId wallB  = blocks.getId(m_py.wallBlock);
		BlockId roofB  = blocks.getId(m_py.roofBlock);
		BlockId floorB = blocks.getId(m_py.floorBlock);
		BlockId pathB  = blocks.getId(m_py.pathBlock);
		if (wallB  == BLOCK_AIR) wallB  = blocks.getId(BlockType::Cobblestone);
		if (roofB  == BLOCK_AIR) roofB  = blocks.getId(BlockType::Wood);
		if (floorB == BLOCK_AIR) floorB = blocks.getId(BlockType::Cobblestone);
		if (pathB  == BLOCK_AIR) pathB  = blocks.getId(BlockType::Cobblestone);
		generateVillage(chunk, cpos, seed, wallB, roofB, floorB, pathB, vc, blocks);
	}

	void generate(Chunk& chunk, ChunkPos cpos, int seed,
	              const BlockRegistry& blocks,
	              std::vector<PendingStructureSpawn>* pendingSpawns = nullptr) override {
		BlockId bStone  = blocks.getId(BlockType::Stone);
		BlockId bDirt   = blocks.getId(BlockType::Dirt);
		BlockId bGrass  = blocks.getId(BlockType::Grass);
		BlockId bSand   = blocks.getId(BlockType::Sand);
		BlockId bWater  = blocks.getId(BlockType::Water);
		BlockId bSnow   = blocks.getId(BlockType::Snow);
		BlockId bWood   = blocks.getId(BlockType::Wood);
		BlockId bLog    = blocks.getId(BlockType::Log);
		BlockId bLeaves = blocks.getId(BlockType::Leaves);
		BlockId bTallGrass = blocks.getId(BlockType::TallGrass);

		BlockId wallB  = blocks.getId(m_py.wallBlock);
		BlockId roofB  = blocks.getId(m_py.roofBlock);
		BlockId floorB = blocks.getId(m_py.floorBlock);
		BlockId pathB  = blocks.getId(m_py.pathBlock);

		if (wallB  == BLOCK_AIR) wallB  = blocks.getId(BlockType::Cobblestone);
		if (roofB  == BLOCK_AIR) roofB  = blocks.getId(BlockType::Wood);
		if (floorB == BLOCK_AIR) floorB = blocks.getId(BlockType::Cobblestone);
		if (pathB  == BLOCK_AIR) pathB  = blocks.getId(BlockType::Cobblestone);

		int ox = cpos.x * CHUNK_SIZE;
		int oy = cpos.y * CHUNK_SIZE;
		int oz = cpos.z * CHUNK_SIZE;

		bool isFlat = (m_py.terrainType == "flat");

		// Per-cell grass tint roll. Coarse smoothNoise gives 4-8 block patches
		// of one tint; a sprinkle of hashFloat jitter breaks straight patch
		// edges. Bucket weights: 40% default, 18% lush, 18% pale, 10% dry,
		// 10% mossy, 4% shadow — accent tints (dry/mossy) are bumped so the
		// field reads as varied terrain rather than near-uniform green.
		auto grassAppearance = [seed](int wx, int wz) -> uint8_t {
			float r = smoothNoise2D((wx + seed * 31) * 0.18f,
			                        (wz + seed * 17) * 0.18f);
			r = r * 0.90f + hashFloat(wx * 7, wz * 11) * 0.10f;
			if (r < 0.40f) return 0;
			if (r < 0.58f) return 1;
			if (r < 0.76f) return 2;
			if (r < 0.86f) return 3;
			if (r < 0.96f) return 4;
			return 5;
		};

		// Minecraft-style cluster placement: most of the world is bare grass,
		// but scattered "patches" of tall grass — sparse between, dense within.
		//
		// Approach: tile the world with 24×24 *macro cells*. A stable hash
		// per macro cell decides (30% chance) whether this tile contains a
		// cluster, plus the cluster's center offset, radius (5..7), and core
		// density (0.70..0.95). A grass top then scans the 3×3 ring of macro
		// cells around it, finds the strongest cluster contribution, and
		// rolls a cell-local hash to decide whether to spawn — and at what
		// height tier (palette 1..5).
		//
		// Height tier encodes distance from the cluster center: center → 5
		// (very tall), edge → 1 (short fringe). Quadratic falloff so the
		// center is a dense mound and coverage thins at the rim.
		// MACRO tile pitch × CLUSTER_P together set how often you walk into a
		// patch. Current values give ~1.5 clusters per 100×100 area — rare
		// enough that each clump is an event, frequent enough that the plains
		// aren't empty. Falloff is `1 - t²` (plateau then fade) so the inner
		// ~60% of every cluster is near-solid, only the rim thins out —
		// patches read as dense cushions, not scattered spikes.
		constexpr int   MACRO      = 40;     // cluster lattice pitch (blocks)
		constexpr float CLUSTER_P  = 0.22f;  // fraction of macro cells hosting a cluster
		auto tallGrassRoll = [seed](int wx, int wz) -> uint8_t {
			int mx = (wx >= 0 ? wx : wx - (MACRO - 1)) / MACRO;
			int mz = (wz >= 0 ? wz : wz - (MACRO - 1)) / MACRO;

			float bestP = 0.0f;
			float bestT = 1.0f;  // normalized d/r of best cluster (1 = edge)

			for (int dmz = -1; dmz <= 1; dmz++) {
				for (int dmx = -1; dmx <= 1; dmx++) {
					int cmx = mx + dmx, cmz = mz + dmz;
					// Stable per-macro-cell hash stream.
					float h0 = hashFloat(cmx * 97 + seed, cmz * 131 + seed * 3);
					if (h0 >= CLUSTER_P) continue;  // no cluster in this tile

					float h1 = hashFloat(cmx * 53 + seed * 7, cmz * 211 - seed);
					float h2 = hashFloat(cmx * 17 - seed * 11, cmz * 73 + seed * 19);
					float h3 = hashFloat(cmx * 29 + seed * 13, cmz * 41 - seed * 5);

					// Center placed inside the macro cell; margin so neighbouring
					// clusters don't fuse into one ambiguous blob.
					float cx = (float)(cmx * MACRO) + 5.0f + h1 * (float)(MACRO - 10);
					float cz = (float)(cmz * MACRO) + 5.0f + h2 * (float)(MACRO - 10);

					float radius = 5.0f + h3 * 4.0f;          // 5..9 blocks
					float coreDensity = 0.95f;                // near-solid core

					float dx = (float)wx - cx;
					float dz = (float)wz - cz;
					float d  = std::sqrt(dx * dx + dz * dz);
					if (d >= radius) continue;

					float t = d / radius;                     // 0 = core, 1 = edge
					// Plateau-then-fade falloff (`1 - t²`): inner ~60% of the
					// cluster stays ≥70% density, only the outer rim thins.
					// Previously we used `(1 - t)²` which dropped to 20% at
					// mid-radius — looked like random spikes, not a patch.
					float p = coreDensity * (1.0f - t * t);
					if (p > bestP) { bestP = p; bestT = t; }
				}
			}

			if (bestP <= 0.0f) return 0;

			// Per-cell spawn roll against the strongest contributing cluster.
			float cellRoll = hashFloat(wx * 53 + seed * 9, wz * 19 - seed);
			if (cellRoll > bestP) return 0;

			// Height tier from normalized distance. Center (t≈0) → 5 (very
			// tall), edge (t→1) → 1 (short fringe). Tight core (<10% radius)
			// so the tallest tufts form a small mound, not a plateau.
			if (bestT < 0.10f) return 5;
			if (bestT < 0.25f) return 4;
			if (bestT < 0.45f) return 3;
			if (bestT < 0.70f) return 2;
			return 1;
		};

		// Terrain
		if (isFlat) {
			int sy = (int)m_py.surfaceY;
			int dd = m_py.dirtDepth;
			for (int ly = 0; ly < CHUNK_SIZE; ly++) {
				int wy = oy + ly;
				BlockId type = BLOCK_AIR;
				if      (wy < sy - dd) type = bStone;
				else if (wy < sy)      type = bDirt;
				else if (wy == sy)     type = bGrass;
				else                   continue;
				bool isGrass = (type == bGrass);
				for (int lz = 0; lz < CHUNK_SIZE; lz++)
					for (int lx = 0; lx < CHUNK_SIZE; lx++) {
						chunk.set(lx, ly, lz, type);
						if (isGrass)
							chunk.setAppearance(lx, ly, lz, grassAppearance(ox + lx, oz + lz));
					}
			}
			// Decoration pass: scatter tall_grass on the +1 layer above any
			// exposed grass top. Runs after the main terrain loop so the grass
			// cells are already in place and we don't race against them.
			if (bTallGrass != BLOCK_AIR) {
				int plantLy = sy - oy + 1;  // surface+1 in local coords
				if (plantLy >= 0 && plantLy < CHUNK_SIZE) {
					for (int lz = 0; lz < CHUNK_SIZE; lz++)
						for (int lx = 0; lx < CHUNK_SIZE; lx++) {
							uint8_t app = tallGrassRoll(ox + lx, oz + lz);
							if (!app) continue;
							chunk.set(lx, plantLy, lz, bTallGrass);
							chunk.setAppearance(lx, plantLy, lz, app);
						}
				}
			}
		} else {
			int wl = (int)m_py.waterLevel;
			float snowLine = m_py.snowThreshold;
			int dd = m_py.dirtDepth;
			for (int lz = 0; lz < CHUNK_SIZE; lz++) {
				for (int lx = 0; lx < CHUNK_SIZE; lx++) {
					int wx = ox + lx, wz = oz + lz;
					int sy = (int)std::round(naturalTerrainHeight(seed, (float)wx, (float)wz, m_tp));
					for (int ly = 0; ly < CHUNK_SIZE; ly++) {
						int wy = oy + ly;
						if (wy > sy) {
							if (wy <= wl) chunk.set(lx, ly, lz, bWater);
						} else if (wy == sy) {
							if      (sy <= wl)             chunk.set(lx, ly, lz, bSand);
							else if ((float)sy >= snowLine) chunk.set(lx, ly, lz, bSnow);
							else {
								chunk.set(lx, ly, lz, bGrass);
								chunk.setAppearance(lx, ly, lz, grassAppearance(wx, wz));
								// Tall-grass decoration immediately above, if the
								// +1 cell is within this chunk's local span and
								// would otherwise stay air.
								int plantLy = ly + 1;
								if (plantLy < CHUNK_SIZE && bTallGrass != BLOCK_AIR) {
									uint8_t app = tallGrassRoll(wx, wz);
									if (app) {
										chunk.set(lx, plantLy, lz, bTallGrass);
										chunk.setAppearance(lx, plantLy, lz, app);
									}
								}
							}
						} else if (wy > sy - dd) {
							chunk.set(lx, ly, lz, (sy <= wl + 1) ? bSand : bDirt);
						} else {
							chunk.set(lx, ly, lz, bStone);
						}
					}
				}
			}
		}

		// Trees (natural terrain only)
		auto vc = villageCenter(seed);
		glm::vec2 anchor = findAnchor(seed);
		int apx = (int)std::round(anchor.x), apz = (int)std::round(anchor.y);

		if (!isFlat && m_py.treeDensity > 0) {
			int wl = (int)m_py.waterLevel;
			float snowLine = m_py.snowThreshold;
			for (int lz = 3; lz < CHUNK_SIZE - 3; lz++) {
				for (int lx = 3; lx < CHUNK_SIZE - 3; lx++) {
					int wx = ox + lx, wz = oz + lz;
					if (hashFloat(wx * 7 + seed, wz * 13) < (1.0f - m_py.treeDensity)) continue;
					int sy = (int)std::round(naturalTerrainHeight(seed, (float)wx, (float)wz, m_tp));
					if (sy <= wl || (float)sy >= snowLine) continue;
					{ int dx = wx-vc.x, dz = wz-vc.y;
					  if (dx*dx+dz*dz < m_py.clearingRadius*m_py.clearingRadius) continue; }
					{ int dx = wx-apx, dz = wz-apz;
					  if (dx*dx+dz*dz < 100) continue; }
					int trunkBase = sy + 1;
					int trunkH = m_py.trunkHeightMin +
						(int)(hashFloat(wx+99, wz+99) * (m_py.trunkHeightMax - m_py.trunkHeightMin));
					int trunkTop = trunkBase + trunkH - 1;
					for (int ty = trunkBase; ty <= trunkTop; ty++) {
						int ly = ty - oy;
						if (ly >= 0 && ly < CHUNK_SIZE) chunk.set(lx, ly, lz, bLog);
					}
					// Worldgen runs per-chunk, and the same (wx,wz) is visited
					// in every stacked Y-chunk. Only the chunk whose Y range
					// contains trunkBase hosts a real tree — the rest would
					// push phantom spawns with no blocks. Early-out there.
					int anchorLy = trunkBase - oy;
					bool anchorInThisChunk = anchorLy >= 0 && anchorLy < CHUNK_SIZE;

					// Build the structure-spawn record alongside the leaf
					// sphere: every leaf placed here is recorded in world
					// coordinates so SeasonalLeaves can paint exactly this
					// tree's canopy (no BFS, no neighbor overlap).
					PendingStructureSpawn s;
					if (pendingSpawns && anchorInThisChunk) {
						s.blueprintId = "tree";
						s.entityType  = StructureName::Tree;
						s.anchorPos   = {wx, trunkBase, wz};
					}
					int leafR = m_py.leafRadius;
					for (int dy = -1; dy <= leafR; dy++) {
						int r = (dy == leafR) ? 1 : leafR;
						for (int dx = -r; dx <= r; dx++)
							for (int ddz = -r; ddz <= r; ddz++) {
								if (dx==0 && ddz==0 && dy < leafR-1) continue;
								if (dx*dx+ddz*ddz+dy*dy > leafR*leafR+1) continue;
								int lxx=lx+dx, lyy=trunkTop+dy-oy, lzz=lz+ddz;
								if (lxx>=0&&lxx<CHUNK_SIZE&&lyy>=0&&lyy<CHUNK_SIZE&&
								    lzz>=0&&lzz<CHUNK_SIZE&&chunk.get(lxx,lyy,lzz)==BLOCK_AIR) {
									chunk.set(lxx, lyy, lzz, bLeaves);
									if (pendingSpawns && anchorInThisChunk)
										s.leafPositions.push_back({ox+lxx, oy+lyy, oz+lzz});
								}
							}
					}
					if (pendingSpawns && anchorInThisChunk)
						pendingSpawns->push_back(std::move(s));
				}
			}
		}

		// Village blocks are no longer baked in at chunk-gen (Phase 4): the
		// VillageStamper applies them per-seat when a seat claims, iterating
		// over the footprint chunks and calling generateVillageInChunk().
		// Tree-exclusion above still uses villageCenter(seed) as a soft anchor
		// — that's fine, seat 1's village lands there by default and trees in
		// later seats' footprints are overwritten by the stamper.

		// Spawn portal, or bare SpawnPoint block for minimal test worlds.
		BlockId spawnPtB = blocks.getId(BlockType::SpawnPoint);
		if (m_py.hasPortal) {
			BlockId stairB   = blocks.getId(BlockType::Stair);
			BlockId planksB  = blocks.getId(BlockType::Planks);
			BlockId arcaneB  = blocks.getId(BlockType::ArcaneStone);
			BlockId portalB  = blocks.getId(BlockType::Portal);
			BlockId beeNestB = blocks.getId(BlockType::BeeNest);
			generatePortal(chunk, cpos, seed, wallB, bStone, stairB, planksB, arcaneB, portalB, spawnPtB, beeNestB, anchor);
		} else {
			placeBareSpawnBlock(chunk, cpos, seed, spawnPtB, anchor);
		}
	}

	// Platform blocks above groundY. Used in preferredSpawn + generatePortal.
	static constexpr int kPlatH = 5;

private:
	WorldPyConfig m_py;
	TerrainParams m_tp;

	float groundHeight(int seed, float x, float z) const {
		if (m_py.terrainType == "flat") return m_py.surfaceY;
		return naturalTerrainHeight(seed, x, z, m_tp);
	}

	// Thread-safe for reads after construction.
	mutable std::unordered_map<int, glm::vec2> m_anchorCache;

	// Flat: fixed from config. Natural: spiral search for level ground.
	glm::vec2 findAnchor(int seed) const {
		auto it = m_anchorCache.find(seed);
		if (it != m_anchorCache.end()) return it->second;

		float sx = m_py.spawnSearchX, sz = m_py.spawnSearchZ;
		if (m_py.terrainType != "flat") {
			for (int t = 0; t < 120; t++) {
				float h = naturalTerrainHeight(seed, sx, sz, m_tp);
				if (h >= m_py.spawnMinH && h <= m_py.spawnMaxH) break;
				float angle = (float)t * 2.399963f;
				float r     = (float)t * 4.0f;
				sx = m_py.spawnSearchX + std::cos(angle) * r;
				sz = m_py.spawnSearchZ + std::sin(angle) * r;
			}
		}
		m_anchorCache[seed] = {sx, sz};
		return {sx, sz};
	}

	// Single SpawnPoint block at anchor ground, no surround. Player stands on top.
	void placeBareSpawnBlock(Chunk& chunk, ChunkPos cpos, int seed,
	                         BlockId spawnPtB, glm::vec2 anchor) {
		if (spawnPtB == BLOCK_AIR) return;
		int ox = cpos.x * CHUNK_SIZE;
		int oy = cpos.y * CHUNK_SIZE;
		int oz = cpos.z * CHUNK_SIZE;
		int px = (int)std::round(anchor.x);
		int pz = (int)std::round(anchor.y);
		int groundY = (m_py.terrainType == "flat")
			? (int)m_py.surfaceY
			: (int)std::round(naturalTerrainHeight(seed, (float)px, (float)pz, m_tp));
		int lx = px - ox, ly = groundY - oy, lz = pz - oz;
		if (lx >= 0 && lx < CHUNK_SIZE &&
		    ly >= 0 && ly < CHUNK_SIZE &&
		    lz >= 0 && lz < CHUNK_SIZE)
			chunk.set(lx, ly, lz, spawnPtB);
	}

	// Spawn altar: open-air raised stone platform with pillars (no roof/walls)
	// so overhead views stay clear. Layout centered on anchor:
	//   Platform 13x8x5, altar back row 3 tall with portal inlay,
	//   4 pillars/side 7 tall, 7-wide stairs on +Z, spawn block at (0,-1).
	// Portal inlay on back altar → player faces +Z toward stairs (away from portal).
	void generatePortal(Chunk& chunk, ChunkPos cpos, int seed,
	                    BlockId wallB, BlockId stoneB, BlockId stairB,
	                    BlockId planksB, BlockId arcaneB, BlockId portalB,
	                    BlockId spawnPtB, BlockId beeNestB, glm::vec2 anchor) {
		int ox = cpos.x * CHUNK_SIZE;
		int oy = cpos.y * CHUNK_SIZE;
		int oz = cpos.z * CHUNK_SIZE;

		int px = (int)std::round(anchor.x);
		int pz = (int)std::round(anchor.y);
		int groundY = portalGroundY(seed);

		auto columnTerrainY = [&](int wx, int wz) -> int {
			if (m_py.terrainType == "flat") return (int)m_py.surfaceY;
			return (int)std::round(naturalTerrainHeight(seed, (float)wx, (float)wz, m_tp));
		};

		auto set = [&](int wx, int wy, int wz, BlockId bid, uint8_t p2 = 0) {
			int lx = wx - ox, ly = wy - oy, lz = wz - oz;
			if (lx >= 0 && lx < CHUNK_SIZE &&
			    ly >= 0 && ly < CHUNK_SIZE &&
			    lz >= 0 && lz < CHUNK_SIZE)
				chunk.set(lx, ly, lz, bid, p2);
		};

		if (stoneB  == BLOCK_AIR) stoneB  = wallB;
		if (planksB == BLOCK_AIR) planksB = stoneB;
		if (stairB  == BLOCK_AIR) stairB  = wallB;
		if (arcaneB == BLOCK_AIR) arcaneB = stoneB;

		constexpr int platH      = kPlatH;
		constexpr int openHW     = 3;        // stair half-width (7 wide)
		constexpr int platHW     = 6;        // platform half-width (13 wide)
		constexpr int backDZ     = -4;       // altar row
		constexpr int frontDZ    = +3;       // stair start
		constexpr int pillarHW   = 5;
		constexpr int pillarOW   = 6;
		constexpr int pillarH    = 7;
		constexpr int altarH     = 3;
		constexpr int numSteps   = platH;

		const int platSurfY = groundY + platH;
		const int pillarTopY = platSurfY + pillarH - 1;
		const int capY       = pillarTopY + 1;

		// 1. Wipe envelope so old gen / trees don't leak through.
		for (int dy = 0; dy <= pillarH + 3; dy++)
			for (int dx = -(pillarOW + 1); dx <= pillarOW + 1; dx++)
				for (int dz = backDZ - 1; dz <= frontDZ + numSteps + 3; dz++)
					set(px + dx, groundY + dy, pz + dz, BLOCK_AIR);

		// 2. Platform foundation (stone fill to ground).
		for (int dx = -platHW; dx <= platHW; dx++) {
			for (int dz = backDZ; dz <= frontDZ; dz++) {
				int colY = columnTerrainY(px + dx, pz + dz);
				int fillBot = std::min(colY, groundY);
				for (int wy = fillBot; wy < platSurfY - 1; wy++)
					set(px + dx, wy, pz + dz, wallB);
				// Plank inner aisle, stone trim outside.
				bool inner = (std::abs(dx) <= openHW);
				set(px + dx, platSurfY - 1, pz + dz, inner ? planksB : stoneB);
			}
		}

		// 2b. Spawn block near altar → player faces the opening.
		if (spawnPtB != BLOCK_AIR)
			set(px, platSurfY - 1, pz - 1, spawnPtB);

		// 3. Back altar: stone frame at dz=backDZ with portal inlay at dx=-2..+2.
		for (int dx = -openHW; dx <= openHW; dx++)
			for (int h = 0; h < altarH; h++)
				set(px + dx, platSurfY + h, pz + backDZ, stoneB);
		if (portalB != BLOCK_AIR) {
			for (int dx = -2; dx <= 2; dx++)
				for (int h = 0; h < altarH; h++)
					set(px + dx, platSurfY + h, pz + backDZ, portalB);
		}
		// Arcane cap ridge.
		for (int dx = -openHW; dx <= openHW; dx++)
			set(px + dx, platSurfY + altarH, pz + backDZ, arcaneB);

		// 4. Colonnade: 4 pillars/side, 2×2 shaft with arcane mid-ring + capital.
		constexpr int kPillarZs[] = {-3, -1, +1, +3};
		for (int sign : {-1, 1}) {
			for (int pz0 : kPillarZs) {
				for (int pw = pillarHW; pw <= pillarOW; pw++) {
					for (int h = 0; h < pillarH; h++) {
						BlockId bid = (h == pillarH / 2) ? arcaneB : stoneB;
						set(px + sign * pw, platSurfY + h, pz + pz0, bid);
					}
				}
				// Capital extends inward by 1 → "pillar with bracket" silhouette.
				int capInner = (pillarHW - 1) * sign;
				int capOuter = pillarOW * sign;
				int capLo = std::min(capInner, capOuter);
				int capHi = std::max(capInner, capOuter);
				for (int dx = capLo; dx <= capHi; dx++)
					set(px + dx, capY, pz + pz0, arcaneB);
				for (int dx = capLo; dx <= capHi; dx++)
					set(px + dx, capY, pz + pz0 + 0, arcaneB);
			}
		}

		// 4b. One bee nest on a capital — signals "bees live here".
		if (beeNestB != BLOCK_AIR)
			set(px - pillarHW, capY + 1, pz + 3, beeNestB);

		// 5. Staircase. param2=2: stair rises -Z, player walks +Z to descend.
		for (int i = 0; i < numSteps; i++) {
			int stepY  = groundY + platH - 1 - i;
			int stepDZ = frontDZ + 1 + i;
			for (int dx = -openHW; dx <= openHW; dx++) {
				set(px + dx, stepY, pz + stepDZ, stairB, /*param2=*/2);
				int fillBot = std::min(columnTerrainY(px + dx, pz + stepDZ), groundY);
				for (int fy = fillBot; fy < stepY; fy++)
					set(px + dx, fy, pz + stepDZ, wallB);
			}
		}

		// 6. Stair side rails.
		for (int sign : {-1, 1}) {
			for (int i = 0; i < numSteps; i++) {
				int stepY  = groundY + platH - 1 - i;
				int stepDZ = frontDZ + 1 + i;
				int fillBot = std::min(columnTerrainY(px + sign * (openHW + 1), pz + stepDZ), groundY);
				for (int wy = fillBot; wy <= stepY; wy++)
					set(px + sign * (openHW + 1), wy, pz + stepDZ, stoneB);
			}
		}
	}

	// Chunk + offsets with bounds-checked world-coord set/get.
	struct GenCtx {
		Chunk& chunk;
		int ox, oy, oz;

		void set(int wx, int wy, int wz, BlockId bid, uint8_t p2 = 0) const {
			int lx = wx-ox, ly = wy-oy, lz = wz-oz;
			if (lx>=0&&lx<CHUNK_SIZE&&ly>=0&&ly<CHUNK_SIZE&&lz>=0&&lz<CHUNK_SIZE)
				chunk.set(lx,ly,lz,bid,p2);
		}
		BlockId get(int wx, int wy, int wz) const {
			int lx = wx-ox, ly = wy-oy, lz = wz-oz;
			if (lx>=0&&lx<CHUNK_SIZE&&ly>=0&&ly<CHUNK_SIZE&&lz>=0&&lz<CHUNK_SIZE)
				return chunk.get(lx,ly,lz);
			return BLOCK_AIR;
		}
	};

	// Reusable passage carvers. Each self-validates via world_accessibility.h
	// and warns to stderr on insufficient geometry.

	// doorBlockRows of doorB at bottom, air above; validates head clearance ≥ ceil(playerH).
	void carveEntrance(const GenCtx& ctx, const BlockRegistry& blocks,
	                   int doorX, int floorY, int doorZ,
	                   int width, int dh, int doorBlockRows,
	                   BlockId doorB, float playerH = 2.5f) const
	{
		for (int dx = 0; dx < width; dx++) {
			for (int dy = 0; dy < dh; dy++) {
				BlockId bid = (dy < doorBlockRows) ? doorB : BLOCK_AIR;
				ctx.set(doorX+dx, floorY+dy, doorZ, bid);
			}
			// Validate
			auto getBlock = [&](int x, int y, int z){ return ctx.get(x,y,z); };
			auto err = checkDoorColumn(getBlock, blocks, doorX+dx, floorY, doorZ, playerH);
			if (!err.empty())
				fprintf(stderr, "[WorldGen] carveEntrance: %s\n", err.c_str());
		}
	}

	// Stairway +Z, (stories-1) flights × sh steps. Opens intermFloor where
	// head clearance would clip, validates every step.
	void carveStairway(const GenCtx& ctx, const BlockRegistry& blocks,
	                   int stairX, int floorY, int baseZ,
	                   int sh, int stories, int stairW, int openW,
	                   BlockId stairB, float playerH = 2.5f, float margin = 0.25f) const
	{
		auto getBlock = [&](int x, int y, int z){ return ctx.get(x,y,z); };

		for (int s = 0; s < stories-1; s++) {
			int intermY = floorY + s*sh + sh - 1;

			// Phase 1: open ceiling where head clips.
			for (int i = 0; i < sh; i++) {
				float feetY  = (float)(floorY + s*sh + i) + 0.5f;
				float headY  = feetY + playerH + margin;
				if (headY > (float)intermY) {
					for (int ddx = 0; ddx < openW; ddx++)
						ctx.set(stairX + ddx, intermY, baseZ+2+i, BLOCK_AIR);
				}
			}

			// Phase 2: place stairs (after clearing so stairB wins at intermY row).
			// param2=0: rises +Z, tread on -Z. Player walks +Z to ascend.
			for (int i = 0; i < sh; i++) {
				for (int ddx = 0; ddx < stairW; ddx++)
					ctx.set(stairX+ddx, floorY+s*sh+i, baseZ+2+i, stairB, /*param2=*/0);
			}

			// Phase 3: validate.
			for (int i = 0; i < sh; i++) {
				for (int ddx = 0; ddx < stairW; ddx++) {
					auto err = checkStairBlock(getBlock, blocks,
					                           stairX+ddx, floorY+s*sh+i, baseZ+2+i,
					                           playerH, margin);
					if (!err.empty())
						fprintf(stderr, "[WorldGen] carveStairway: %s\n", err.c_str());
				}
			}
		}
	}

	// House: 2-wide staircase (dx=1..2), 4-wide opening for 2.5-tall player clearance.
	// Foundation: clears local terrain up to floorY-1, fills stone down to minGroundY.
	void placeFoundation(const GenCtx& ctx, int seed, BlockId stoneB,
	                     int hcx, int hcz, int w, int d,
	                     int floorY, int minGroundY) const {
		for (int dx = 0; dx < w; dx++) {
			for (int dz = 0; dz < d; dz++) {
				int localGY = (int)std::round(groundHeight(seed, (float)(hcx+dx), (float)(hcz+dz)));
				for (int y = localGY + 1; y < floorY; y++)
					ctx.set(hcx+dx, y, hcz+dz, BLOCK_AIR);
				for (int y = floorY - 1; y >= minGroundY; y--)
					ctx.set(hcx+dx, y, hcz+dz, stoneB);
			}
		}
	}

	// Clear the volume a building is about to occupy, so that a pre-existing
	// tree (logs, leaves) or any terrain block inside the envelope gets
	// overwritten with AIR *before* the structure generator writes its walls.
	// Without this, generateHouse/generateBarn only stamp the blocks they
	// care about and stray tree blocks persist inside the interior — visible
	// as "a tree in the barn" and a trap for villager pathing (the executor
	// picks those blocks as harvest anchors and wedges against walls).
	//
	// Widens Z by 1 on each side to cover the gable roof overhang that
	// both generateHouse and generateBarn emit at dz=-1 and dz=h.d.
	void clearBuildingEnvelope(const GenCtx& ctx,
	                           int hcx, int hcz, int w, int d,
	                           int yMin, int yMax) const {
		for (int y = yMin; y <= yMax; y++) {
			for (int dx = 0; dx < w; dx++) {
				for (int dz = -1; dz <= d; dz++) {
					ctx.set(hcx + dx, y, hcz + dz, BLOCK_AIR);
				}
			}
		}
	}

	void generateHouse(const GenCtx& ctx, int seed,
	                   BlockId wallB, BlockId roofB, BlockId floorB, BlockId stairB,
	                   BlockId glassB, BlockId doorB, BlockId stoneB,
	                   const WorldPyConfig::HouseLayout& h, glm::ivec2 vc,
	                   int floorY) {
		int sh = m_py.storyHeight, dh = m_py.doorHeight, wr = m_py.windowRow;
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		int totalH = sh * h.stories;

		// Walls, interior, stairs, intermediate floors
		for (int dy = 0; dy < totalH; dy++) {
			for (int dx = 0; dx < h.w; dx++) {
				for (int dz = 0; dz < h.d; dz++) {
					bool wall   = (dx==0||dx==h.w-1||dz==0||dz==h.d-1);
					bool door   = ((dx==h.w/2||dx==h.w/2-1)&&dz==0&&dy<dh);
					// 3w × 2h window groups, two per wall face per story.
					bool windowRow = false;
					for (int s = 0; s < h.stories && !windowRow; s++)
						if (dy == s*sh + wr || dy == s*sh + wr + 1) windowRow = true;
					bool window = wall && windowRow && (
						((dz==0||dz==h.d-1)&&((dx>=1&&dx<=3)||(dx>=h.w-4&&dx<=h.w-2)))||
						((dx==0||dx==h.w-1)&&((dz>=1&&dz<=3)||(dz>=h.d-4&&dz<=h.d-2))));

					// Ceiling of story s = floor of story s+1.
					bool intermFloor = false;
					if (h.stories >= 2)
						for (int s = 1; s < h.stories; s++)
							if (dy == s*sh-1) { intermFloor = true; break; }

					BlockId bid;
					uint8_t p2 = 0;
					if (door) {
						bid = doorB;
						// Mirror hinge: right col (h.w/2) opens +X, left (h.w/2-1) opens -X.
						p2 = (dx == h.w/2) ? 0x4 : 0;
					}
					else if (window)           bid = glassB;
					else if (intermFloor)                    bid = floorB;
					else if (dy == totalH-1)                 bid = roofB;
					else if (wall)                           bid = wallB;
					else                                     bid = BLOCK_AIR;

					ctx.set(hcx+dx, floorY+dy, hcz+dz, bid, p2);
				}
			}
		}

		// Post-pass 1: punch 2×(sh-1) opening above stairs through each floor.
		if (h.stories >= 2) {
			for (int s = 0; s < h.stories - 1; s++) {
				int ceilDy = (s + 1) * sh - 1;
				for (int i = 0; i < sh - 1; i++) {
					for (int dx = 1; dx <= 2; dx++)
						ctx.set(hcx + dx, floorY + ceilDy, hcz + (2 + i), BLOCK_AIR);
				}
			}
		}

		// Post-pass 2: stairs last so earlier passes never overwrite them.
		if (h.stories >= 2) {
			for (int s = 0; s < h.stories - 1; s++) {
				for (int i = 0; i < sh; i++) {
					for (int dx = 1; dx <= 2; dx++)
						ctx.set(hcx + dx, floorY + s*sh + i, hcz + (2 + i), stairB, 0);
				}
			}
		}

		// Peaked gable, 1-block front/back overhang.
		int roofLayers = (h.w+2)/2;
		for (int ry = 0; ry < roofLayers; ry++) {
			for (int dz = -1; dz <= h.d; dz++) {
				for (int dx = ry; dx < h.w-ry; dx++) {
					bool gableEnd = (dz==0||dz==h.d-1);
					bool roofEdge = (dx==ry||dx==h.w-ry-1||ry==roofLayers-1);
					if (gableEnd||roofEdge)
						ctx.set(hcx+dx, floorY+totalH+ry, hcz+dz, roofB);
				}
			}
		}
	}

	// 5×3 platform at floorY-1 keeps door unblocked; fills below with wall material.
	void generatePorch(const GenCtx& ctx, int seed,
	                   BlockId pathB, BlockId wallB,
	                   const WorldPyConfig::HouseLayout& h, glm::ivec2 vc,
	                   int floorY) {
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		int dh = m_py.doorHeight;
		int doorMid = h.w / 2;

		for (int pDz = 1; pDz <= 3; pDz++) {
			int wz = hcz - pDz;
			for (int pDx = doorMid - 2; pDx <= doorMid + 2; pDx++) {
				int wx = hcx + pDx;

				ctx.set(wx, floorY-1, wz, pathB);

				// Support pillars if terrain is concave.
				for (int sy = floorY-2; sy >= floorY-6; sy--) {
					if (ctx.get(wx, sy, wz) != BLOCK_AIR) break;
					ctx.set(wx, sy, wz, wallB);
				}

				// Clear hills up to door height.
				for (int cy = floorY; cy < floorY+dh; cy++)
					ctx.set(wx, cy, wz, BLOCK_AIR);
			}
		}
	}

	// Bed + table + chairs + chest. House[0] chest skipped — server.h places it
	// via houseChestPositions() to avoid double-set.
	void generateFurniture(const GenCtx& ctx, int seed,
	                       BlockId woodB, BlockId planksB, BlockId bedB, BlockId chestB,
	                       const WorldPyConfig::HouseLayout& h, glm::ivec2 vc,
	                       bool isMainHouse, int floorY) {
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;

		// Bed: back-left, foot→head on -Z.
		{
			int bx = hcx + 2, bz1 = hcz + h.d - 2, bz2 = hcz + h.d - 3;
			ctx.set(bx, floorY, bz1, bedB);
			ctx.set(bx, floorY, bz2, bedB);
		}

		if (!isMainHouse && chestB != BLOCK_AIR) {
			ctx.set(hcx + h.w - 3, floorY, hcz + 1, chestB);
		}

		{
			int tx = hcx + h.w/2 + 1;
			int tz = hcz + h.d/2;
			ctx.set(tx,   floorY, tz,   planksB != BLOCK_AIR ? planksB : woodB);
			ctx.set(tx+1, floorY, tz,   planksB != BLOCK_AIR ? planksB : woodB);
			ctx.set(tx,   floorY, tz-1, woodB);
			ctx.set(tx+1, floorY, tz+1, woodB);
		}
	}

	void generatePaths(const GenCtx& ctx, int seed,
	                   BlockId pathB, glm::ivec2 vc) {
		for (int dz = -22; dz <= 26; dz++) {
			int wx = vc.x + 2, wz = vc.y + dz;
			int surfY = (int)std::round(groundHeight(seed, (float)wx, (float)wz));
			ctx.set(wx,   surfY, wz, pathB);
			ctx.set(wx+1, surfY, wz, pathB);
		}
	}

	// Open-sided barn: corner + mid pillars, peaked gable roof.
	void generateBarn(const GenCtx& ctx, int seed,
	                  BlockId woodB, BlockId planksB, BlockId roofB, BlockId stoneB,
	                  const WorldPyConfig::HouseLayout& h, glm::ivec2 vc,
	                  int floorY) {
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		int barnH = 9;
		BlockId col = (woodB != BLOCK_AIR) ? woodB : planksB;

		// 1. Plank floor (foundation placed by caller).
		for (int dx = 0; dx < h.w; dx++)
			for (int dz = 0; dz < h.d; dz++)
				ctx.set(hcx+dx, floorY-1, hcz+dz, planksB != BLOCK_AIR ? planksB : col);

		// 2. Corner pillars.
		for (int dy = 0; dy < barnH; dy++) {
			ctx.set(hcx,       floorY+dy, hcz,       col);
			ctx.set(hcx+h.w-1, floorY+dy, hcz,       col);
			ctx.set(hcx,       floorY+dy, hcz+h.d-1, col);
			ctx.set(hcx+h.w-1, floorY+dy, hcz+h.d-1, col);
		}

		// 3. Mid pillars every 4 blocks.
		for (int dy = 0; dy < barnH; dy++) {
			for (int dx = 4; dx < h.w - 1; dx += 4) {
				ctx.set(hcx+dx, floorY+dy, hcz,       col);
				ctx.set(hcx+dx, floorY+dy, hcz+h.d-1, col);
			}
			for (int dz = 4; dz < h.d - 1; dz += 4) {
				ctx.set(hcx,       floorY+dy, hcz+dz, col);
				ctx.set(hcx+h.w-1, floorY+dy, hcz+dz, col);
			}
		}

		// 4. Top beams.
		for (int dx = 0; dx < h.w; dx++) {
			ctx.set(hcx+dx, floorY+barnH-1, hcz,       col);
			ctx.set(hcx+dx, floorY+barnH-1, hcz+h.d-1, col);
		}
		for (int dz = 0; dz < h.d; dz++) {
			ctx.set(hcx,       floorY+barnH-1, hcz+dz, col);
			ctx.set(hcx+h.w-1, floorY+barnH-1, hcz+dz, col);
		}

		// 5. Peaked gable, ridge along Z.
		int roofLayers = (h.w + 2) / 2;
		for (int ry = 0; ry < roofLayers; ry++) {
			for (int dz = -1; dz <= h.d; dz++) {
				for (int dx = ry; dx < h.w - ry; dx++) {
					bool gableEnd = (dz == 0 || dz == h.d - 1);
					bool roofEdge = (dx == ry || dx == h.w - ry - 1 || ry == roofLayers - 1);
					if (gableEnd || roofEdge)
						ctx.set(hcx+dx, floorY+barnH+ry, hcz+dz, roofB);
				}
			}
		}
	}

	void generateVillage(Chunk& chunk, ChunkPos cpos, int seed,
	                     BlockId wallB, BlockId roofB, BlockId floorB, BlockId pathB,
	                     glm::ivec2 vc, const BlockRegistry& blocks) {
		GenCtx ctx{chunk, cpos.x*CHUNK_SIZE, cpos.y*CHUNK_SIZE, cpos.z*CHUNK_SIZE};

		BlockId woodB   = blocks.getId(BlockType::Wood);
		BlockId planksB = blocks.getId(BlockType::Planks);
		BlockId bedB    = blocks.getId(BlockType::Bed);
		BlockId chestB  = blocks.getId(BlockType::Chest);
		BlockId stairB  = blocks.getId(BlockType::Stair);
		BlockId glassB  = blocks.getId(BlockType::Glass);
		// Stamp doors in the open state so villagers can path through on
		// first approach (closed-door collision would wedge the Navigator
		// until someone toggled it). Same param2 layout as Door — DoorOpen's
		// mesher reads the same hinge bit (bit 2).
		BlockId doorB   = blocks.getId(BlockType::DoorOpen);
		if (stairB == BLOCK_AIR) stairB = floorB;
		if (glassB == BLOCK_AIR) glassB = BLOCK_AIR;  // → open hole
		if (doorB  == BLOCK_AIR) doorB  = BLOCK_AIR;  // → open hole

		BlockId stoneB = blocks.getId(BlockType::Stone);
		if (stoneB == BLOCK_AIR) stoneB = blocks.getId(BlockType::Cobblestone);

		for (int hi = 0; hi < (int)m_py.houses.size(); hi++) {
			const auto& h = m_py.houses[hi];
			int hcx = vc.x + h.cx, hcz = vc.y + h.cz;

			// floorY = footprint max+1 so building sits on hill, never buried.
			auto [minGY, maxGY] = footprintHeightRange(seed, hcx, hcz, h.w, h.d);
			int floorY = maxGY + 1;
			placeFoundation(ctx, seed, stoneB, hcx, hcz, h.w, h.d, floorY, minGY);

			// Clear the interior + roof volume BEFORE any structure writes,
			// so pre-existing trees inside the footprint can't survive as
			// stray logs/leaves inside a wall or under a roof.
			int roofLayers = (h.w + 2) / 2;
			int envelopeTop;
			if (h.type == "barn") {
				constexpr int kBarnBodyH = 9;   // matches generateBarn's barnH
				envelopeTop = floorY + kBarnBodyH + roofLayers;
			} else {
				envelopeTop = floorY + m_py.storyHeight * h.stories + roofLayers;
			}
			clearBuildingEnvelope(ctx, hcx, hcz, h.w, h.d, floorY, envelopeTop);

			if (h.type == "barn") {
				BlockId hRoofB = (!h.roofBlock.empty()) ? blocks.getId(h.roofBlock) : roofB;
				if (hRoofB == BLOCK_AIR) hRoofB = roofB;
				generateBarn(ctx, seed, woodB, planksB, hRoofB, stoneB, h, vc, floorY);
				continue;
			}

			BlockId hWallB = (!h.wallBlock.empty()) ? blocks.getId(h.wallBlock) : wallB;
			BlockId hRoofB = (!h.roofBlock.empty()) ? blocks.getId(h.roofBlock) : roofB;
			if (hWallB == BLOCK_AIR) hWallB = wallB;
			if (hRoofB == BLOCK_AIR) hRoofB = roofB;

			generateHouse(ctx, seed, hWallB, hRoofB, floorB, stairB, glassB, doorB, stoneB, h, vc, floorY);
			generatePorch(ctx, seed, pathB, hWallB, h, vc, floorY);
			generateFurniture(ctx, seed, woodB, planksB, bedB, chestB, h, vc, hi == 0, floorY);
		}

		generatePaths(ctx, seed, pathB, vc);

		// Farm plot
		BlockId farmlandB = blocks.getId(BlockType::Farmland);
		BlockId wheatB    = blocks.getId(BlockType::Wheat);
		if (farmlandB != BLOCK_AIR) {
			int fx = vc.x - 12, fz = vc.y + 16;
			int fy = (int)std::round(groundHeight(seed, (float)fx+3, (float)fz+3)) + 1;
			for (int dx = 0; dx < 6; dx++) {
				for (int dz = 0; dz < 6; dz++) {
					ctx.set(fx+dx, fy-1, fz+dz, farmlandB);
					if (wheatB != BLOCK_AIR)
						ctx.set(fx+dx, fy, fz+dz, wheatB);
				}
			}
			// Center water channel.
			BlockId waterB = blocks.getId(BlockType::Water);
			if (waterB != BLOCK_AIR) {
				for (int dz = 0; dz < 6; dz++) {
					ctx.set(fx+3, fy-1, fz+dz, waterB);
				}
			}
		}

		// Animal pen
		BlockId fenceB = blocks.getId(BlockType::Fence);
		if (fenceB != BLOCK_AIR) {
			int px = vc.x + 16, pzz = vc.y - 18;
			auto [penMinGY, penMaxGY] = footprintHeightRange(seed, px, pzz, 10, 8);
			int py = penMaxGY + 1;
			placeFoundation(ctx, seed, stoneB, px, pzz, 10, 8, py, penMinGY);
			// 10×8 fence perimeter.
			for (int dx = 0; dx < 10; dx++) {
				ctx.set(px+dx, py, pzz,   fenceB);
				ctx.set(px+dx, py, pzz+7, fenceB);
			}
			for (int dz = 1; dz < 7; dz++) {
				ctx.set(px,   py, pzz+dz, fenceB);
				ctx.set(px+9, py, pzz+dz, fenceB);
			}
			// 2-wide gate.
			ctx.set(px+4, py, pzz, BLOCK_AIR);
			ctx.set(px+5, py, pzz, BLOCK_AIR);
		}

		// Village-center monument: arcane trident tower, visible from spawn.
		{
			BlockId arcaneB = blocks.getId(BlockType::ArcaneStone);
			BlockId glassB  = blocks.getId(BlockType::Glass);
			BlockId portalB = blocks.getId(BlockType::Portal);
			BlockId fenceB  = blocks.getId(BlockType::Fence);
			if (arcaneB == BLOCK_AIR) arcaneB = stoneB;
			if (glassB  == BLOCK_AIR) glassB  = arcaneB;
			if (portalB == BLOCK_AIR) portalB = arcaneB;
			if (fenceB  == BLOCK_AIR) fenceB  = stoneB;

			int mx = vc.x, mz = vc.y;
			int my = (int)std::round(groundHeight(seed, (float)mx, (float)mz)) + 1;

			// Stepped pyramid base (r=3,2,1).
			for (int tier = 0; tier < 3; tier++) {
				int r = 3 - tier;
				BlockId tb = (tier == 0) ? stoneB : arcaneB;
				for (int dx = -r; dx <= r; dx++)
					for (int dz = -r; dz <= r; dz++)
						ctx.set(mx+dx, my+tier, mz+dz, tb);
			}

			// 5×5 hollow tower body, 18 tall.
			constexpr int towerH = 18;
			int bodyBase = my + 3;
			for (int dy = 0; dy < towerH; dy++) {
				int y = bodyBase + dy;
				for (int dx = -2; dx <= 2; dx++)
					for (int dz = -2; dz <= 2; dz++) {
						bool edge = (std::abs(dx) == 2 || std::abs(dz) == 2);
						bool corner = (std::abs(dx) == 2 && std::abs(dz) == 2);
						if (corner) {
							// Corners alternate arcane/stone every 3.
							ctx.set(mx+dx, y, mz+dz, (dy % 3 == 0) ? stoneB : arcaneB);
						} else if (edge) {
							ctx.set(mx+dx, y, mz+dz, arcaneB);
						} else {
							ctx.set(mx+dx, y, mz+dz, BLOCK_AIR);
						}
					}
			}

			// Portal glow rings (base, 1/3, 2/3).
			for (int ring : {0, 6, 12}) {
				int y = bodyBase + ring;
				for (int dx = -2; dx <= 2; dx++)
					for (int dz = -2; dz <= 2; dz++) {
						bool edge = (std::abs(dx) == 2 || std::abs(dz) == 2);
						bool corner = (std::abs(dx) == 2 && std::abs(dz) == 2);
						if (edge && !corner)
							ctx.set(mx+dx, y, mz+dz, portalB);
					}
			}

			// Glass windows center-column on each face.
			for (int winY : {4, 9, 14}) {
				int y = bodyBase + winY;
				ctx.set(mx, y,   mz-2, glassB);
				ctx.set(mx, y+1, mz-2, glassB);
				ctx.set(mx, y,   mz+2, glassB);
				ctx.set(mx, y+1, mz+2, glassB);
				ctx.set(mx-2, y,   mz, glassB);
				ctx.set(mx-2, y+1, mz, glassB);
				ctx.set(mx+2, y,   mz, glassB);
				ctx.set(mx+2, y+1, mz, glassB);
			}

			// Door on -Z face.
			for (int dy = 0; dy <= 2; dy++)
				ctx.set(mx, bodyBase+dy, mz-2, BLOCK_AIR);

			// 7×7 observation deck + portal inlay perimeter.
			int deckY = bodyBase + towerH;
			for (int dx = -3; dx <= 3; dx++)
				for (int dz = -3; dz <= 3; dz++)
					ctx.set(mx+dx, deckY, mz+dz, stoneB);
			for (int dx = -2; dx <= 2; dx++) {
				ctx.set(mx+dx, deckY, mz-3, portalB);
				ctx.set(mx+dx, deckY, mz+3, portalB);
			}
			for (int dz = -2; dz <= 2; dz++) {
				ctx.set(mx-3, deckY, mz+dz, portalB);
				ctx.set(mx+3, deckY, mz+dz, portalB);
			}

			// Fence railing.
			for (int dx = -3; dx <= 3; dx++) {
				ctx.set(mx+dx, deckY+1, mz-3, fenceB);
				ctx.set(mx+dx, deckY+1, mz+3, fenceB);
			}
			for (int dz = -2; dz <= 2; dz++) {
				ctx.set(mx-3, deckY+1, mz+dz, fenceB);
				ctx.set(mx+3, deckY+1, mz+dz, fenceB);
			}

			// Trident crown: 3 prongs along X, portal tips.
			int tridentBase = deckY + 1;
			for (int dy = 1; dy <= 10; dy++)
				ctx.set(mx, tridentBase+dy, mz, arcaneB);
			ctx.set(mx, tridentBase+11, mz, portalB);

			for (int dy = 1; dy <= 3; dy++)
				ctx.set(mx-1, tridentBase+dy, mz, arcaneB);
			ctx.set(mx-2, tridentBase+4, mz, arcaneB);
			ctx.set(mx-2, tridentBase+5, mz, arcaneB);
			ctx.set(mx-3, tridentBase+6, mz, arcaneB);
			ctx.set(mx-3, tridentBase+7, mz, arcaneB);
			ctx.set(mx-3, tridentBase+8, mz, portalB);

			for (int dy = 1; dy <= 3; dy++)
				ctx.set(mx+1, tridentBase+dy, mz, arcaneB);
			ctx.set(mx+2, tridentBase+4, mz, arcaneB);
			ctx.set(mx+2, tridentBase+5, mz, arcaneB);
			ctx.set(mx+3, tridentBase+6, mz, arcaneB);
			ctx.set(mx+3, tridentBase+7, mz, arcaneB);
			ctx.set(mx+3, tridentBase+8, mz, portalB);

			// Crossbar.
			for (int dx = -2; dx <= 2; dx++)
				ctx.set(mx+dx, tridentBase+3, mz, arcaneB);

			// Corner portal orbs.
			for (int sx : {-3, 3})
				for (int sz : {-3, 3})
					ctx.set(mx+sx, deckY+2, mz+sz, portalB);
		}

		// Village pond
		{
			BlockId waterB = blocks.getId(BlockType::Water);
			BlockId sandB  = blocks.getId(BlockType::Sand);
			int pdx = vc.x + 3, pdz = vc.y + 3;
			int pdy = (int)std::round(groundHeight(seed, (float)pdx+2, (float)pdz+2));
			if (waterB != BLOCK_AIR) {
				// 5×5 pond, 2 deep, sand rim.
				for (int dx = -1; dx <= 5; dx++)
					for (int dz = -1; dz <= 5; dz++) {
						bool rim = (dx == -1 || dx == 5 || dz == -1 || dz == 5);
						if (rim) {
							ctx.set(pdx+dx, pdy, pdz+dz, sandB);
						} else {
							ctx.set(pdx+dx, pdy,   pdz+dz, waterB);
							ctx.set(pdx+dx, pdy-1, pdz+dz, sandB);
						}
					}
			}
		}
	}
};

} // namespace civcraft
