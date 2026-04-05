#pragma once

#include "shared/types.h"
#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/chunk_source.h"
#include "client/gl.h"
#include <vector>
#include <array>

namespace agentica {

struct ChunkVertex {
	glm::vec3 position;
	glm::vec3 color;
	glm::vec3 normal;
	float ao;
	float shade;
	float alpha;
	float glow;   // 1.0 = magical surface animation, 0.0 = normal
};

struct ChunkMesh {
	GLuint vao = 0, vbo = 0;
	int vertexCount = 0;
	GLuint tVao = 0, tVbo = 0;
	int tVertexCount = 0;
	ChunkPos pos;

	void upload(const std::vector<ChunkVertex>& opaque,
	            const std::vector<ChunkVertex>& transparent);
	void draw() const;
	void drawTransparent() const;
	void destroy();
};

class ChunkMesher {
public:
	// Returns {opaque vertices, transparent vertices}
	std::pair<std::vector<ChunkVertex>, std::vector<ChunkVertex>>
		buildMesh(ChunkSource& world, ChunkPos pos);

private:
	float computeAO(bool side1, bool side2, bool corner);
	void fillPaddedVolume(ChunkSource& world, ChunkPos cpos);

	// 18x18x18 cached block volume (chunk + 1-block border from neighbors)
	// Eliminates per-block getBlock()/mutex/hash-lookup during meshing
	static constexpr int PADDED_SIZE = CHUNK_SIZE + 2;
	std::array<BlockId, PADDED_SIZE * PADDED_SIZE * PADDED_SIZE> m_padded;
};

} // namespace agentica
