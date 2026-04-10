#pragma once

#include "shared/types.h"
#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/constants.h"
#include "server/noise.h"
#include "server/world_gen_config.h"
#include "server/python_bridge.h"
#include "server/world_accessibility.h"
#include <string>
#include <cmath>
#include <unordered_map>
#include <cstdio>

namespace modcraft {

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
	virtual float surfaceHeight(int seed, float x, float z) const = 0;

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

	// Bed positions (world XYZ, above the bed block) — one per house that has a bed.
	// Returns empty if this template has no village / no beds.
	virtual std::vector<glm::vec3> bedPositions(int seed) const { return {}; }

	// Center of the first barn in world XZ (for animal spawn placement).
	// Returns {-1, -1} if this template has no barn.
	virtual glm::ivec2 barnCenter(int seed) const { return {-1, -1}; }

	// Chest positions (world XYZ) for all non-barn houses, in house order.
	// Villager[i] is assigned chest[i] so they know where to deposit items.
	// Returns empty if this template has no village.
	virtual std::vector<glm::vec3> houseChestPositions(int seed) const { return {}; }
};

// ============================================================
// Configurable World Template
// ============================================================
// Reads any world Python config and conditionally generates:
//   - Flat or natural (Perlin noise) terrain
//   - Village structures (if config has a village section)
//   - Spawn portal (always)
//   - Trees (natural terrain only, if density > 0)
//
// FlatWorldTemplate and VillageWorldTemplate are thin aliases.
// ============================================================
class ConfigurableWorldTemplate : public WorldTemplate {
public:
	explicit ConfigurableWorldTemplate(const std::string& pyPath) {
		loadWorldConfig(pyPath, m_py);
		// Set up noise params for natural terrain
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
		// Player spawns on the portal platform (groundY + kPlatH) at the exact center
		// of the arch interior — world XZ origin when spawnSearch is (0,0).
		// The spawn block is at the same X,Z on the platform floor.
		float gY, sx, sz;
		if (m_py.terrainType == "flat") {
			gY = m_py.surfaceY;
			sx = m_py.spawnSearchX;
			sz = m_py.spawnSearchZ;
		} else {
			auto anchor = findAnchor(seed);
			gY = naturalTerrainHeight(seed, anchor.x, anchor.y, m_tp);
			sx = anchor.x;
			sz = anchor.y;
		}
		// +0.5 in X and Z to center the player within the block's [bx, bx+1) footprint.
		// -1.0 in Z: spawn block is at dz=-1 (center of the 9-block floor: backDZ=-5..frontDZ=+3).
		return {(float)(int)std::round(sx) + 0.5f,
		        gY + kPlatH,
		        (float)(int)std::round(sz) - 0.5f};
	}

	glm::ivec2 villageCenter(int seed) const override {
		if (m_py.terrainType == "flat") {
			return {(int)m_py.spawnSearchX + (int)m_py.villageOffsetX,
			        (int)m_py.spawnSearchZ + (int)m_py.villageOffsetZ};
		}
		auto anchor = findAnchor(seed);
		return {(int)anchor.x + (int)m_py.villageOffsetX,
		        (int)anchor.y + (int)m_py.villageOffsetZ};
	}

	glm::vec3 chestPosition(int seed, glm::vec3 spawnPos) const override {
		if (m_py.hasVillage && !m_py.houses.empty()) {
			auto vc = villageCenter(seed);
			const auto& h0 = m_py.houses[0];
			float hx = (float)(vc.x + h0.cx) + (float)(h0.w - 3);
			float hz = (float)(vc.y + h0.cz) + 1.0f;
			float hy = surfaceHeight(seed, hx, hz) + 1.0f;
			return {hx, hy, hz};
		}
		// Fallback: offset from spawn (flat world without village)
		float sy = surfaceHeight(seed, spawnPos.x + m_py.chestOffsetX,
		                         spawnPos.z + m_py.chestOffsetZ) + 1.0f;
		return {spawnPos.x + m_py.chestOffsetX, sy, spawnPos.z + m_py.chestOffsetZ};
	}

	glm::ivec2 barnCenter(int seed) const override {
		if (!m_py.hasVillage) return {-1, -1};
		auto vc = villageCenter(seed);
		for (const auto& h : m_py.houses) {
			if (h.type == "barn")
				return {vc.x + h.cx + h.w / 2, vc.y + h.cz + h.d / 2};
		}
		return {-1, -1};
	}

	// Scan every (x,z) in the structure footprint and return {minGroundY, maxGroundY}.
	// Used to place the floor at maxGroundY+1 so the building always sits on the
	// highest terrain point within its footprint, never buried in a hillside.
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

	// Compute the correct floor Y for a structure footprint.
	// floorY = maxGroundY + 1 so the structure always sits above the hillside peak.
	int structureFloorY(int seed, int hcx, int hcz, int w, int d) const {
		return footprintHeightRange(seed, hcx, hcz, w, d).second + 1;
	}

