#pragma once

#include "shared/types.h"
#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/constants.h"
#include "server/noise.h"
#include "server/world_gen_config.h"
#include "server/python_bridge.h"
#include <string>
#include <cmath>
#include <unordered_map>

namespace agentworld {

// ============================================================
// Base class for world generation templates
// ============================================================
class WorldTemplate {
public:
	virtual ~WorldTemplate() = default;

	virtual std::string name()        const = 0;
	virtual std::string description() const = 0;

	// Generate terrain for a single chunk
	virtual void generate(Chunk& chunk, ChunkPos cpos, int seed,
	                      const BlockRegistry& blocks) = 0;

	// Surface height at world XZ (for entity/spawn placement)
	virtual float surfaceHeight(int seed, float x, float z) = 0;

	// Preferred player spawn position — templates return a safe, visible spot
	virtual glm::vec3 preferredSpawn(int seed) const = 0;

	// Preferred chest position — templates place the starter chest here
	// (spawnPos provided in case chest should be relative to it)
	virtual glm::vec3 chestPosition(int seed, glm::vec3 spawnPos) const = 0;

	// Village / POI center — used for mob spawn centering
	// Returns {spawnPos.x, spawnPos.z} if this template has no village
	virtual glm::ivec2 villageCenter(int seed) const = 0;

	// Mob spawn list with per-mob radius (populated from Python config or defaults)
	virtual const WorldPyConfig& pyConfig() const = 0;
};

// ============================================================
// Flat World
// ============================================================
class FlatWorldTemplate : public WorldTemplate {
public:
	FlatWorldTemplate() {
		// Try to load Python config; fall back to struct defaults silently
		loadWorldConfig("artifacts/worlds/base/flat.py", m_py);
		m_py.hasVillage = false;
		m_py.terrainType = "flat";
	}

	std::string name()        const override { return "Flat World"; }
	std::string description() const override { return "Flat grass plane, ideal for building"; }

	float surfaceHeight(int, float, float) override { return m_py.surfaceY; }

	glm::vec3 preferredSpawn(int /*seed*/) const override {
		return {m_py.spawnSearchX, m_py.surfaceY + 1.0f, m_py.spawnSearchZ};
	}

	glm::vec3 chestPosition(int /*seed*/, glm::vec3 spawnPos) const override {
		// Chest placed at a fixed offset from spawn so it never overlaps the player
		float sy = m_py.surfaceY + 1.0f;
		return {spawnPos.x + m_py.chestOffsetX, sy, spawnPos.z + m_py.chestOffsetZ};
	}

	glm::ivec2 villageCenter(int /*seed*/) const override {
		return {(int)m_py.spawnSearchX, (int)m_py.spawnSearchZ};
	}

	const WorldPyConfig& pyConfig() const override { return m_py; }

