#pragma once

#include "common/types.h"
#include "common/chunk.h"
#include "common/block_registry.h"
#include "common/world.h"
#include <glad/gl.h>
#include <vector>
#include <array>

namespace aicraft {

struct ChunkVertex {
	glm::vec3 position;
	glm::vec3 color;
	glm::vec3 normal;
	float ao;
	float shade;
	float alpha;
};

struct ChunkMesh {
	GLuint vao = 0;
	GLuint vbo = 0;
	int vertexCount = 0;
	ChunkPos pos;

	void upload(const std::vector<ChunkVertex>& vertices);
	void draw() const;
	void destroy();
};

class ChunkMesher {
public:
	std::vector<ChunkVertex> buildMesh(World& world, ChunkPos pos);

private:
	float computeAO(bool side1, bool side2, bool corner);
	void fillPaddedVolume(World& world, ChunkPos cpos);

	// 18x18x18 cached block volume (chunk + 1-block border from neighbors)
	// Eliminates per-block getBlock()/mutex/hash-lookup during meshing
	static constexpr int PADDED_SIZE = CHUNK_SIZE + 2;
	std::array<BlockId, PADDED_SIZE * PADDED_SIZE * PADDED_SIZE> m_padded;
};

} // namespace aicraft
