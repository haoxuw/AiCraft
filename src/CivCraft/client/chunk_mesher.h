#pragma once

// Chunk meshing — converts a 16³ block volume (plus 1-block neighbor border)
// into a per-vertex stream ready for the GPU.
//
// This header is graphics-backend agnostic: it produces std::vector<ChunkVertex>
// and never touches GL or Vulkan. The GL upload + draw container lives in
// chunk_mesh_gl.h; the Vulkan backend uploads the same vertex stream through
// IRhi::createChunkMesh.

#include "shared/types.h"
#include "shared/chunk.h"
#include "shared/block_registry.h"
#include "shared/chunk_source.h"
#include <vector>
#include <array>

namespace civcraft {

// 13 floats / 52 bytes — same layout the VK chunk-mesh pipeline declares
// in src/platform/shaders/vk/chunk_terrain.vert (locations 0..6).
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

	// 18x18x18 cached block volume (chunk + 1-block border from neighbors)
	// Eliminates per-block getBlock()/mutex/hash-lookup during meshing
	static constexpr int PADDED_SIZE = CHUNK_SIZE + 2;
	std::array<BlockId, PADDED_SIZE * PADDED_SIZE * PADDED_SIZE> m_padded;
};

} // namespace civcraft
