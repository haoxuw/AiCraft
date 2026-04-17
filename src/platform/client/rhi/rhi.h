#pragma once

// Render Hardware Interface — GL (legacy/web) or Vulkan (native) backend.

struct GLFWwindow;   // fwd-decl: avoid pulling backend headers

#include <cstdint>

namespace civcraft::rhi {

struct InitInfo {
	GLFWwindow* window = nullptr;
	int width = 0;
	int height = 0;
	const char* appName = "civcraft";
	bool enableValidation = true;
};

class IRhi {
public:
	virtual ~IRhi() = default;

	virtual bool init(const InitInfo& info) = 0;
	virtual void shutdown() = 0;

	virtual void onResize(int width, int height) = 0;

	// Returns false to skip frame (swapchain out of date).
	virtual bool beginFrame() = 0;
	virtual void endFrame() = 0;

	virtual bool initImGui() = 0;
	virtual void shutdownImGui() = 0;
	virtual void imguiNewFrame() = 0;
	// Between beginFrame and endFrame.
	virtual void imguiRender() = 0;

	// Between beginFrame and imguiRender / endFrame.
	virtual void drawCube(const float mvp[16]) = 0;

	// std140 layout — stable binary contract with shaders (reserved slots
	// were seasonPhase/rainAmt, now in composite UBO).
	struct SceneParams {
		float viewProj[16];
		float camPos[3]; float time;
		float sunDir[3]; float sunStr;
		float _reserved0;
		float _reserved1;
		float _reserved[2];  // pad to 16B
	};

	// std140, 16B alignment — groups of 4 floats mirror GLSL exactly.
	// Defaults: "Vivid" look (ACES + SSAO + bloom + saturation). F6 resets.
	struct GradingParams {
		// vec4 #0 — post-process amounts, 0 = disabled.
		float ssao     = 0.55f;  // AO darken (0..1)
		float bloom    = 0.45f;  // bloom add (0..1)
		float vignette = 0.35f;  // edge darken (0..0.5)
		float aces     = 1.0f;   // ACES tonemap (0=skip, 1=apply)

		// vec4 #1 — baseline saturation boost.
		float exposure    = 1.05f;  // pre-ACES (0.3..1.5, 1=neutral)
		float warmTint    = 0.30f;  // warm WB (0..1)
		float sCurve      = 0.03f;  // black-crush (0..0.2)
		float saturation  = 0.45f;  // delta (-1..+1, 0=neutral)
	};
	static_assert(sizeof(GradingParams) == 32, "GradingParams must be 32 bytes");

	// Fullscreen sky: VS reconstructs ray dir from invVP. VK ignores
	// skyColor/horizonColor (hardcoded in shader); GL uses all.
	virtual void drawSky(const float invVP[16],
	                     const float skyColor[3],
	                     const float horizonColor[3],
	                     const float sunDir[3],
	                     float sunStrength,
	                     float time) = 0;

	// Call BEFORE endFrame (UBO read at swapchain-pass start). GL: no-op.
	virtual void setGrading(const GradingParams& g) = 0;

	// Call after imguiRender, before endFrame.
	virtual bool screenshot(const char* path) = 0;

	// Re-uploads instances per call. 6 floats/instance {pos,rgb}.
	virtual void drawVoxels(const SceneParams& scene,
	                        const float* instances,
	                        uint32_t instanceCount) = 0;

	// Persistent mesh handles. destroyMesh defers GPU release until any
	// in-flight frame using the mesh has retired.
	using MeshHandle = uint64_t;
	static constexpr MeshHandle kInvalidMesh = 0;

	virtual MeshHandle createVoxelMesh(const float* instances,
	                                   uint32_t instanceCount) = 0;
	// Handle stays valid across grow — chunk meshers don't churn handles
	// on block break/place.
	virtual void       updateVoxelMesh(MeshHandle mesh,
	                                   const float* instances,
	                                   uint32_t instanceCount) = 0;
	virtual void       destroyMesh(MeshHandle mesh) = 0;
	virtual void       drawVoxelsMesh(const SceneParams& scene,
	                                  MeshHandle mesh) = 0;
	virtual void       renderShadowsMesh(const float sunVP[16],
	                                     MeshHandle mesh) = 0;