	void generate(Chunk& chunk, ChunkPos cpos, int /*seed*/,
	              const BlockRegistry& blocks) override {
		BlockId stone = blocks.getId(BlockType::Stone);
		BlockId dirt  = blocks.getId(BlockType::Dirt);
		BlockId grass = blocks.getId(BlockType::Grass);

		int oy = cpos.y * CHUNK_SIZE;
		int sy = (int)m_py.surfaceY;

		for (int ly = 0; ly < CHUNK_SIZE; ly++) {
			int wy = oy + ly;
			BlockId type = BLOCK_AIR;
			if      (wy < sy - m_py.dirtDepth) type = stone;
			else if (wy < sy)                  type = dirt;
			else if (wy == sy)                 type = grass;
			else                               continue;

			for (int lz = 0; lz < CHUNK_SIZE; lz++)
				for (int lx = 0; lx < CHUNK_SIZE; lx++)
					chunk.set(lx, ly, lz, type);
		}
	}

private:
	WorldPyConfig m_py;
};

// ============================================================
// Village World: natural terrain, trees, village with chest
// ============================================================
class VillageWorldTemplate : public WorldTemplate {
public:
	VillageWorldTemplate() {
		// Load parameters from Python artifact; fall back to struct defaults
		WorldPyConfig loaded;
		if (loadWorldConfig("artifacts/worlds/base/village.py", loaded))
			m_py = loaded;
		else {
			// Ensure mob list has the standard village mobs
			m_py.mobs = {
				{"base:villager", 3, 10.0f},
				{"base:pig",      4, 22.0f},
				{"base:chicken",  3, 18.0f},
				{"base:dog",      2, 14.0f},
				{"base:cat",      2, 12.0f},
			};
		}
		// Expose terrain params to noise layer
		m_tp.continentScale     = m_py.continentScale;
		m_tp.continentAmplitude = m_py.continentAmplitude;
		m_tp.hillScale          = m_py.hillScale;
		m_tp.hillAmplitude      = m_py.hillAmplitude;
		m_tp.detailScale        = m_py.detailScale;
		m_tp.detailAmplitude    = m_py.detailAmplitude;
		m_tp.microScale         = m_py.microScale;
		m_tp.microAmplitude     = m_py.microAmplitude;
	}

	std::string name()        const override { return "Village"; }
	std::string description() const override { return "Rolling hills, trees, and a village"; }

	void setConfig(const WorldGenConfig& cfg) { m_cfg = cfg; }
	const WorldGenConfig& config() const { return m_cfg; }
	const WorldPyConfig& pyConfig() const override { return m_py; }

	float surfaceHeight(int seed, float x, float z) override {
		return naturalTerrainHeight(seed, x, z, m_tp);
	}

	// ── Preferred spawn: near a flat area, far enough from village ──
	glm::vec3 preferredSpawn(int seed) const override {
		auto anchor = findAnchor(seed);
		float sy = naturalTerrainHeight(seed, anchor.x, anchor.y, m_tp);
		return {anchor.x, sy + 1.0f, anchor.y};
	}

	// ── Village center: anchor + config offset ───────────────────
	glm::ivec2 villageCenter(int seed) const override {
		auto anchor = findAnchor(seed);
		return {(int)anchor.x + (int)m_py.villageOffsetX,
		        (int)anchor.y + (int)m_py.villageOffsetZ};
	}

	// ── Chest: inside the center house (house[0]) ────────────────
	glm::vec3 chestPosition(int seed, glm::vec3 /*spawnPos*/) const override {
		auto vc = villageCenter(seed);
		if (m_py.houses.empty()) {
			// No houses — fall back to a simple offset from village center
			float vy = naturalTerrainHeight(seed, (float)vc.x, (float)vc.y, m_tp);
			return {(float)vc.x + 0.5f, vy + 2.0f, (float)vc.y + 0.5f};
		}
		const auto& h0 = m_py.houses[0];
		// Interior center of house 0 (avoid the wall)
		float hx = (float)(vc.x + h0.cx) + h0.w * 0.5f;
		float hz = (float)(vc.y + h0.cz) + h0.d * 0.5f;
		float hy = naturalTerrainHeight(seed, hx, hz, m_tp) + 2.0f; // floor + 1
		return {hx, hy, hz};
	}

