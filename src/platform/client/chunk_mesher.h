#pragma once

// Chunk mesher: 16³ volume + 1-block neighbor border → vertex stream.
// RHI consumes via IRhi::createChunkMesh (13-float vertex layout).

#include "logic/types.h"
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/chunk_source.h"
#include <vector>
#include <array>

namespace solarium {

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
	// 18³ cached volume (chunk + 1-block border).
	static constexpr int PADDED_SIZE = CHUNK_SIZE + 2;
	static constexpr int PADDED_VOL  = PADDED_SIZE * PADDED_SIZE * PADDED_SIZE;
	struct PaddedSnapshot {
		std::array<BlockId, PADDED_VOL> blocks;
		std::array<uint8_t, PADDED_VOL> param2;      // only center-chunk cells are used (stairs/doors)
		std::array<uint8_t, PADDED_VOL> appearance;  // palette index per cell (see docs/22_APPEARANCE.md)
	};

	// Main-thread convenience: snapshot live world + mesh in one call.
	std::pair<std::vector<ChunkVertex>, std::vector<ChunkVertex>>
		buildMesh(ChunkSource& world, ChunkPos pos);

	// Async path: main thread calls snapshotPadded() to capture block data
	// under whatever thread-safety ChunkSource requires, then any thread may
	// call buildMeshFromSnapshot() on the result — it only reads the padded
	// snapshot + the (immutable-after-init) BlockRegistry. Returns false if
	// the center chunk is not loaded.
	static bool snapshotPadded(ChunkSource& world, ChunkPos pos,
	                           PaddedSnapshot& out);

	static std::pair<std::vector<ChunkVertex>, std::vector<ChunkVertex>>
		buildMeshFromSnapshot(const PaddedSnapshot& padded, ChunkPos pos,
		                      const BlockRegistry& reg);

private:
	static float computeAO(bool side1, bool side2, bool corner);
	void fillPaddedVolume(ChunkSource& world, ChunkPos cpos);

	// Eliminates per-block getBlock()/mutex during meshing.
	PaddedSnapshot m_padded;
};

} // namespace solarium
