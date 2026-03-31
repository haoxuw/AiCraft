#pragma once

#include "common/types.h"
#include "common/chunk.h"
#include "common/block_registry.h"
#include "common/constants.h"
#include "common/noise.h"
#include <string>
#include <cmath>

namespace aicraft {

// ============================================================
// Base class for world generation templates
// ============================================================
class WorldTemplate {
public:
	virtual ~WorldTemplate() = default;

	virtual std::string name() const = 0;
	virtual std::string description() const = 0;

	// Generate terrain for a single chunk
	virtual void generate(Chunk& chunk, ChunkPos cpos, int seed,
	                      const BlockRegistry& blocks) = 0;

	// Get the surface height at a world XZ position (for spawn placement)
	virtual float surfaceHeight(int seed, float x, float z) = 0;
};

// ============================================================
// Flat World: grass at y=4, dirt y=0..3, stone below y=0
// ============================================================
class FlatWorldTemplate : public WorldTemplate {
public:
	std::string name() const override { return "Flat World"; }
	std::string description() const override { return "Flat grass plane, ideal for building"; }

	float surfaceHeight(int, float, float) override { return 4.0f; }

	void generate(Chunk& chunk, ChunkPos cpos, int,
	              const BlockRegistry& blocks) override {
		BlockId stone = blocks.getId(BlockType::Stone);
		BlockId dirt  = blocks.getId(BlockType::Dirt);
		BlockId grass = blocks.getId(BlockType::Grass);

		int oy = cpos.y * CHUNK_SIZE;

		for (int ly = 0; ly < CHUNK_SIZE; ly++) {
			int wy = oy + ly;
			BlockId type;
			if (wy < 0)       type = stone;
			else if (wy < 4)  type = dirt;
			else if (wy == 4) type = grass;
			else               continue; // above surface = air (already default)

			for (int lz = 0; lz < CHUNK_SIZE; lz++)
				for (int lx = 0; lx < CHUNK_SIZE; lx++)
					chunk.set(lx, ly, lz, type);
		}
	}
};

// ============================================================
// Village World: rolling terrain with trees and village structures
// ============================================================
class VillageWorldTemplate : public WorldTemplate {
public:
	std::string name() const override { return "Village"; }
	std::string description() const override { return "Rolling hills, trees, and a village"; }

	float surfaceHeight(int seed, float x, float z) override {
		return terrainHeight(seed, x, z);
	}

	void generate(Chunk& chunk, ChunkPos cpos, int seed,
	              const BlockRegistry& blocks) override {
		BlockId stone  = blocks.getId(BlockType::Stone);
		BlockId dirt   = blocks.getId(BlockType::Dirt);
		BlockId grass  = blocks.getId(BlockType::Grass);
		BlockId sand   = blocks.getId(BlockType::Sand);
		BlockId water  = blocks.getId(BlockType::Water);
		BlockId snow   = blocks.getId(BlockType::Snow);
		BlockId wood   = blocks.getId(BlockType::Wood);
		BlockId leaves = blocks.getId(BlockType::Leaves);
		BlockId cobble = blocks.getId(BlockType::Cobblestone);

		int ox = cpos.x * CHUNK_SIZE;
		int oy = cpos.y * CHUNK_SIZE;
		int oz = cpos.z * CHUNK_SIZE;

		// --- Terrain ---
		for (int lz = 0; lz < CHUNK_SIZE; lz++) {
			for (int lx = 0; lx < CHUNK_SIZE; lx++) {
				int wx = ox + lx;
				int wz = oz + lz;
				int surfaceY = (int)std::round(terrainHeight(seed, (float)wx, (float)wz));

				for (int ly = 0; ly < CHUNK_SIZE; ly++) {
					int wy = oy + ly;

					if (wy > surfaceY) {
						if (wy <= -2)
							chunk.set(lx, ly, lz, water);
						// else air (default)
					} else if (wy == surfaceY) {
						if (surfaceY <= -2)      chunk.set(lx, ly, lz, sand);
						else if (surfaceY > 18)  chunk.set(lx, ly, lz, snow);
						else                     chunk.set(lx, ly, lz, grass);
					} else if (wy > surfaceY - 4) {
						chunk.set(lx, ly, lz, (surfaceY <= -1) ? sand : dirt);
					} else {
						chunk.set(lx, ly, lz, stone);
					}
				}
			}
		}

		// --- Trees (skip chunk border to avoid cross-chunk leaf issues) ---
		for (int lz = 2; lz < CHUNK_SIZE - 2; lz++) {
			for (int lx = 2; lx < CHUNK_SIZE - 2; lx++) {
				int wx = ox + lx, wz = oz + lz;

				// Deterministic tree placement via hash
				if (hashFloat(wx * 7 + seed, wz * 13) < 0.97f) continue;

				int surfaceY = (int)std::round(terrainHeight(seed, (float)wx, (float)wz));
				if (surfaceY < 0 || surfaceY > 16) continue;

				// Don't place trees in the village clearing
				if (isVillageArea(seed, wx, wz)) continue;

				int trunkBase = surfaceY + 1;
				int trunkHeight = 4 + (int)(hashFloat(wx + 99, wz + 99) * 3);
				int trunkTop = trunkBase + trunkHeight - 1;

				// Trunk
				for (int ty = trunkBase; ty <= trunkTop; ty++) {
					int ly = ty - oy;
					if (ly >= 0 && ly < CHUNK_SIZE)
						chunk.set(lx, ly, lz, wood);
				}

				// Leaves (sphere around trunk top)
				int leafR = 2;
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
								chunk.set(lxx, lyy, lzz, leaves);
						}
					}
				}
			}
		}

		// --- Village structures ---
		generateVillage(chunk, cpos, seed, cobble, wood);
	}

