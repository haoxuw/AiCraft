#pragma once

// Default fill for chunks that have NEVER been streamed / generated.
// One policy, two outcomes:
//
//   cy >= 0 → AIR  (sky / above the bbox top of a baked region)
//   cy <  0 → DIRT (underground / below the floor of a baked region)
//
// Why: voxel-earth bakes only contain a finite Y band. Above bbox_max the
// chunks are sky; below bbox_min the chunks should feel like solid ground
// when the player digs through. Today the engine returned AIR for any
// unloaded chunk, so digging past the streamed shell drops the player
// into nothing. With this default, "unloaded below floor" reads as dirt
// without the server having to send those chunks at all — the absence of
// an S_CHUNK is itself the signal.
//
// Resolution: BLOCK_AIR is a compile-time constant (id 0); Dirt is data
// (registered by builtin.cpp), so we look it up once at world boot and
// stash both BlockIds here. The hot path (`forChunkY(cy)`) is two compares.

#include "logic/block_registry.h"
#include "logic/constants.h"

namespace solarium {

struct DefaultFill {
	BlockId air  = BLOCK_AIR;
	BlockId dirt = BLOCK_AIR;     // overwritten by resolve()

	void resolve(const BlockRegistry& reg) {
		air  = BLOCK_AIR;
		dirt = reg.getId(BlockType::Dirt);
		if (dirt == BLOCK_AIR) dirt = BLOCK_AIR;  // headless tests: no Dirt → fall back to AIR
	}

	BlockId forChunkY(int cy) const { return cy >= 0 ? air : dirt; }
};

}  // namespace solarium
