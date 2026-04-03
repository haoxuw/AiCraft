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

	// ── Chest: front-right interior of house[0] at floor level ──────
	glm::vec3 chestPosition(int seed, glm::vec3 /*spawnPos*/) const override {
		auto vc = villageCenter(seed);
		if (m_py.houses.empty()) {
			float vy = naturalTerrainHeight(seed, (float)vc.x, (float)vc.y, m_tp);
			return {(float)vc.x + 0.5f, vy + 1.0f, (float)vc.y + 0.5f};
		}
		const auto& h0 = m_py.houses[0];
		// Front-right interior corner, 2 in from right wall and 1 from front wall
		float hx = (float)(vc.x + h0.cx) + (float)(h0.w - 3);
		float hz = (float)(vc.y + h0.cz) + 1.0f;
		float hy = naturalTerrainHeight(seed, hx, hz, m_tp) + 1.0f;  // floor level
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
	WorldPyConfig m_py;
	TerrainParams m_tp;

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
	// 9-wide × 13-tall cobblestone arch centred on the spawn anchor.
	// Opening: 5 wide (centre ±2) × 10 tall — player walks out the +Z face.
	//
	// Geometry (all offsets relative to portal centre px, pz):
	//   Z = pz-2 : back wall (with decorative window)
	//   Z = pz-1 : interior row 1  ← player body clears the back wall here
	//   Z = pz   : interior row 2  ← player spawns here
	//   Z = pz+1 : front arch face (open in centre for door)
	//
	// The 2-block-deep interior ensures the player (halfWidth ≈ 0.4) can
	// never touch the back wall at spawn.  Physics step-up handles exit.
	void generatePortal(Chunk& chunk, ChunkPos cpos, int seed,
	                    BlockId wallB, BlockId stoneB, glm::vec2 anchor) {
		int ox = cpos.x * CHUNK_SIZE;
		int oy = cpos.y * CHUNK_SIZE;
		int oz = cpos.z * CHUNK_SIZE;

		int px = (int)std::round(anchor.x);
		int pz = (int)std::round(anchor.y);
		int groundY = (int)std::round(
			naturalTerrainHeight(seed, (float)px, (float)pz, m_tp));

		auto set = [&](int wx, int wy, int wz, BlockId bid) {
			int lx = wx - ox, ly = wy - oy, lz = wz - oz;
			if (lx >= 0 && lx < CHUNK_SIZE &&
			    ly >= 0 && ly < CHUNK_SIZE &&
			    lz >= 0 && lz < CHUNK_SIZE)
				chunk.set(lx, ly, lz, bid);
		};

		// Wipe the full portal volume first (removes any terrain)
		for (int dy = 0; dy <= 14; dy++)
			for (int dx = -5; dx <= 5; dx++)
				for (int dz = -3; dz <= 2; dz++)
					set(px + dx, groundY + dy, pz + dz, BLOCK_AIR);

		// ── Base layer (dy=0): 9-wide foundation, all 4 depths ──
		for (int dx = -4; dx <= 4; dx++)
			for (int dz = -2; dz <= 1; dz++)
				set(px + dx, groundY, pz + dz, wallB);

		// ── Main side pillars (dx=±4): full height, stone accent rings ──
		for (int sign : {-1, 1}) {
			for (int dz = -2; dz <= 1; dz++) {
				for (int dy = 1; dy <= 12; dy++) {
					BlockId b = (dy % 4 == 0) ? stoneB : wallB;
					set(px + sign * 4, groundY + dy, pz + dz, b);
				}
			}
		}

		// ── Secondary pillars (dx=±3): all depths, front face open below arch ──
		for (int sign : {-1, 1}) {
			for (int dz = -2; dz <= 1; dz++) {
				for (int dy = 1; dy <= 12; dy++) {
					if (dz == 1 && dy <= 10) continue;  // front opening region
					set(px + sign * 3, groundY + dy, pz + dz, wallB);
				}
			}
		}

		// ── Back wall (dz=-2): solid with decorative window ──
		for (int dx = -2; dx <= 2; dx++) {
			for (int dy = 1; dy <= 12; dy++) {
				bool isWindow = (std::abs(dx) <= 1 && dy >= 3 && dy <= 7);
				if (!isWindow)
					set(px + dx, groundY + dy, pz - 2, wallB);
			}
		}

		// ── Front arch top (dz=+1): stepped arch above opening ──
		for (int dx = -2; dx <= 2; dx++)
			set(px + dx, groundY + 11, pz + 1, wallB);
		for (int dx = -1; dx <= 1; dx++)
			set(px + dx, groundY + 12, pz + 1, (dx == 0) ? stoneB : wallB);

		// ── Interior (dz=-1 and dz=0): air between secondary pillars ──
		for (int dz : {-1, 0}) {
			for (int dy = 1; dy <= 12; dy++)
				for (int dx = -2; dx <= 2; dx++)
					set(px + dx, groundY + dy, pz + dz, BLOCK_AIR);
		}

		// ── Battlements (dy=13): crenellations atop main pillars ──
		for (int sign : {-1, 1}) {
			for (int dz = -2; dz <= 1; dz++)
				set(px + sign * 4, groundY + 13, pz + dz, stoneB);
		}
		set(px, groundY + 13, pz + 1, stoneB);  // peak accent over keystone
	}

	// ── Convenience: common generation context ───────────────────────
	// Wraps a chunk+offsets so helpers can call set(worldX, worldY, worldZ, bid)
	// without repeating the bounds check everywhere.
	struct GenCtx {
		Chunk& chunk;
		int ox, oy, oz;

		void set(int wx, int wy, int wz, BlockId bid) const {
			int lx = wx-ox, ly = wy-oy, lz = wz-oz;
			if (lx>=0&&lx<CHUNK_SIZE&&ly>=0&&ly<CHUNK_SIZE&&lz>=0&&lz<CHUNK_SIZE)
				chunk.set(lx,ly,lz,bid);
		}
		BlockId get(int wx, int wy, int wz) const {
			int lx = wx-ox, ly = wy-oy, lz = wz-oz;
			if (lx>=0&&lx<CHUNK_SIZE&&ly>=0&&ly<CHUNK_SIZE&&lz>=0&&lz<CHUNK_SIZE)
				return chunk.get(lx,ly,lz);
			return BLOCK_AIR;
		}
	};

	// ── House walls, floors, stairs, and roof ─────────────────────
	void generateHouse(const GenCtx& ctx, int seed,
	                   BlockId wallB, BlockId roofB, BlockId floorB,
	                   const WorldPyConfig::HouseLayout& h, glm::ivec2 vc) {
		int sh = m_py.storyHeight, dh = m_py.doorHeight, wr = m_py.windowRow;
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		int floorY = (int)std::round(naturalTerrainHeight(seed, (float)hcx+h.w*0.5f,
		                                                        (float)hcz+h.d*0.5f, m_tp)) + 1;
		int totalH = sh * h.stories;

		// Foundation row
		for (int dx = 0; dx < h.w; dx++)
			for (int dz = 0; dz < h.d; dz++)
				ctx.set(hcx+dx, floorY-1, hcz+dz, floorB);

		// Walls, interior, stairs, intermediate floors
		for (int dy = 0; dy < totalH; dy++) {
			for (int dx = 0; dx < h.w; dx++) {
				for (int dz = 0; dz < h.d; dz++) {
					bool wall   = (dx==0||dx==h.w-1||dz==0||dz==h.d-1);
					bool door   = ((dx==h.w/2||dx==h.w/2-1)&&dz==0&&dy<dh);
					bool window = wall && dy==wr && (
						((dz==0||dz==h.d-1)&&(dx==1||dx==h.w-2))||
						((dx==0||dx==h.w-1)&&(dz==1||dz==h.d-2)));

					// Stair steps — each step is at the SAME dy as the player's
					// current feet, so physics step-up (height ≤ 1) carries them up.
					// Story s, step i: at (dx=1, dz=2+i, dy=s*sh+i).
					bool stairStep = false;
					if (h.stories >= 2) {
						for (int s = 0; s < h.stories-1 && !stairStep; s++)
							for (int i = 0; i < sh-1 && !stairStep; i++)
								if (dx==1 && dz==2+i && dy==s*sh+i)
									stairStep = true;
					}

					// Intermediate floor = ceiling of story s, walkable floor of s+1
					bool intermFloor = false;
					if (h.stories >= 2 && !stairStep)
						for (int s = 1; s < h.stories; s++)
							if (dy == s*sh-1) { intermFloor = true; break; }

					BlockId bid;
					if (door || window)          bid = BLOCK_AIR;
					else if (stairStep||intermFloor) bid = floorB;
					else if (dy == totalH-1)     bid = roofB;
					else if (wall)               bid = wallB;
					else                         bid = BLOCK_AIR;

					ctx.set(hcx+dx, floorY+dy, hcz+dz, bid);
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
	                   const WorldPyConfig::HouseLayout& h, glm::ivec2 vc) {
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		int floorY = (int)std::round(naturalTerrainHeight(seed, (float)hcx+h.w*0.5f,
		                                                        (float)hcz+h.d*0.5f, m_tp)) + 1;
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
	// Beds (2-block foot+head), table (2 wood), chairs, and optionally
	// a decorative chest.  House[0] skips the chest — server.h places
	// the starter chest there from chestPosition().
	void generateFurniture(const GenCtx& ctx, int seed,
	                       BlockId woodB, BlockId planksB, BlockId bedB, BlockId chestB,
	                       const WorldPyConfig::HouseLayout& h, glm::ivec2 vc,
	                       bool isMainHouse) {
		int hcx = vc.x + h.cx, hcz = vc.y + h.cz;
		int floorY = (int)std::round(naturalTerrainHeight(seed, (float)hcx+h.w*0.5f,
		                                                        (float)hcz+h.d*0.5f, m_tp)) + 1;
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
			int surfY = (int)std::round(naturalTerrainHeight(seed, (float)wx, (float)wz, m_tp));
			ctx.set(wx,   surfY, wz, pathB);
			ctx.set(wx+1, surfY, wz, pathB);
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

		for (int hi = 0; hi < (int)m_py.houses.size(); hi++) {
			const auto& h = m_py.houses[hi];

			// Per-house material overrides (Python can set wall/roof per house)
			BlockId hWallB = (!h.wallBlock.empty()) ? blocks.getId(h.wallBlock) : wallB;
			BlockId hRoofB = (!h.roofBlock.empty()) ? blocks.getId(h.roofBlock) : roofB;
			if (hWallB == BLOCK_AIR) hWallB = wallB;
			if (hRoofB == BLOCK_AIR) hRoofB = roofB;

			generateHouse(ctx, seed, hWallB, hRoofB, floorB, h, vc);
			generatePorch(ctx, seed, pathB, hWallB, h, vc);
			generateFurniture(ctx, seed, woodB, planksB, bedB, chestB, h, vc, hi == 0);
		}

		generatePaths(ctx, seed, pathB, vc);
	}
};

} // namespace agentworld
