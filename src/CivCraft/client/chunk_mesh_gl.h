#pragma once

// Per-chunk GPU mesh container for the OpenGL backend.
//
// Wraps the {opaque, transparent} vertex streams produced by ChunkMesher into
// a pair of VAO/VBO handles. The Vulkan backend bypasses this struct entirely
// and uploads the same vertex stream through IRhi::createChunkMesh — so this
// file is only compiled / included by the GL renderer path.

#include "client/chunk_mesher.h"
#include "client/gfx.h"

namespace civcraft {

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

} // namespace civcraft
