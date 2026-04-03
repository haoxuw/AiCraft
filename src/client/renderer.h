#pragma once

#include "shared/types.h"
#include "shared/chunk_source.h"
#include "client/shader.h"
#include "client/camera.h"
#include "client/chunk_mesher.h"
#include "client/model.h"
#include <unordered_map>
#include <unordered_set>

namespace agentworld {

class Renderer {
public:
	bool init(const std::string& shaderDir);
	void shutdown();

	void updateChunks(ChunkSource& world, const Camera& cam, int renderDistance);
	void meshAllPending(ChunkSource& world, const Camera& cam, int renderDistance);
	void render(const Camera& cam, float aspect, glm::ivec3* highlight = nullptr,
	            int selectedSlot = 0, int hotbarSize = 7,
	            glm::vec2 crosshairOffset = {0, 0}, bool showCrosshair = true);
	ModelRenderer& modelRenderer() { return m_modelRenderer; }
	Shader& highlightShader() { return m_highlightShader; }
	void markChunkDirty(ChunkPos pos);
	void setTimeOfDay(float t); // 0=midnight, 0.5=noon
	void renderMoveTarget(const Camera& cam, float aspect, glm::ivec3 pos);
	void renderBreakProgress(const Camera& cam, float aspect, glm::ivec3 pos, float progress);
	float sunStrength() const { return m_sunStrength; }

private:
	void renderSky(const Camera& cam, float aspect);
	void renderTerrain(const Camera& cam, float aspect);
	void renderHighlight(const Camera& cam, float aspect, glm::ivec3 pos);
	void renderHotbar(float aspect, int selectedSlot, int hotbarSize);
	void renderCrosshair(float aspect, glm::vec2 center = {0, 0});

	Shader m_terrainShader;
	Shader m_skyShader;
	Shader m_crosshairShader;
	Shader m_highlightShader;

	GLuint m_skyVAO = 0, m_skyVBO = 0;
	GLuint m_crosshairVAO = 0, m_crosshairVBO = 0;
	GLuint m_highlightVAO = 0, m_highlightVBO = 0;
	GLuint m_quadVAO = 0, m_quadVBO = 0;
	ModelRenderer m_modelRenderer;

	ChunkMesher m_mesher;
	std::unordered_map<ChunkPos, ChunkMesh, ChunkPosHash> m_meshes;
	std::unordered_set<ChunkPos, ChunkPosHash> m_dirtyChunks;

	glm::vec3 m_skyColor = {0.40f, 0.60f, 0.85f};
	glm::vec3 m_horizonColor = {0.70f, 0.82f, 0.92f};
	glm::vec3 m_sunDir = glm::normalize(glm::vec3(0.5f, 0.85f, 0.3f));
	float m_fogStart = 100.0f;
	float m_fogEnd = 160.0f;
	float m_timeOfDay = 0.5f; // noon
	float m_sunStrength = 1.0f;
};

} // namespace agentworld
