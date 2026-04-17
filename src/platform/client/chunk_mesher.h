#pragma once

// Chunk mesher: 16³ volume + 1-block neighbor border → vertex stream. Backend-agnostic;
// both GL and VK consume via IRhi::createChunkMesh (same 13-float layout).

#include "logic/types.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/chunk_source.h"
#include <vector>
#include <array>

namespace civcraft {

// 13 floats / 52 bytes — MUST match shaders/vk/chunk_terrain.vert locations 0..6.
struct ChunkVertex {
	glm::vec3 position;
	glm::vec3 color;
	glm::vec3 normal;
	float ao;
	float shade;
	float alpha;
	float glow;   // 1.0 = magical surface animation, 0.0 = normal
};

class ChunkMesher {
public:
	// Returns {opaque vertices, transparent vertices}
	std::pair<std::vector<ChunkVertex>, std::vector<ChunkVertex>>
		buildMesh(ChunkSource& world, ChunkPos pos);

private:
	float computeAO(bool side1, bool side2, bool corner);
	void fillPaddedVolume(ChunkSource& world, ChunkPos cpos);

	// 18³ cached volume (chunk + border). Eliminates per-block getBlock()/mutex during meshing.
	static constexpr int PADDED_SIZE = CHUNK_SIZE + 2;
	std::array<BlockId, PADDED_SIZE * PADDED_SIZE * PADDED_SIZE> m_padded;
};

} // namespace civcraft