	// Chest position for each non-barn house, in house order — matches bedPositions() order.
	// Uses the footprint-max floor height so chests land on the actual floor.
	std::vector<glm::vec3> houseChestPositions(int seed) const override {
		if (!m_py.hasVillage || m_py.houses.empty()) return {};
		auto vc = villageCenter(seed);
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

	// Villager spawn positions: one per non-barn house, at the interior bed location.
	// Matches generateFurniture(): bed is at (hcx+2, floorY, hcz+d-2).
	std::vector<glm::vec3> bedPositions(int seed) const override {
		if (!m_py.hasVillage || m_py.houses.empty()) return {};
		auto vc = villageCenter(seed);
		std::vector<glm::vec3> beds;
		for (const auto& h : m_py.houses) {
			if (h.type == "barn") continue;
			int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
			int floorY = structureFloorY(seed, hcx, hcz, h.w, h.d);
			// Interior bed position (same as generateFurniture bed block + 1 for standing)
			float bx = (float)(hcx + 2) + 0.5f;
			float bz = (float)(hcz + h.d - 2) + 0.5f;
			float by = (float)floorY + 1.0f;  // standing height above floor
			beds.push_back({bx, by, bz});
		}
		return beds;
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
		BlockId bLog    = blocks.getId(BlockType::Log);
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

		bool isFlat = (m_py.terrainType == "flat");

		// ── Terrain ──────────────────────────────────────────────
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
				for (int lz = 0; lz < CHUNK_SIZE; lz++)
					for (int lx = 0; lx < CHUNK_SIZE; lx++)
						chunk.set(lx, ly, lz, type);
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
							else                            chunk.set(lx, ly, lz, bGrass);
						} else if (wy > sy - dd) {
							chunk.set(lx, ly, lz, (sy <= wl + 1) ? bSand : bDirt);
						} else {
							chunk.set(lx, ly, lz, bStone);
						}
					}
				}
			}
		}

		// ── Trees (natural terrain only) ─────────────────────────
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
					int leafR = m_py.leafRadius;
					for (int dy = -1; dy <= leafR; dy++) {
						int r = (dy == leafR) ? 1 : leafR;
						for (int dx = -r; dx <= r; dx++)
							for (int ddz = -r; ddz <= r; ddz++) {
								if (dx==0 && ddz==0 && dy < leafR-1) continue;
								if (dx*dx+ddz*ddz+dy*dy > leafR*leafR+1) continue;
								int lxx=lx+dx, lyy=trunkTop+dy-oy, lzz=lz+ddz;
								if (lxx>=0&&lxx<CHUNK_SIZE&&lyy>=0&&lyy<CHUNK_SIZE&&
								    lzz>=0&&lzz<CHUNK_SIZE&&chunk.get(lxx,lyy,lzz)==BLOCK_AIR)
									chunk.set(lxx, lyy, lzz, bLeaves);
							}
					}
				}
			}
		}

		// ── Village structures ────────────────────────────────────
		if (m_py.hasVillage)
			generateVillage(chunk, cpos, seed, wallB, roofB, floorB, pathB, vc, blocks);

		// ── Spawn portal ──────────────────────────────────────────
		BlockId stairB   = blocks.getId(BlockType::Stair);
		BlockId planksB  = blocks.getId(BlockType::Planks);
		BlockId arcaneB  = blocks.getId(BlockType::ArcaneStone);
		BlockId portalB  = blocks.getId(BlockType::Portal);
		BlockId spawnPtB = blocks.getId(BlockType::SpawnPoint);
		generatePortal(chunk, cpos, seed, wallB, bStone, stairB, planksB, arcaneB, portalB, spawnPtB, anchor);
	}

	// Portal platform height (blocks above groundY). Used in preferredSpawn + generatePortal.
	static constexpr int kPlatH = 5;

