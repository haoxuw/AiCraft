#pragma once

#include "logic/types.h"
#include "logic/block_registry.h"
#include <array>

namespace civcraft {

class Chunk {
public:
	Chunk() { m_blocks.fill(BLOCK_AIR); m_param2.fill(0); }

	BlockId get(int x, int y, int z) const {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return BLOCK_AIR;
		return m_blocks[index(x, y, z)];
	}

	// param2: rotation data. FourDir bits 0-1 = facing: 0=+Z, 1=+X, 2=-Z, 3=-X.
	uint8_t getParam2(int x, int y, int z) const {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return 0;
		return m_param2[index(x, y, z)];
	}

	void set(int x, int y, int z, BlockId type, uint8_t p2 = 0) {
		if (x < 0 || x >= CHUNK_SIZE || y < 0 || y >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE)
			return;
		int i = index(x, y, z);
		m_blocks[i] = type;
		m_param2[i] = p2;
		m_dirty = true;
	}

	bool isDirty() const { return m_dirty; }
	void clearDirty() { m_dirty = false; }

	// Serialization access
	BlockId getRaw(int i) const { return m_blocks[i]; }
	void setRaw(int i, BlockId v) { m_blocks[i] = v; m_dirty = true; }
	const std::array<BlockId, CHUNK_VOLUME>& rawBlocks() const { return m_blocks; }
	void setRawBlocks(const std::array<BlockId, CHUNK_VOLUME>& data) { m_blocks = data; m_dirty = true; }

	uint8_t getRawParam2(int i) const { return m_param2[i]; }
	void setRawParam2(int i, uint8_t v) { m_param2[i] = v; }
	const std::array<uint8_t, CHUNK_VOLUME>& rawParam2() const { return m_param2; }
	void setRawParam2Array(const std::array<uint8_t, CHUNK_VOLUME>& data) { m_param2 = data; }

private:
	static int index(int x, int y, int z) {
		return y * CHUNK_SIZE * CHUNK_SIZE + z * CHUNK_SIZE + x;
	}

	std::array<BlockId, CHUNK_VOLUME> m_blocks;
	std::array<uint8_t, CHUNK_VOLUME> m_param2;
	bool m_dirty = true;
};

} // namespace civcraft
