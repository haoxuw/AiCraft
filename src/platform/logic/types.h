#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>
#include <functional>

namespace civcraft {

// Kept out of server.h so non-server TUs can depend on it.
using ClientId = uint32_t;

constexpr int CHUNK_SIZE = 16;
constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;

struct ChunkPos {
	int x, y, z;

	bool operator==(const ChunkPos& o) const {
		return x == o.x && y == o.y && z == o.z;
	}

	glm::vec3 worldOrigin() const {
		return glm::vec3(x * CHUNK_SIZE, y * CHUNK_SIZE, z * CHUNK_SIZE);
	}
};

struct ChunkPosHash {
	size_t operator()(const ChunkPos& p) const {
		size_t h = 0;
		h ^= std::hash<int>()(p.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>()(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>()(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}
};

constexpr glm::ivec3 FACE_DIRS[6] = {
	{ 1,  0,  0},
	{-1,  0,  0},
	{ 0,  1,  0},
	{ 0, -1,  0},
	{ 0,  0,  1},
	{ 0,  0, -1},
};

constexpr float FACE_SHADE[6] = {
	0.80f,
	0.80f,
	1.00f,
	0.50f,
	0.90f,
	0.90f,
};

inline ChunkPos worldToChunk(int wx, int wy, int wz) {
	return {
		(wx < 0 ? (wx + 1) / CHUNK_SIZE - 1 : wx / CHUNK_SIZE),
		(wy < 0 ? (wy + 1) / CHUNK_SIZE - 1 : wy / CHUNK_SIZE),
		(wz < 0 ? (wz + 1) / CHUNK_SIZE - 1 : wz / CHUNK_SIZE)
	};
}

} // namespace civcraft