private:
	WorldPyConfig m_py;
	TerrainParams m_tp;

	// Ground height at world (x,z) — works for both flat and natural terrain
	float groundHeight(int seed, float x, float z) const {
		if (m_py.terrainType == "flat") return m_py.surfaceY;
		return naturalTerrainHeight(seed, x, z, m_tp);
	}

	// Per-seed spawn anchor cache (thread-safe for reading after construction)
	mutable std::unordered_map<int, glm::vec2> m_anchorCache;

	// Find spawn anchor for this seed.
	// Flat: fixed position from config. Natural: spiral search for flat terrain.
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

	// ── Spawn portal (grand elevated stone temple arch) ──────────
	// 17-wide × 25-tall stone arch on a 5-block raised platform.
	// 7-wide descending staircase exits the +Z face to ground level.
	// Player spawns on the platform (groundY+5) facing +Z (toward stairs and village).
	//
	// Z layout (offsets from anchor pz):
	//   dz=-5..-4 : back wall (2 thick, with windows)
	//   dz=-3..+3 : interior chamber (9-block floor, center at dz=-1 = spawn block)
	//   dz=+3     : front arch opening
	//   dz=+4..+8 : descending staircase (5 steps, 0.5–1.0 drop each)
	//
	// X layout (offsets from anchor px):
	//   dx=±3     : arch passage (7-wide opening)
	//   dx=±4     : arch jambs
	//   dx=±5..±6 : inner pillar pair
	//   dx=±7..±8 : outer tower pair
	//
	void generatePortal(Chunk& chunk, ChunkPos cpos, int seed,
	                    BlockId wallB, BlockId stoneB, BlockId stairB,
	                    BlockId planksB, BlockId arcaneB, BlockId portalB,
	                    BlockId spawnPtB, glm::vec2 anchor) {
		int ox = cpos.x * CHUNK_SIZE;
		int oy = cpos.y * CHUNK_SIZE;
		int oz = cpos.z * CHUNK_SIZE;

		int px = (int)std::round(anchor.x);
		int pz = (int)std::round(anchor.y);
		int groundY = (m_py.terrainType == "flat")
			? (int)m_py.surfaceY
			: (int)std::round(naturalTerrainHeight(seed, (float)px, (float)pz, m_tp));

		auto set = [&](int wx, int wy, int wz, BlockId bid, uint8_t p2 = 0) {
			int lx = wx - ox, ly = wy - oy, lz = wz - oz;
			if (lx >= 0 && lx < CHUNK_SIZE &&
			    ly >= 0 && ly < CHUNK_SIZE &&
			    lz >= 0 && lz < CHUNK_SIZE)
				chunk.set(lx, ly, lz, bid, p2);
		};

		// Stone accent ring every 4 blocks of height above groundY
		auto pickBlock = [&](int wy) -> BlockId {
			return (((wy - groundY) % 4) == 0) ? stoneB : wallB;
		};

		if (stoneB  == BLOCK_AIR) stoneB  = wallB;
		if (planksB == BLOCK_AIR) planksB = stoneB;
		if (stairB  == BLOCK_AIR) stairB  = wallB;
		if (arcaneB == BLOCK_AIR) arcaneB = stoneB;

		// Dimensions
		constexpr int platH   = kPlatH; // platform height above groundY
		constexpr int openH   = 13;  // arch opening height
		constexpr int crownH  = 2;   // arch crown thickness above opening
		constexpr int towerXH = 5;   // extra tower height above crown
		constexpr int openHW  = 3;   // opening half-width (7-wide passage)
		constexpr int jambHW  = 4;   // arch jamb column
		constexpr int pier2HW = 6;   // inner pillar outer edge
		constexpr int towerHW = 7;   // outer tower inner edge
		constexpr int towerOW = 8;   // outer tower outer edge
		constexpr int backDZ  = -5;  // back wall Z
		constexpr int frontDZ = +3;  // front arch face Z (9-deep floor: backDZ..frontDZ = ODD → center at dz=0)
		constexpr int numSteps = platH;

		const int platSurfY = groundY + platH;
		const int archTopY  = platSurfY + openH;
		const int crownTopY = archTopY  + crownH;
		const int towerTopY = crownTopY + towerXH;

		// ── 1. Wipe ──────────────────────────────────────────────
		for (int dy = 0; dy <= towerTopY - groundY + 3; dy++)
			for (int dx = -(towerOW + 1); dx <= towerOW + 1; dx++)
				for (int dz = backDZ - 1; dz <= frontDZ + numSteps + 3; dz++)
					set(px + dx, groundY + dy, pz + dz, BLOCK_AIR);

		// ── 2. Platform foundation ────────────────────────────────
		for (int dx = -towerOW; dx <= towerOW; dx++) {
			for (int dz = backDZ; dz <= frontDZ; dz++) {
				for (int dy = 0; dy < platH - 1; dy++)
					set(px + dx, groundY + dy, pz + dz, wallB);
				// Top surface: plank floor inside opening, stone elsewhere
				bool inner = (std::abs(dx) <= openHW);
				set(px + dx, groundY + platH - 1, pz + dz, inner ? planksB : stoneB);
			}
		}

		// ── 2b. Spawn point block — at floor center (dx=0, dz=-1) ────────────────
		// Floor depth backDZ..frontDZ = -5..+3 = 9 blocks (ODD) → center at dz=-1.
		// Player spawns centered above it at (px+0.5, groundY+platH, pz-0.5).
		if (spawnPtB != BLOCK_AIR)
			set(px, groundY + platH - 1, pz - 1, spawnPtB);

		// ── 3. Outer towers (dx=±7..±8, to towerTopY) — arcane ──
		for (int sign : {-1, 1})
			for (int tw = towerHW; tw <= towerOW; tw++)
				for (int dz = backDZ; dz <= frontDZ; dz++)
					for (int wy = platSurfY; wy <= towerTopY; wy++)
						set(px + sign * tw, wy, pz + dz, arcaneB);

		// ── 4. Inner pillars (dx=±5..±6, to crownTopY) — arcane ─
		for (int sign : {-1, 1})
			for (int pw = 5; pw <= pier2HW; pw++)
				for (int dz = backDZ; dz <= frontDZ; dz++)
					for (int wy = platSurfY; wy <= crownTopY; wy++)
						set(px + sign * pw, wy, pz + dz, arcaneB);

		// ── 5. Arch jambs (dx=±4, to archTopY) — arcane ─────────
		for (int sign : {-1, 1})
			for (int dz = backDZ; dz <= frontDZ; dz++)
				for (int wy = platSurfY; wy < archTopY; wy++)
					set(px + sign * jambHW, wy, pz + dz, arcaneB);

		// ── 6. Arch crown span (dx=-6..+6, archTopY..crownTopY) — arcane ─
		for (int dz = backDZ; dz <= frontDZ; dz++) {
			for (int wy = archTopY; wy < crownTopY; wy++)
				for (int dx = -pier2HW; dx <= pier2HW; dx++)
					set(px + dx, wy, pz + dz, arcaneB);
			// Keystone at crownTopY: narrower span dx=-4..+4
			for (int dx = -jambHW; dx <= jambHW; dx++)
				set(px + dx, crownTopY, pz + dz, arcaneB);
		}

		// ── 7. Back wall (dz=backDZ..backDZ+1, with windows) ────
		for (int dz = backDZ; dz <= backDZ + 1; dz++) {
			for (int dx = -openHW; dx <= openHW; dx++) {
				for (int wy = platSurfY; wy < archTopY; wy++) {
					int relY = wy - platSurfY;
					bool mainWin = (std::abs(dx) <= 2 && relY >= 3 && relY <= 8);
					bool topWin  = (dx == 0           && relY >= 10 && relY <= 12);
					if (!mainWin && !topWin)
						set(px + dx, wy, pz + dz, wallB);
				}
			}
			// Crown fill above opening on back face — arcane
			for (int dx = -(pier2HW - 1); dx <= pier2HW - 1; dx++)
				for (int wy = archTopY; wy <= crownTopY; wy++)
					set(px + dx, wy, pz + dz, arcaneB);
		}

		// ── 8. Interior clear (dz=backDZ+2..frontDZ) ─────────────
		for (int dx = -openHW; dx <= openHW; dx++)
			for (int dz = backDZ + 2; dz <= frontDZ; dz++)
				for (int wy = platSurfY; wy < archTopY; wy++)
					set(px + dx, wy, pz + dz, BLOCK_AIR);

		// ── 9. Tower battlements — alternating arcane/stone ─────
		for (int sign : {-1, 1}) {
			for (int tw = towerHW; tw <= towerOW; tw++)
				for (int dz = backDZ; dz <= frontDZ; dz++)
					if (((dz - backDZ) % 2) == 0)
						set(px + sign * tw, towerTopY + 1, pz + dz, arcaneB);
			// Corner turret posts (2 high) — arcane
			for (int h = 1; h <= 2; h++) {
				set(px + sign * towerOW, towerTopY + h, pz + backDZ,  arcaneB);
				set(px + sign * towerOW, towerTopY + h, pz + frontDZ, arcaneB);
			}
		}

		// ── 10. Descending staircase (dz=+4..+8) ────────────────────
		// Step i at (Y=groundY+platH-1-i, dz=frontDZ+1+i).
		// param2=2: stair rises in -Z → HIGH backing wall on -Z (uphill/platform side),
		// LOW tread on +Z (downhill side).  Player walks +Z to descend; the backing wall
		// is behind them and the tread extends forward, giving a clear downward view.
		for (int i = 0; i < numSteps; i++) {
			int stepY  = groundY + platH - 1 - i;
			int stepDZ = frontDZ + 1 + i;
			for (int dx = -openHW; dx <= openHW; dx++) {
				set(px + dx, stepY, pz + stepDZ, stairB, /*param2=*/2);
				for (int fy = groundY; fy < stepY; fy++)
					set(px + dx, fy, pz + stepDZ, wallB);
			}
		}

		// ── 11. Stair side walls (dx=±4, descending parapets) ────
		for (int sign : {-1, 1}) {
			for (int i = 0; i < numSteps; i++) {
				int stepY  = groundY + platH - 1 - i;
				int stepDZ = frontDZ + 1 + i;
				for (int wy = groundY; wy <= stepY + 1; wy++)
					set(px + sign * (openHW + 1), wy, pz + stepDZ, stoneB);
			}
		}

		// ── 12. Portal plane (dz=backDZ+1) ───────────────────────
		if (portalB != BLOCK_AIR) {
			for (int dx = -openHW; dx <= openHW; dx++)
				for (int wy = platSurfY; wy < archTopY; wy++)
					set(px + dx, wy, pz + backDZ + 1, portalB);
		}
	}

	// ── Convenience: common generation context ───────────────────────
	// Wraps a chunk+offsets so helpers can call set(worldX, worldY, worldZ, bid)
	// without repeating the bounds check everywhere.
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

	// ── Reusable passage subroutines ─────────────────────────────
	//
	// These are called by generateHouse (and can be called for towers, dungeons,
	// etc.) to carve navigable openings through solid structures.
	// Each subroutine self-validates using world_accessibility.h predicates and
	// prints a warning to stderr if the resulting geometry is insufficient.

	// Carve a doorway at (doorX..doorX+width-1, floorY..floorY+dh-1, doorZ).
	// Overwrites whatever is there: doorBlockRows of doorB at the bottom, air above.
	// Validates that the non-door clearance >= ceil(playerH).
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

	// Carve a stairway rising in +Z direction for (stories-1) flights of sh steps.
	// Each step: stairB at (stairX..stairX+stairW-1, floorY+s*sh+i, baseZ+2+i).
	// Opens the intermFloor (at floorY+s*sh+sh-1) for columns dx=stairX..stairX+openW-1
	// where a player of height playerH would clip the ceiling.
	// Validates clearance above every step.
	void carveStairway(const GenCtx& ctx, const BlockRegistry& blocks,
	                   int stairX, int floorY, int baseZ,
	                   int sh, int stories, int stairW, int openW,
	                   BlockId stairB, float playerH = 2.5f, float margin = 0.25f) const
	{
		auto getBlock = [&](int x, int y, int z){ return ctx.get(x,y,z); };

		for (int s = 0; s < stories-1; s++) {
			int intermY = floorY + s*sh + sh - 1;  // intermediate floor Y

			// Phase 1: open ceiling where player head clips
			for (int i = 0; i < sh; i++) {
				float feetY  = (float)(floorY + s*sh + i) + 0.5f;
				float headY  = feetY + playerH + margin;
				if (headY > (float)intermY) {
					for (int ddx = 0; ddx < openW; ddx++)
						ctx.set(stairX + ddx, intermY, baseZ+2+i, BLOCK_AIR);
				}
			}

			// Phase 2: place stair blocks (after clearing so stairB wins at intermY row)
			// param2=0: stair rises in +Z → HIGH backing wall on +Z (landing side), LOW tread
			// on -Z (approach side).  Player walks +Z to ascend; tread is at their feet.
			for (int i = 0; i < sh; i++) {
				for (int ddx = 0; ddx < stairW; ddx++)
					ctx.set(stairX+ddx, floorY+s*sh+i, baseZ+2+i, stairB, /*param2=*/0);
			}

			// Phase 3: validate
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

	// ── House walls, floors, stairs, and roof ─────────────────────
	//
	// Staircase: 2-wide (dx=1..2), sh steps per story.
	// Opening: 4-wide (dx=1..4) × full stairwell depth — guarantees ≥ 4 clear
	// blocks above the landing so the 2.5-tall player fits comfortably.
	// Place a solid stone foundation under a structure footprint.
	// Fills from floorY-1 down to minGroundY with stone, replacing any existing
	// terrain (dirt, sand, grass) so the foundation is always stone.
	// Also clears any terrain inside the footprint between minGroundY+1 and floorY-1
	// so there are no buried terrain columns inside the building.
	void placeFoundation(const GenCtx& ctx, int seed, BlockId stoneB,
	                     int hcx, int hcz, int w, int d,
	                     int floorY, int minGroundY) const {
		for (int dx = 0; dx < w; dx++) {
			for (int dz = 0; dz < d; dz++) {
				// Clear any terrain sticking up inside the building footprint.
				// Each column may be lower or higher than minGroundY locally.
				int localGY = (int)std::round(groundHeight(seed, (float)(hcx+dx), (float)(hcz+dz)));
				for (int y = localGY + 1; y < floorY; y++)
					ctx.set(hcx+dx, y, hcz+dz, BLOCK_AIR);
				// Fill stone from just below the floor down to the lowest corner.
				// Replaces dirt/sand/grass with solid stone so the foundation is clean.
				for (int y = floorY - 1; y >= minGroundY; y--)
					ctx.set(hcx+dx, y, hcz+dz, stoneB);
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
					// Window: 3 wide × 2 tall on each story, two groups per wall face.
					bool windowRow = false;
					for (int s = 0; s < h.stories && !windowRow; s++)
						if (dy == s*sh + wr || dy == s*sh + wr + 1) windowRow = true;
					bool window = wall && windowRow && (
						((dz==0||dz==h.d-1)&&((dx>=1&&dx<=3)||(dx>=h.w-4&&dx<=h.w-2)))||
						((dx==0||dx==h.w-1)&&((dz>=1&&dz<=3)||(dz>=h.d-4&&dz<=h.d-2))));

					// Intermediate floor = ceiling of story s / floor of story s+1.
					bool intermFloor = false;
					if (h.stories >= 2)
						for (int s = 1; s < h.stories; s++)
							if (dy == s*sh-1) { intermFloor = true; break; }

					BlockId bid;
					uint8_t p2 = 0;
					if (door) {
						bid = doorB;
						// Mirror hinge: right door column (dx==h.w/2) opens right (+X),
						// left column (dx==h.w/2-1) opens left (-X).
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

		// Post-pass 1: clear the intermediate floor tiles directly above the stair
		// footprint (2-wide, exact step positions). Floors are fully placed first;
		// only the exact 2×(sh-1) opening above the stairs is punched through.
		// Does not touch the roof or higher-story floors.
		if (h.stories >= 2) {
			for (int s = 0; s < h.stories - 1; s++) {
				int ceilDy = (s + 1) * sh - 1; // intermediate floor level (ceiling of story s)
				for (int i = 0; i < sh - 1; i++) { // last step (i=sh-1) is at this level, placed below
					for (int dx = 1; dx <= 2; dx++)
						ctx.set(hcx + dx, floorY + ceilDy, hcz + (2 + i), BLOCK_AIR);
				}
			}
		}

		// Post-pass 2: place stair blocks last so they are never overwritten by
		// walls, floors, or other blocks from earlier passes.
		// Stair step i of story s: (dx=1..2, dz=2+i, dy=s*sh+i), i=0..sh-1.
		if (h.stories >= 2) {
			for (int s = 0; s < h.stories - 1; s++) {
				for (int i = 0; i < sh; i++) {
					for (int dx = 1; dx <= 2; dx++)
						ctx.set(hcx + dx, floorY + s*sh + i, hcz + (2 + i), stairB, 0);
				}
			}
		}

		// Peaked gable roof (overhangs 1 block front/back)
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

	// ── Porch / patio in front of the door ───────────────────────
	// Levels a 5-wide × 3-deep platform at floorY-1 so the door is
	// never blocked by a terrain hill.  Fills below with wall material
	// if the porch is elevated above surrounding ground.
	void generatePorch(const GenCtx& ctx, int seed,
	                   BlockId pathB, BlockId wallB,
	                   const WorldPyConfig::HouseLayout& h, glm::ivec2 vc,
	                   int floorY) {
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		int dh = m_py.doorHeight;
		int doorMid = h.w / 2;  // x-offset of door centre from hcx

		for (int pDz = 1; pDz <= 3; pDz++) {
			int wz = hcz - pDz;
			for (int pDx = doorMid - 2; pDx <= doorMid + 2; pDx++) {
				int wx = hcx + pDx;

				// Porch floor
				ctx.set(wx, floorY-1, wz, pathB);

				// Fill support pillars if terrain is concave below porch
				for (int sy = floorY-2; sy >= floorY-6; sy--) {
					if (ctx.get(wx, sy, wz) != BLOCK_AIR) break;
					ctx.set(wx, sy, wz, wallB);
				}

				// Clear terrain hills above porch up to door height
				for (int cy = floorY; cy < floorY+dh; cy++)
					ctx.set(wx, cy, wz, BLOCK_AIR);
			}
		}
	}

	// ── Interior furniture ────────────────────────────────────────
	// Beds (2-block foot+head), table (2 wood), chairs, chest.
	// All houses get a chest via houseChestPositions() placed by server.h::init().
	// House[0] is skipped here since server.h places it (avoids double-set for main house).
	void generateFurniture(const GenCtx& ctx, int seed,
	                       BlockId woodB, BlockId planksB, BlockId bedB, BlockId chestB,
	                       const WorldPyConfig::HouseLayout& h, glm::ivec2 vc,
	                       bool isMainHouse, int floorY) {
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		// Only furnish story 0 interior (never touch walls or stair column dx=1)

		// Bed: back-left corner, two blocks (foot at dz=d-2, head at dz=d-3)
		{
			int bx = hcx + 2, bz1 = hcz + h.d - 2, bz2 = hcz + h.d - 3;
			ctx.set(bx, floorY, bz1, bedB);
			ctx.set(bx, floorY, bz2, bedB);
		}

		// Chest: front-right interior corner (skip for main house, server.h owns that)
		if (!isMainHouse && chestB != BLOCK_AIR) {
			ctx.set(hcx + h.w - 3, floorY, hcz + 1, chestB);
		}

		// Table: 2 plank blocks (or wood), right-of-centre
		{
			int tx = hcx + h.w/2 + 1;
			int tz = hcz + h.d/2;
			ctx.set(tx,   floorY, tz,   planksB != BLOCK_AIR ? planksB : woodB);
			ctx.set(tx+1, floorY, tz,   planksB != BLOCK_AIR ? planksB : woodB);
			// Chairs on either side of table
			ctx.set(tx,   floorY, tz-1, woodB);
			ctx.set(tx+1, floorY, tz+1, woodB);
		}
	}

	// ── Village paths ─────────────────────────────────────────────
	void generatePaths(const GenCtx& ctx, int seed,
	                   BlockId pathB, glm::ivec2 vc) {
		for (int dz = -22; dz <= 26; dz++) {
			int wx = vc.x + 2, wz = vc.y + dz;
			int surfY = (int)std::round(groundHeight(seed, (float)wx, (float)wz));
			ctx.set(wx,   surfY, wz, pathB);
			ctx.set(wx+1, surfY, wz, pathB);
		}
	}

	// ── Barn (open-sided, pillars + massive peaked roof) ─────────
	// No walls, no door. Corner pillars + one mid-pillar per long side.
	// Large peaked gable roof — classic agricultural building.
	void generateBarn(const GenCtx& ctx, int seed,
	                  BlockId woodB, BlockId planksB, BlockId roofB, BlockId stoneB,
	                  const WorldPyConfig::HouseLayout& h, glm::ivec2 vc,
	                  int floorY) {
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		int barnH = 9;  // pillar/wall height before roof starts
		BlockId col = (woodB != BLOCK_AIR) ? woodB : planksB;

		// 1. Plank floor on top of stone foundation (foundation placed by caller)
		for (int dx = 0; dx < h.w; dx++)
			for (int dz = 0; dz < h.d; dz++)
				ctx.set(hcx+dx, floorY-1, hcz+dz, planksB != BLOCK_AIR ? planksB : col);

		// 2. Corner pillars (full height)
		for (int dy = 0; dy < barnH; dy++) {
			ctx.set(hcx,       floorY+dy, hcz,       col);
			ctx.set(hcx+h.w-1, floorY+dy, hcz,       col);
			ctx.set(hcx,       floorY+dy, hcz+h.d-1, col);
			ctx.set(hcx+h.w-1, floorY+dy, hcz+h.d-1, col);
		}

		// 3. Intermediate pillars every 4 blocks along each side
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

		// 4. Top beam: horizontal planks connecting pillar tops on front/back
		for (int dx = 0; dx < h.w; dx++) {
			ctx.set(hcx+dx, floorY+barnH-1, hcz,       col);
			ctx.set(hcx+dx, floorY+barnH-1, hcz+h.d-1, col);
		}
		// Side beams (along depth)
		for (int dz = 0; dz < h.d; dz++) {
			ctx.set(hcx,       floorY+barnH-1, hcz+dz, col);
			ctx.set(hcx+h.w-1, floorY+barnH-1, hcz+dz, col);
		}

		// 5. Peaked gable roof — ridge runs along the width (Z-axis)
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

	// ── Village orchestrator ──────────────────────────────────────
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
		BlockId doorB   = blocks.getId(BlockType::Door);
		if (stairB == BLOCK_AIR) stairB = floorB;
		if (glassB == BLOCK_AIR) glassB = BLOCK_AIR;  // windows become open holes
		if (doorB  == BLOCK_AIR) doorB  = BLOCK_AIR;  // doors become open holes

		BlockId stoneB = blocks.getId(BlockType::Stone);
		if (stoneB == BLOCK_AIR) stoneB = blocks.getId(BlockType::Cobblestone);

		for (int hi = 0; hi < (int)m_py.houses.size(); hi++) {
			const auto& h = m_py.houses[hi];
			int hcx = vc.x + h.cx, hcz = vc.y + h.cz;

			// Compute floor height from footprint max so building sits on the hill,
			// never buried. Foundation fills from floorY-1 down to minGroundY with stone.
			auto [minGY, maxGY] = footprintHeightRange(seed, hcx, hcz, h.w, h.d);
			int floorY = maxGY + 1;
			placeFoundation(ctx, seed, stoneB, hcx, hcz, h.w, h.d, floorY, minGY);

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

		// ── Farm plot (wheat + farmland near village) ────────────
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
			// Water channel down the middle
			BlockId waterB = blocks.getId(BlockType::Water);
			if (waterB != BLOCK_AIR) {
				for (int dz = 0; dz < 6; dz++) {
					ctx.set(fx+3, fy-1, fz+dz, waterB);
				}
			}
		}

		// ── Animal pen (fence enclosure near village) ────────────
		BlockId fenceB = blocks.getId(BlockType::Fence);
		if (fenceB != BLOCK_AIR) {
			int px = vc.x + 16, pzz = vc.y - 18;
			auto [penMinGY, penMaxGY] = footprintHeightRange(seed, px, pzz, 10, 8);
			int py = penMaxGY + 1;
			placeFoundation(ctx, seed, stoneB, px, pzz, 10, 8, py, penMinGY);
			// Fence perimeter 10x8
			for (int dx = 0; dx < 10; dx++) {
				ctx.set(px+dx, py, pzz,   fenceB);
				ctx.set(px+dx, py, pzz+7, fenceB);
			}
			for (int dz = 1; dz < 7; dz++) {
				ctx.set(px,   py, pzz+dz, fenceB);
				ctx.set(px+9, py, pzz+dz, fenceB);
			}
			// Gate opening (2 wide)
			ctx.set(px+4, py, pzz, BLOCK_AIR);
			ctx.set(px+5, py, pzz, BLOCK_AIR);
		}

		// ── Village center monument — magical trident tower ─────
		// Tall arcane spire with glass windows, portal glow rings,
		// and a three-pronged trident crown.  Visible from spawn.
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

			// ─ Stepped base pyramid (3 tiers) ─
			for (int tier = 0; tier < 3; tier++) {
				int r = 3 - tier;   // radii: 3, 2, 1
				BlockId tb = (tier == 0) ? stoneB : arcaneB;
				for (int dx = -r; dx <= r; dx++)
					for (int dz = -r; dz <= r; dz++)
						ctx.set(mx+dx, my+tier, mz+dz, tb);
			}

			// ─ Tower body: 5x5 walls, 18 blocks tall, hollow 3x3 ─
			constexpr int towerH = 18;
			int bodyBase = my + 3;  // sits on top of pyramid
			for (int dy = 0; dy < towerH; dy++) {
				int y = bodyBase + dy;
				for (int dx = -2; dx <= 2; dx++)
					for (int dz = -2; dz <= 2; dz++) {
						bool edge = (std::abs(dx) == 2 || std::abs(dz) == 2);
						bool corner = (std::abs(dx) == 2 && std::abs(dz) == 2);
						if (corner) {
							// Corner columns: alternate arcane/stone every 3
							ctx.set(mx+dx, y, mz+dz, (dy % 3 == 0) ? stoneB : arcaneB);
						} else if (edge) {
							ctx.set(mx+dx, y, mz+dz, arcaneB);
						} else {
							ctx.set(mx+dx, y, mz+dz, BLOCK_AIR);
						}
					}
			}

			// ─ Portal glow rings at base, 1/3, 2/3 height ─
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

			// ─ Glass windows on each face (centered, at 1/4 and 3/4 height) ─
			for (int winY : {4, 9, 14}) {
				int y = bodyBase + winY;
				// ±Z faces: center column
				ctx.set(mx, y,   mz-2, glassB);
				ctx.set(mx, y+1, mz-2, glassB);
				ctx.set(mx, y,   mz+2, glassB);
				ctx.set(mx, y+1, mz+2, glassB);
				// ±X faces: center column
				ctx.set(mx-2, y,   mz, glassB);
				ctx.set(mx-2, y+1, mz, glassB);
				ctx.set(mx+2, y,   mz, glassB);
				ctx.set(mx+2, y+1, mz, glassB);
			}

			// ─ Door on -Z face ─
			for (int dy = 0; dy <= 2; dy++)
				ctx.set(mx, bodyBase+dy, mz-2, BLOCK_AIR);

			// ─ Observation deck (7x7 overhang) ─
			int deckY = bodyBase + towerH;
			for (int dx = -3; dx <= 3; dx++)
				for (int dz = -3; dz <= 3; dz++)
					ctx.set(mx+dx, deckY, mz+dz, stoneB);
			// Portal inlay on deck edge
			for (int dx = -2; dx <= 2; dx++) {
				ctx.set(mx+dx, deckY, mz-3, portalB);
				ctx.set(mx+dx, deckY, mz+3, portalB);
			}
			for (int dz = -2; dz <= 2; dz++) {
				ctx.set(mx-3, deckY, mz+dz, portalB);
				ctx.set(mx+3, deckY, mz+dz, portalB);
			}

			// ─ Fence railing on deck perimeter ─
			for (int dx = -3; dx <= 3; dx++) {
				ctx.set(mx+dx, deckY+1, mz-3, fenceB);
				ctx.set(mx+dx, deckY+1, mz+3, fenceB);
			}
			for (int dz = -2; dz <= 2; dz++) {
				ctx.set(mx-3, deckY+1, mz+dz, fenceB);
				ctx.set(mx+3, deckY+1, mz+dz, fenceB);
			}

			// ─ Trident crown (3 prongs along X axis) ─
			// Center prong: 10 blocks, arcane with portal cap
			int tridentBase = deckY + 1;
			for (int dy = 1; dy <= 10; dy++)
				ctx.set(mx, tridentBase+dy, mz, arcaneB);
			ctx.set(mx, tridentBase+11, mz, portalB);  // glowing tip

			// Left prong (-X): branches at +3, curves outward
			for (int dy = 1; dy <= 3; dy++)
				ctx.set(mx-1, tridentBase+dy, mz, arcaneB);
			ctx.set(mx-2, tridentBase+4, mz, arcaneB);
			ctx.set(mx-2, tridentBase+5, mz, arcaneB);
			ctx.set(mx-3, tridentBase+6, mz, arcaneB);
			ctx.set(mx-3, tridentBase+7, mz, arcaneB);
			ctx.set(mx-3, tridentBase+8, mz, portalB);  // glowing tip

			// Right prong (+X): mirror of left
			for (int dy = 1; dy <= 3; dy++)
				ctx.set(mx+1, tridentBase+dy, mz, arcaneB);
			ctx.set(mx+2, tridentBase+4, mz, arcaneB);
			ctx.set(mx+2, tridentBase+5, mz, arcaneB);
			ctx.set(mx+3, tridentBase+6, mz, arcaneB);
			ctx.set(mx+3, tridentBase+7, mz, arcaneB);
			ctx.set(mx+3, tridentBase+8, mz, portalB);  // glowing tip

			// Crossbar connecting prongs at branch point
			for (int dx = -2; dx <= 2; dx++)
				ctx.set(mx+dx, tridentBase+3, mz, arcaneB);

			// Portal orbs floating at corner posts of deck
			for (int sx : {-3, 3})
				for (int sz : {-3, 3})
					ctx.set(mx+sx, deckY+2, mz+sz, portalB);
		}

		// ── Village pond (small water feature) ───────────────────
		{
			BlockId waterB = blocks.getId(BlockType::Water);
			BlockId sandB  = blocks.getId(BlockType::Sand);
			int pdx = vc.x + 3, pdz = vc.y + 3;
			int pdy = (int)std::round(groundHeight(seed, (float)pdx+2, (float)pdz+2));
			if (waterB != BLOCK_AIR) {
				// 5x5 pond, 2 blocks deep, sand rim
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

} // namespace modcraft
