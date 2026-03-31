#pragma once

#include "common/types.h"
#include "common/block_registry.h"
#include <array>

namespace aicraft {

class Chunk {
public:
	Chunk() { m_blocks.fill(BLOCK_AIR); }

	BlockId get(int x, int y, int z) const {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return BLOCK_AIR;
		return m_blocks[index(x, y, z)];
	}

	void set(int x, int y, int z, BlockId type) {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return;
		m_blocks[index(x, y, z)] = type;
		m_dirty = true;
	}

	bool isDirty() const { return m_dirty; }
	void clearDirty() { m_dirty = false; }

private:
	static int index(int x, int y, int z) {
		return y * CHUNK_SIZE * CHUNK_SIZE + z * CHUNK_SIZE + x;
	}

	std::array<BlockId, CHUNK_VOLUME> m_blocks;
	bool m_dirty = true;
};

} // namespace aicraft
