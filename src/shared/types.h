#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>
#include <functional>

namespace agentworld {

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

// 6 face directions
constexpr glm::ivec3 FACE_DIRS[6] = {
	{ 1,  0,  0}, // +X (east)
	{-1,  0,  0}, // -X (west)
	{ 0,  1,  0}, // +Y (up)
	{ 0, -1,  0}, // -Y (down)
	{ 0,  0,  1}, // +Z (south)
	{ 0,  0, -1}, // -Z (north)
};

// Face normals as floats for shading
constexpr float FACE_SHADE[6] = {
	0.80f, // +X
	0.80f, // -X
	1.00f, // +Y (top is brightest)
	0.50f, // -Y (bottom is darkest)
	0.90f, // +Z
	0.90f, // -Z
};

// Convert world coordinates to chunk position
inline ChunkPos worldToChunk(int wx, int wy, int wz) {
	return {
		(wx < 0 ? (wx + 1) / CHUNK_SIZE - 1 : wx / CHUNK_SIZE),
		(wy < 0 ? (wy + 1) / CHUNK_SIZE - 1 : wy / CHUNK_SIZE),
		(wz < 0 ? (wz + 1) / CHUNK_SIZE - 1 : wz / CHUNK_SIZE)
	};
}

} // namespace agentworld
