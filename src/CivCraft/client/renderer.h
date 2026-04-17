#pragma once

#include "logic/types.h"
#include "shared/chunk_source.h"
#include "client/shader.h"
#include "client/fog_of_war.h"
#include "client/camera.h"
#include "client/chunk_mesher.h"
#include "client/model.h"
#include "client/rhi/rhi.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace civcraft {

struct DoorAnim {
	glm::ivec3 basePos;   // world position of the door block (bottom of column)
	int        height;    // number of door blocks in column
	float      timer;     // elapsed time
	bool       opening;   // true = closed→open, false = open→closed
	bool       hingeRight;// true = right hinge (param2 bit 2)
	glm::vec3  color;     // block color_side
};

class Renderer {
public:
	bool init(rhi::IRhi* rhi, const std::string& shaderDir);
	void shutdown();

	void updateChunks(ChunkSource& world, const Camera& cam, int renderDistance);
	void meshAllPending(ChunkSource& world, const Camera& cam, int renderDistance);
	void render(const Camera& cam, float aspect, glm::ivec3* highlight = nullptr,
	            int selectedSlot = 0, int hotbarSize = 7,
	            glm::vec2 crosshairOffset = {0, 0}, bool showCrosshair = true);
	void renderFogOfWar(const Camera& cam, float aspect, ChunkSource& chunks, int renderDistance);
	ModelRenderer& modelRenderer() { return m_modelRenderer; }
	Shader& highlightShader() { return m_highlightShader; }
	void markChunkDirty(ChunkPos pos);
	void setTimeOfDay(float t); // 0=midnight, 0.5=noon
	void tick(float dt) { m_time += dt; m_hitmarkerTimer = std::max(0.0f, m_hitmarkerTimer - dt); }
	void triggerHitmarker(bool isKill = false) { m_hitmarkerTimer = 0.18f; m_hitmarkerKill = isKill; }
	void renderMoveTarget(const Camera& cam, float aspect, glm::ivec3 pos);

	// Spinning polygon marker hovering above a destination block. Used by
	// the RTS move-target indicator: `sides=3` = triangle (Walk command),
	// `sides=6` = hexagon (Build command). `spinRad` is the current rotation
	// around the Y axis (caller advances with elapsed time).
	void renderMoveTargetSpinner(const Camera& cam, float aspect,
	                             glm::vec3 pos, int sides, glm::vec4 color,
	                             float spinRad);
	void renderBreakProgress(const Camera& cam, float aspect, glm::ivec3 pos, float progress);
	void renderDoorAnims(const Camera& cam, float aspect,
	                     const std::vector<DoorAnim>& anims);
	float sunStrength() const { return m_sunStrength; }
	float time() const { return m_time; }

	// Render an animated flowing dashed path from a list of world-space points.
	// Used for agent plan visualization.
	void renderPlanPath(const Camera& cam, float aspect,
	                    const std::vector<glm::vec3>& points,
	                    glm::vec4 color, float dashLen, float time);

	// Per-chunk RHI mesh handles. Public so the .cpp file's free helpers can
	// refer to it without a friend declaration; Renderer alone manages
	// lifetime via createChunkMesh / destroyMesh.
	struct ChunkMeshSlot {
		rhi::IRhi::MeshHandle opaque      = rhi::IRhi::kInvalidMesh;
		rhi::IRhi::MeshHandle transparent = rhi::IRhi::kInvalidMesh;
		uint32_t opaqueVerts = 0;
		uint32_t transparentVerts = 0;
		glm::vec3 mn{0}, mx{0};
	};

private:
	void renderSky(const Camera& cam, float aspect);
	void renderTerrain(const Camera& cam, float aspect);
	void renderHighlight(const Camera& cam, float aspect, glm::ivec3 pos);
	void renderCrosshair(float aspect, glm::vec2 center = {0, 0});

	rhi::IRhi* m_rhi = nullptr;

	// Terrain rendering is fully owned by the RHI now (terrain.vert/frag live
	// in src/platform/shaders/, the GL backend's createChunkMesh handles
	// upload, drawChunkMesh* runs the per-pass GL state). No m_terrainShader
	// here anymore.
	Shader m_crosshairShader;
	Shader m_highlightShader;

	GLuint m_crosshairVAO = 0, m_crosshairVBO = 0;
	GLuint m_highlightVAO = 0, m_highlightVBO = 0;
	GLuint m_crackVAO = 0, m_crackVBO = 0;
	GLuint m_quadVAO = 0, m_quadVBO = 0;
	GLuint m_pathVAO = 0, m_pathVBO = 0;
	// Door-anim verts ride on the RHI chunk-mesh pipeline — one persistent
	// handle, re-uploaded each frame any door is mid-animation.
	rhi::IRhi::MeshHandle m_doorAnimMesh = rhi::IRhi::kInvalidMesh;
	ModelRenderer m_modelRenderer;

	ChunkMesher m_mesher;

	std::unordered_map<ChunkPos, ChunkMeshSlot, ChunkPosHash> m_meshes;
	std::unordered_set<ChunkPos, ChunkPosHash> m_dirtyChunks;

	glm::vec3 m_skyColor = {0.40f, 0.60f, 0.85f};
	glm::vec3 m_horizonColor = {0.70f, 0.82f, 0.92f};
	glm::vec3 m_sunDir = glm::normalize(glm::vec3(0.5f, 0.85f, 0.3f));
	float m_fogStart = 100.0f;
	float m_fogEnd = 160.0f;
	float m_timeOfDay = 0.5f; // noon
	float m_sunStrength = 1.0f;
	float m_time = 0.0f; // elapsed seconds (for shader animations)

	// Hitmarker crosshair flash
	float m_hitmarkerTimer = 0.0f; // counts down from 0.18s
	bool  m_hitmarkerKill  = false; // true = red kill shot, false = orange hit

	// Fog of war
	FogOfWar m_fogOfWar;
};

} // namespace civcraft
