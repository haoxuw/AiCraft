#pragma once

// VillageStamper — paints one village's blocks + structure entities into the
// world at runtime. Phase 4 of the seats overhaul: world-gen produces only
// terrain; villages are stamped per-seat when a seat claims, which is why
// this has to work against an already-generated world (force-loading chunks
// in the footprint, mutating blocks, spawning structure entities).
//
// One stamp = one village, bounded by the template's house/farm/pen footprint
// plus the `clearingRadius` padding. Caller owns the `VillageRecord`; stamper
// just reads its `centerXZ`.

#include "server/world.h"
#include "server/world_template.h"
#include "server/village_registry.h"
#include "logic/types.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace civcraft {

class VillageStamper {
public:
	// Returns the Y of the monument deck (for caller-side entity spawn); -1
	// if this template has no village or no monument.
	static void stamp(World& world,
	                  const VillageRecord& rec,
	                  ConfigurableWorldTemplate& tmpl,
	                  const BlockRegistry& blocks) {
		const auto& cfg = tmpl.pyConfig();
		if (!cfg.hasVillage) return;

		// Conservative XZ bounds: widest house footprint, farm, pen, plus a
		// generous clearing radius so foundations and paths cover ground.
		int minDx = -cfg.clearingRadius, maxDx = cfg.clearingRadius;
		int minDz = -cfg.clearingRadius, maxDz = cfg.clearingRadius;
		for (const auto& h : cfg.houses) {
			minDx = std::min(minDx, h.cx);
			maxDx = std::max(maxDx, h.cx + h.w);
			minDz = std::min(minDz, h.cz);
			maxDz = std::max(maxDz, h.cz + h.d);
		}
		// Farm is +(-12,+16) relative with w=6 d=6; pen is +(+16,-18) with w=10 d=8.
		// Pad a couple of blocks so adjacent path segments land in-chunk.
		minDx = std::min(minDx, -16); maxDx = std::max(maxDx, 28);
		minDz = std::min(minDz, -22); maxDz = std::max(maxDz, 24);

		int x0 = rec.centerXZ.x + minDx;
		int x1 = rec.centerXZ.x + maxDx;
		int z0 = rec.centerXZ.y + minDz;
		int z1 = rec.centerXZ.y + maxDz;

		int cx0 = (int)std::floor((float)x0 / CHUNK_SIZE);
		int cx1 = (int)std::floor((float)x1 / CHUNK_SIZE);
		int cz0 = (int)std::floor((float)z0 / CHUNK_SIZE);
		int cz1 = (int)std::floor((float)z1 / CHUNK_SIZE);
		// Y range: deepest foundation dips ~10 below surface; monument deck
		// sits ~+21 above, tower peak a few more. Pad generously so no block
		// falls out. Anchor on ground height at center — works for both flat
		// (constant surfaceY) and natural (variable) templates.
		//
		// Use worldToChunk for proper floor-division on negative Y — the old
		// std::max(0, …) clamp dropped cy=-1 chunks when a footprint dipped
		// to y=0..-1, so placeFoundation's stone writes landed out-of-chunk
		// and terrain grass survived (W1 regression).
		float gh = tmpl.surfaceHeight(world.seed(),
		                              (float)rec.centerXZ.x, (float)rec.centerXZ.y);
		int cy0 = worldToChunk(0, (int)std::floor(gh) - 12, 0).y;
		int cy1 = worldToChunk(0, (int)std::ceil(gh)  + 34, 0).y;

		int chunksStamped = 0;
		for (int cy = cy0; cy <= cy1; cy++)
			for (int cz = cz0; cz <= cz1; cz++)
				for (int cx = cx0; cx <= cx1; cx++) {
					ChunkPos cp{cx, cy, cz};
					Chunk* c = world.getChunk(cp);  // force-generate terrain
					if (!c) continue;
					tmpl.generateVillageInChunk(*c, cp, world.seed(),
					                            rec.centerXZ, blocks);
					chunksStamped++;
				}
		printf("[VillageStamper] Village %u at (%d,%d) — stamped %d chunks\n",
		       rec.id, rec.centerXZ.x, rec.centerXZ.y, chunksStamped);
	}
};

} // namespace civcraft