	// ── Chunk generation ─────────────────────────────────────────
	void generate(Chunk& chunk, ChunkPos cpos, int seed,
	              const BlockRegistry& blocks) override {
		BlockId bStone  = blocks.getId(BlockType::Stone);
		BlockId bDirt   = blocks.getId(BlockType::Dirt);
		BlockId bGrass  = blocks.getId(BlockType::Grass);
		BlockId bSand   = blocks.getId(BlockType::Sand);
		BlockId bWater  = blocks.getId(BlockType::Water);
		BlockId bSnow   = blocks.getId(BlockType::Snow);
		BlockId bWood   = blocks.getId(BlockType::Wood);
		BlockId bLeaves = blocks.getId(BlockType::Leaves);

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

		int   wl       = (int)m_py.waterLevel;
		float snowLine = m_py.snowThreshold;
		int   dd       = m_py.dirtDepth;

		// ── Terrain ──────────────────────────────────────────────
		for (int lz = 0; lz < CHUNK_SIZE; lz++) {
			for (int lx = 0; lx < CHUNK_SIZE; lx++) {
				int wx = ox + lx;
				int wz = oz + lz;
				int surfaceY = (int)std::round(naturalTerrainHeight(seed, (float)wx, (float)wz, m_tp));

				for (int ly = 0; ly < CHUNK_SIZE; ly++) {
					int wy = oy + ly;
					if (wy > surfaceY) {
						if (wy <= wl) chunk.set(lx, ly, lz, bWater);
						// else air (default)
					} else if (wy == surfaceY) {
						if      (surfaceY <= wl)             chunk.set(lx, ly, lz, bSand);
						else if ((float)surfaceY >= snowLine) chunk.set(lx, ly, lz, bSnow);
						else                                  chunk.set(lx, ly, lz, bGrass);
					} else if (wy > surfaceY - dd) {
						chunk.set(lx, ly, lz, (surfaceY <= wl + 1) ? bSand : bDirt);
					} else {
						chunk.set(lx, ly, lz, bStone);
					}
				}
			}
		}

		// ── Trees ────────────────────────────────────────────────
		auto anchor = findAnchor(seed);
		auto vc = villageCenter(seed);
		int apx = (int)std::round(anchor.x), apz = (int)std::round(anchor.y);

		for (int lz = 3; lz < CHUNK_SIZE - 3; lz++) {
			for (int lx = 3; lx < CHUNK_SIZE - 3; lx++) {
				int wx = ox + lx, wz = oz + lz;

				if (hashFloat(wx * 7 + seed, wz * 13) < (1.0f - m_py.treeDensity)) continue;

				int surfaceY = (int)std::round(naturalTerrainHeight(seed, (float)wx, (float)wz, m_tp));
				if (surfaceY <= wl || (float)surfaceY >= snowLine) continue;

				// No trees in village clearing
				{
					int dx = wx - vc.x, dz = wz - vc.y;
					if (dx*dx + dz*dz < m_py.clearingRadius * m_py.clearingRadius) continue;
				}
				// No trees near spawn portal
				{
					int dx = wx - apx, dz = wz - apz;
					if (dx*dx + dz*dz < 100) continue;  // 10-block radius around portal
				}

				int trunkBase = surfaceY + 1;
				int trunkHeight = m_py.trunkHeightMin +
					(int)(hashFloat(wx + 99, wz + 99) * (m_py.trunkHeightMax - m_py.trunkHeightMin));
				int trunkTop = trunkBase + trunkHeight - 1;

				for (int ty = trunkBase; ty <= trunkTop; ty++) {
					int ly = ty - oy;
					if (ly >= 0 && ly < CHUNK_SIZE) chunk.set(lx, ly, lz, bWood);
				}

				int leafR = m_py.leafRadius;
				for (int dy = -1; dy <= leafR; dy++) {
					int r = (dy == leafR) ? 1 : leafR;
					for (int dx = -r; dx <= r; dx++) {
						for (int ddz = -r; ddz <= r; ddz++) {
							if (dx == 0 && ddz == 0 && dy < leafR - 1) continue;
							if (dx*dx + ddz*ddz + dy*dy > leafR*leafR + 1) continue;
							int lxx = lx + dx, lyy = trunkTop + dy - oy, lzz = lz + ddz;
							if (lxx >= 0 && lxx < CHUNK_SIZE &&
							    lyy >= 0 && lyy < CHUNK_SIZE &&
							    lzz >= 0 && lzz < CHUNK_SIZE &&
							    chunk.get(lxx, lyy, lzz) == BLOCK_AIR)
								chunk.set(lxx, lyy, lzz, bLeaves);
						}
					}
				}
			}
		}

		// ── Village structures ────────────────────────────────────
		generateVillage(chunk, cpos, seed, wallB, roofB, floorB, pathB, vc, blocks);

		// ── Spawn portal ──────────────────────────────────────────
		generatePortal(chunk, cpos, seed, wallB, bStone, anchor);
	}

private:
	WorldPyConfig  m_py;
	WorldGenConfig m_cfg;
	TerrainParams  m_tp;

