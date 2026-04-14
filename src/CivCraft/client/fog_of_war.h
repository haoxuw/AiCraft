#pragma once

/**
 * FogOfWar — renders translucent fog volumes at unloaded chunk positions.
 *
 * When the client hasn't received a chunk (too far, not yet streamed),
 * instead of showing nothing (visible chunk edges), we render a subtle
 * fog volume that blends with the sky. This creates a "fog of war"
 * effect at the boundary of known terrain.
 */

#include "client/shader.h"
#include "shared/types.h"
#include "shared/chunk_source.h"
#include "client/camera.h"
#include "client/gl.h"
#include <unordered_set>

namespace civcraft {

class FogOfWar {
public:
	bool init(const std::string& shaderDir) {
		if (!m_shader.loadFromFile(shaderDir + "/fog.vert", shaderDir + "/fog.frag"))
			return false;

		// Unit cube (0,0,0) to (1,1,1)
		float verts[] = {
			// Front
			0,0,1, 1,0,1, 1,1,1,  0,0,1, 1,1,1, 0,1,1,
			// Back
			1,0,0, 0,0,0, 0,1,0,  1,0,0, 0,1,0, 1,1,0,
			// Left
			0,0,0, 0,0,1, 0,1,1,  0,0,0, 0,1,1, 0,1,0,
			// Right
			1,0,1, 1,0,0, 1,1,0,  1,0,1, 1,1,0, 1,1,1,
			// Top
			0,1,1, 1,1,1, 1,1,0,  0,1,1, 1,1,0, 0,1,0,
			// Bottom
			0,0,0, 1,0,0, 1,0,1,  0,0,0, 1,0,1, 0,0,1,
		};

		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glBindVertexArray(m_vao);
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
		glEnableVertexAttribArray(0);
		glBindVertexArray(0);
		return true;
	}

	void shutdown() {
		if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
		if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
	}

	void render(const Camera& cam, float aspect, ChunkSource& chunks,
	            int renderDistance, glm::vec3 fogColor, float timeOfDay) {
		if (!m_vao) return;

		// Find the camera's chunk position
		ChunkPos camChunk = {
			(int)std::floor(cam.position.x / CHUNK_SIZE),
			(int)std::floor(cam.position.y / CHUNK_SIZE),
			(int)std::floor(cam.position.z / CHUNK_SIZE)
		};

		glm::mat4 view = cam.viewMatrix();
		glm::mat4 proj = glm::perspective(glm::radians(cam.fov), aspect, 0.1f, 500.0f);
		glm::mat4 vp = proj * view;

		m_shader.use();
		m_shader.setMat4("uVP", vp);
		m_shader.setVec3("uFogColor", fogColor);
		m_shader.setVec3("uCamPos", cam.position);
		m_shader.setFloat("uTime", timeOfDay);

		glBindVertexArray(m_vao);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE); // don't write depth (translucent)

		int R = renderDistance;
		for (int dy = -1; dy <= 2; dy++)
		for (int dz = -R; dz <= R; dz++)
		for (int dx = -R; dx <= R; dx++) {
			ChunkPos cp = {camChunk.x + dx, camChunk.y + dy, camChunk.z + dz};

			// Only render fog where chunk is NOT loaded
			if (chunks.getChunkIfLoaded(cp) != nullptr) continue;

			// Check if at least one neighbor IS loaded (only fog at boundaries)
			bool atBoundary = false;
			ChunkPos neighbors[] = {
				{cp.x-1,cp.y,cp.z}, {cp.x+1,cp.y,cp.z},
				{cp.x,cp.y-1,cp.z}, {cp.x,cp.y+1,cp.z},
				{cp.x,cp.y,cp.z-1}, {cp.x,cp.y,cp.z+1}
			};
			for (auto& n : neighbors) {
				if (chunks.getChunkIfLoaded(n) != nullptr) {
					atBoundary = true;
					break;
				}
			}
			if (!atBoundary) continue;

			// Distance-based opacity (denser further from camera)
			float dist = glm::length(glm::vec3(
				cp.x * CHUNK_SIZE + 8 - cam.position.x,
				cp.y * CHUNK_SIZE + 8 - cam.position.y,
				cp.z * CHUNK_SIZE + 8 - cam.position.z));
			float alpha = glm::clamp(dist / (R * CHUNK_SIZE * 0.7f), 0.15f, 0.6f);

			glm::vec3 worldPos(cp.x * CHUNK_SIZE, cp.y * CHUNK_SIZE, cp.z * CHUNK_SIZE);
			m_shader.setVec3("uChunkPos", worldPos);
			m_shader.setFloat("uAlpha", alpha);

			glDrawArrays(GL_TRIANGLES, 0, 36);
		}

		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
		glBindVertexArray(0);
	}

private:
	Shader m_shader;
	GLuint m_vao = 0, m_vbo = 0;
};

} // namespace civcraft