private:
	// Village center location (deterministic from seed)
	static glm::ivec2 villageCenter(int seed) {
		float sx = 30, sz = 30;
		for (int t = 0; t < 50; t++) {
			if (terrainHeight(seed, sx, sz) > 2 && terrainHeight(seed, sx, sz) < 15) break;
			sx += 7; sz += 3;
		}
		return {(int)sx + 15, (int)sz + 15};
	}

	static bool isVillageArea(int seed, int wx, int wz) {
		auto vc = villageCenter(seed);
		int dx = wx - vc.x, dz = wz - vc.y;
		return dx*dx + dz*dz < 20*20;
	}

	void generateVillage(Chunk& chunk, ChunkPos cpos, int seed,
	                     BlockId cobble, BlockId wood) {
		auto vc = villageCenter(seed);
		int ox = cpos.x * CHUNK_SIZE;
		int oy = cpos.y * CHUNK_SIZE;
		int oz = cpos.z * CHUNK_SIZE;

		// Houses around the village center
		struct House { int cx, cz, w, d, h; };
		House houses[] = {
			{vc.x,      vc.y,      5, 5, 4},
			{vc.x + 10, vc.y - 2,  4, 6, 3},
			{vc.x - 8,  vc.y + 5,  6, 4, 4},
			{vc.x + 3,  vc.y + 12, 5, 5, 3},
		};

		for (auto& h : houses) {
			int floorY = (int)std::round(terrainHeight(seed, (float)h.cx, (float)h.cz)) + 1;

			for (int dy = 0; dy < h.h; dy++) {
				int ly = floorY + dy - oy;
				if (ly < 0 || ly >= CHUNK_SIZE) continue;

				for (int dx = 0; dx < h.w; dx++) {
					for (int dz = 0; dz < h.d; dz++) {
						int lx = h.cx + dx - ox;
						int lz = h.cz + dz - oz;
						if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;

						bool wall = (dx == 0 || dx == h.w-1 || dz == 0 || dz == h.d-1);
						bool door = (dx == h.w/2 && dz == 0 && dy < 2);

						if (door)           chunk.set(lx, ly, lz, BLOCK_AIR);
						else if (dy == 0)   chunk.set(lx, ly, lz, cobble); // floor
						else if (dy == h.h-1) chunk.set(lx, ly, lz, wood); // roof
						else if (wall)      chunk.set(lx, ly, lz, cobble);
						else                chunk.set(lx, ly, lz, BLOCK_AIR); // interior
					}
				}
			}
		}

		// Cobblestone path through village
		for (int dz = -15; dz <= 15; dz++) {
			int wx = vc.x + 2, wz = vc.y + dz;
			int lx = wx - ox, lz = wz - oz;
			if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE) continue;

			int surfY = (int)std::round(terrainHeight(seed, (float)wx, (float)wz));
			int ly = surfY - oy;
			if (ly >= 0 && ly < CHUNK_SIZE) {
				chunk.set(lx, ly, lz, cobble);
				if (lx + 1 < CHUNK_SIZE) chunk.set(lx + 1, ly, lz, cobble);
			}
		}
	}
};

} // namespace aicraft