	// Per-seed spawn anchor cache (thread-safe for reading after construction)
	mutable std::unordered_map<int, glm::vec2> m_anchorCache;

	// Find flat-terrain anchor for this seed (spiral search from config origin)
	glm::vec2 findAnchor(int seed) const {
		auto it = m_anchorCache.find(seed);
		if (it != m_anchorCache.end()) return it->second;

		float sx = m_py.spawnSearchX, sz = m_py.spawnSearchZ;
		// Spiral outward until we find terrain in the right height band
		for (int t = 0; t < 120; t++) {
			float h = naturalTerrainHeight(seed, sx, sz, m_tp);
			if (h >= m_py.spawnMinH && h <= m_py.spawnMaxH) break;
			float angle = (float)t * 2.399963f; // golden-angle spiral
			float r     = (float)t * 4.0f;
			sx = m_py.spawnSearchX + std::cos(angle) * r;
			sz = m_py.spawnSearchZ + std::sin(angle) * r;
		}
		m_anchorCache[seed] = {sx, sz};
		return {sx, sz};
	}

	// ── Spawn portal (DST-style gateway) ──────────────────────────
	// A 9-wide × 13-tall cobblestone arch centered on the spawn anchor.
	// Opening: 5 wide (centre ± 2) × 10 tall — player walks out the +Z face.
	// Decorations: stone accent rings on pillars, stepped arch top,
	// back decorative window, and battlement crenellations at the peak.
	void generatePortal(Chunk& chunk, ChunkPos cpos, int seed,
	                    BlockId wallB, BlockId stoneB, glm::vec2 anchor) {
		int ox = cpos.x * CHUNK_SIZE;
		int oy = cpos.y * CHUNK_SIZE;
		int oz = cpos.z * CHUNK_SIZE;

		int px = (int)std::round(anchor.x);
		int pz = (int)std::round(anchor.y);
		int groundY = (int)std::round(
			naturalTerrainHeight(seed, (float)px, (float)pz, m_tp));

		// Portal spans: X = px-4..px+4 (9 wide)
		//               Y = groundY..groundY+12 (13 tall)
		//               Z = pz-1..pz+1 (3 deep)
		// Front arch face at Z = pz+1 (player faces +Z → looks through opening)
		// Opening: X = px-2..px+2 (5 wide), Y = groundY+1..groundY+10 (10 tall)

		auto set = [&](int wx, int wy, int wz, BlockId bid) {
			int lx = wx - ox, ly = wy - oy, lz = wz - oz;
			if (lx >= 0 && lx < CHUNK_SIZE &&
			    ly >= 0 && ly < CHUNK_SIZE &&
			    lz >= 0 && lz < CHUNK_SIZE)
				chunk.set(lx, ly, lz, bid);
		};

		// Clear portal volume (remove any terrain that crept in)
		for (int dy = 0; dy <= 14; dy++)
			for (int dx = -5; dx <= 5; dx++)
				for (int dz = -2; dz <= 2; dz++)
					set(px + dx, groundY + dy, pz + dz, BLOCK_AIR);

		// ── Base layer (dy=0): full 9-wide foundation ──
		for (int dx = -4; dx <= 4; dx++)
			for (int dz = -1; dz <= 1; dz++)
				set(px + dx, groundY, pz + dz, wallB);

		// ── Main side pillars (dx=±4): full height, stone accent rings every 4 rows ──
		for (int sign : {-1, 1}) {
			for (int dz = -1; dz <= 1; dz++) {
				for (int dy = 1; dy <= 12; dy++) {
					BlockId b = (dy % 4 == 0) ? stoneB : wallB;
					set(px + sign * 4, groundY + dy, pz + dz, b);
				}
			}
		}

		// ── Secondary pillars (dx=±3): height 1-12, all depths except open front ──
		for (int sign : {-1, 1}) {
			for (int dz = -1; dz <= 1; dz++) {
				for (int dy = 1; dy <= 12; dy++) {
					if (dz == 1 && dy <= 10) continue;  // front face: opening region
					set(px + sign * 3, groundY + dy, pz + dz, wallB);
				}
			}
		}

		// ── Back wall (dz=-1): fill interior with decorative window ──
		for (int dx = -2; dx <= 2; dx++) {
			for (int dy = 1; dy <= 12; dy++) {
				bool isWindow = (std::abs(dx) <= 1 && dy >= 3 && dy <= 7);
				if (!isWindow)
					set(px + dx, groundY + dy, pz - 1, wallB);
			}
		}

		// ── Front arch top (dz=+1): stepped arch above opening ──
		// dy=11: 7-wide arch (dx=-3..3 come from secondary pillars + this row)
		for (int dx = -2; dx <= 2; dx++)
			set(px + dx, groundY + 11, pz + 1, wallB);
		// dy=12: narrowing keystone — stone centre accent
		for (int dx = -1; dx <= 1; dx++)
			set(px + dx, groundY + 12, pz + 1, (dx == 0) ? stoneB : wallB);

		// ── Interior (dz=0): clear between secondary pillars ──
		for (int dy = 1; dy <= 12; dy++)
			for (int dx = -2; dx <= 2; dx++)
				set(px + dx, groundY + dy, pz, BLOCK_AIR);

		// ── Battlements (dy=13): crenellations atop main pillars ──
		for (int sign : {-1, 1}) {
			set(px + sign * 4, groundY + 13, pz - 1, stoneB);
			set(px + sign * 4, groundY + 13, pz,     stoneB);
			set(px + sign * 4, groundY + 13, pz + 1, stoneB);
		}
		// Peak accent centred above keystone
		set(px, groundY + 13, pz + 1, stoneB);
	}

