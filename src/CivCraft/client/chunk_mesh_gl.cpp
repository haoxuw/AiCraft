#include "client/chunk_mesh_gl.h"

namespace civcraft {

static void uploadToVAO(GLuint& vao, GLuint& vbo, int& count,
                        const std::vector<ChunkVertex>& vertices) {
	count = (int)vertices.size();
	if (count == 0) return;
	if (!vao) {
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
	}
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(ChunkVertex),
		vertices.data(), GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, position));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, color));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, normal));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, ao));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, shade));
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, alpha));
	glEnableVertexAttribArray(5);
	glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(ChunkVertex), (void*)offsetof(ChunkVertex, glow));
	glEnableVertexAttribArray(6);
	glBindVertexArray(0);
}

void ChunkMesh::upload(const std::vector<ChunkVertex>& opaque,
                       const std::vector<ChunkVertex>& transparent) {
	uploadToVAO(vao, vbo, vertexCount, opaque);
	uploadToVAO(tVao, tVbo, tVertexCount, transparent);
}

void ChunkMesh::draw() const {
	if (vertexCount == 0) return;
	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLES, 0, vertexCount);
}

void ChunkMesh::drawTransparent() const {
	if (tVertexCount == 0) return;
	glBindVertexArray(tVao);
	glDrawArrays(GL_TRIANGLES, 0, tVertexCount);
}

void ChunkMesh::destroy() {
	if (vbo) glDeleteBuffers(1, &vbo);
	if (vao) glDeleteVertexArrays(1, &vao);
	if (tVbo) glDeleteBuffers(1, &tVbo);
	if (tVao) glDeleteVertexArrays(1, &tVao);
	vao = vbo = tVao = tVbo = 0;
	vertexCount = tVertexCount = 0;
}

} // namespace civcraft