	// Chunk meshes — 13 floats/vertex, no instancing. Caller splits
	// opaque/transparent and sorts transparent back-to-front.
	// Opaque: depth test+write, no blend, cull back.
	// Transparent: depth test, no write, alpha blend, no cull.
	// Layout: [0..2] pos, [3..5] rgb, [6..8] normal, [9] ao, [10] shade,
	//         [11] alpha (<1 → transparent), [12] glow (1 → arcane anim).
	virtual MeshHandle createChunkMesh(const float* verts,
	                                   uint32_t vertexCount) = 0;
	virtual void       updateChunkMesh(MeshHandle mesh,
	                                   const float* verts,
	                                   uint32_t vertexCount) = 0;
	virtual void       drawChunkMeshOpaque(const SceneParams& scene,
	                                       const float fogColor[3],
	                                       float fogStart, float fogEnd,
	                                       MeshHandle mesh) = 0;
	virtual void       drawChunkMeshTransparent(const SceneParams& scene,
	                                            const float fogColor[3],
	                                            float fogStart, float fogEnd,
	                                            MeshHandle mesh) = 0;
	// AFTER beginFrame, BEFORE first lit draw. Shares shadow map with
	// renderShadows/renderBoxShadows.
	virtual void       renderShadowsChunkMesh(const float sunVP[16],
	                                          MeshHandle mesh) = 0;

	// 19 floats/box: mat4 model + rgb. Matrix maps unit cube [0,1]^3 to
	// oriented box (preserves per-part rotations across instanced batch).
	// Call AFTER drawVoxels; multiple calls append.
	virtual void drawBoxModel(const SceneParams& scene,
	                          const float* boxes,
	                          uint32_t boxCount) = 0;

	// AFTER beginFrame, BEFORE first drawSky/drawVoxels. sunVP = ortho light
	// VP. voxel.frag samples with PCF. GL: no-op. Backend lazily opens
	// depth-only pass on first shadow call, closes before first lit draw —
	// all shadow calls accumulate into one map.
	virtual void renderShadows(const float sunVP[16],
	                           const float* instances,
	                           uint32_t instanceCount) = 0;

	// 19-float box format (same as drawBoxModel); shares shadow map.
	virtual void renderBoxShadows(const float sunVP[16],
	                              const float* boxes,
	                              uint32_t boxCount) = 0;

	// Billboards, 8 floats/particle. Additive, depth test, no depth write.
	virtual void drawParticles(const SceneParams& scene,
	                           const float* particles,
	                           uint32_t particleCount) = 0;

	// Ribbon trail, 8 floats/point. RHI expands into ±width/2 along
	// tangent×viewDir. pointCount >= 2. Premultiplied additive.
	virtual void drawRibbon(const SceneParams& scene,
	                        const float* points,
	                        uint32_t pointCount) = 0;

	// NDC (+y up; VK flips Y internally). 4 floats/vertex {pos.xy, uv.xy}.
	// Backend lazily enters swapchain pass (idempotent with imguiNewFrame).
	// mode: 0=SDF text, 1=solid fill, 2=SDF title (fill+outline+glow).
	virtual void drawUi2D(const float* vertsPosUV,
	                      uint32_t vertCount,
	                      int mode,
	                      const float rgba[4]) = 0;

	// CPU-tessellation wrappers over drawUi2D (rhi_ui.cpp).
	// scale 1.0 ≈ 24 px at 900p.
	void drawText2D(const char* text, float x, float y, float scale,
	                const float rgba[4]);
	void drawTitle2D(const char* text, float x, float y, float scale,
	                 const float rgba[4]);
	void drawRect2D(float x, float y, float w, float h, const float rgba[4]);
	// r_inner=0 → filled disk. aspect keeps circles round.
	void drawArc2D(float cx, float cy, float r_inner, float r_outer,
	               float startRad, float endRad, const float rgba[4],
	               float aspect, int segments = 40);
};

// One backend compiled per binary.
IRhi* createRhi();

} // namespace civcraft::rhi