	void generateVillage(Chunk& chunk, ChunkPos cpos, int seed,
	                     BlockId wallB, BlockId roofB, BlockId floorB, BlockId pathB,
	                     glm::ivec2 vc, const BlockRegistry& blocks) {
		int ox = cpos.x * CHUNK_SIZE;
		int oy = cpos.y * CHUNK_SIZE;
		int oz = cpos.z * CHUNK_SIZE;

		int sh = m_py.storyHeight;
		int dh = m_py.doorHeight;
		int wr = m_py.windowRow;

		for (auto& h : m_py.houses) {
			int hcx = vc.x + h.cx;
			int hcz = vc.y + h.cz;
			int floorY = (int)std::round(naturalTerrainHeight(seed, (float)hcx + h.w * 0.5f,
			                                                         (float)hcz + h.d * 0.5f, m_tp)) + 1;
			int totalH = sh * h.stories;

			// Per-house material overrides
			BlockId hWallB = (!h.wallBlock.empty()) ? blocks.getId(h.wallBlock) : wallB;
			BlockId hRoofB = (!h.roofBlock.empty()) ? blocks.getId(h.roofBlock) : roofB;
			if (hWallB == BLOCK_AIR) hWallB = wallB;
			if (hRoofB == BLOCK_AIR) hRoofB = roofB;

			// Foundation: overwrite ground-level blocks with floor material
			{
				int fy = floorY - 1 - oy;
				if (fy >= 0 && fy < CHUNK_SIZE) {
					for (int dx = 0; dx < h.w; dx++) {
						for (int dz = 0; dz < h.d; dz++) {
							int lx = hcx + dx - ox;
							int lz = hcz + dz - oz;
							if (lx >= 0 && lx < CHUNK_SIZE && lz >= 0 && lz < CHUNK_SIZE)
								chunk.set(lx, fy, lz, floorB);
						}
					}
				}
			}

			for (int dy = 0; dy < totalH; dy++) {
				int ly = floorY + dy - oy;
				if (ly < 0 || ly >= CHUNK_SIZE) continue;

				for (int dx = 0; dx < h.w; dx++) {
					for (int dz = 0; dz < h.d; dz++) {
						int lx = hcx + dx - ox;
						int lz = hcz + dz - oz;
						if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;

						bool wall   = (dx == 0 || dx == h.w-1 || dz == 0 || dz == h.d-1);
						bool door   = ((dx == h.w/2 || dx == h.w/2 - 1) && dz == 0 && dy < dh);
						bool window = wall && dy == wr && (
							((dz == 0 || dz == h.d-1) && (dx == 1 || dx == h.w-2)) ||
							((dx == 0 || dx == h.w-1) && (dz == 1 || dz == h.d-2))
						);

						// Stair steps for multi-story: ascending along the left interior
						// wall (dx=1). For story s, step i sits at dz=2+i, dy=s*sh+i+1.
						bool stairStep = false;
						if (h.stories >= 2) {
							for (int s = 0; s < h.stories - 1 && !stairStep; s++) {
								for (int i = 0; i < sh - 1 && !stairStep; i++) {
									if (dx == 1 && dz == 2 + i && dy == s * sh + i + 1)
										stairStep = true;
								}
							}
						}

						// Intermediate floor between stories (acts as ceiling of story s
						// and walkable floor of story s+1). All cells at dy = s*sh - 1.
						bool intermFloor = false;
						if (h.stories >= 2 && !stairStep) {
							for (int s = 1; s < h.stories; s++) {
								if (dy == s * sh - 1) { intermFloor = true; break; }
							}
						}

						if (door || window)
							chunk.set(lx, ly, lz, BLOCK_AIR);
						else if (stairStep || intermFloor)
							chunk.set(lx, ly, lz, floorB);
						else if (dy == totalH - 1)
							chunk.set(lx, ly, lz, hRoofB);   // top ceiling
						else if (wall)
							chunk.set(lx, ly, lz, hWallB);
						else
							chunk.set(lx, ly, lz, BLOCK_AIR);  // interior (foundation is floor)
					}
				}
			}

			// Peaked gable roof (overhangs by 1 block front/back)
			int roofLayers = (h.w + 2) / 2;
			for (int ry = 0; ry < roofLayers; ry++) {
				int ly = floorY + totalH + ry - oy;
				if (ly < 0 || ly >= CHUNK_SIZE) continue;

				for (int dz = -1; dz <= h.d; dz++) {
					for (int dx = ry; dx < h.w - ry; dx++) {
						int lx = hcx + dx - ox;
						int lz = hcz + dz - oz;
						if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;

						bool gableEnd = (dz == 0 || dz == h.d - 1);
						bool roofEdge = (dx == ry || dx == h.w - ry - 1 || ry == roofLayers - 1);
						if (gableEnd || roofEdge) chunk.set(lx, ly, lz, hRoofB);
					}
				}
			}
		}

		// Cobblestone path between village center and south edge
		for (int dz = -22; dz <= 26; dz++) {
			int wx = vc.x + 2, wz = vc.y + dz;
			int lx = wx - ox, lz = wz - oz;
			if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;

			int surfY = (int)std::round(naturalTerrainHeight(seed, (float)wx, (float)wz, m_tp));
			int ly = surfY - oy;
			if (ly >= 0 && ly < CHUNK_SIZE) {
				chunk.set(lx, ly, lz, pathB);
				if (lx + 1 < CHUNK_SIZE) chunk.set(lx + 1, ly, lz, pathB);
			}
		}
	}
};

} // namespace agentworld
