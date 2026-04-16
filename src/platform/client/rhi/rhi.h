#pragma once

// Render Hardware Interface — minimal abstraction so the engine can target
// either OpenGL (legacy/web) or Vulkan (native, new). Phase 0 surface area:
// just enough to open a window, clear, and run ImGui.

// Forward-declare GLFWwindow so rhi.h doesn't pull in backend headers.
struct GLFWwindow;

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

	// One-shot per frame. beginFrame returns false if the frame should be
	// skipped (e.g. swapchain out of date — caller continues to next loop).
	virtual bool beginFrame() = 0;
	virtual void endFrame() = 0;

	// Backend-specific ImGui setup. For GL backend: imgui_impl_opengl3_*.
	// For VK backend: imgui_impl_vulkan_* + descriptor pool + render pass.
	virtual bool initImGui() = 0;
	virtual void shutdownImGui() = 0;
	virtual void imguiNewFrame() = 0;
	// Records ImGui draw data into the active command buffer (VK) or
	// flushes immediately (GL). Call between beginFrame and endFrame.
	virtual void imguiRender() = 0;

	// Phase 2 demo: draw a unit cube with the supplied MVP. Records into
	// the active frame's command buffer; call between beginFrame and
	// imguiRender / endFrame.
	virtual void drawCube(const float mvp[16]) = 0;

	// Phase 2.1: instanced voxel rendering. `instances` packs
	// {posX, posY, posZ, r, g, b} per instance (6 floats). Re-uploads the
	// instance buffer every call (fine for small terrains; chunk mesher
	// will own its own persistent buffers later).
	struct SceneParams {
		float viewProj[16];
		float camPos[3]; float time;    // camPos xyz + elapsed time
		float sunDir[3]; float sunStr;  // sun direction xyz + strength 0..1
	};

	virtual void drawSky(const float invVP[16],
	                     const float sunDir[3], float sunStr) = 0;

	// Write the current frame to a PPM file. Call after imguiRender, before
	// endFrame. Returns true on success.
	virtual bool screenshot(const char* path) = 0;

	virtual void drawVoxels(const SceneParams& scene,
	                        const float* instances,
	                        uint32_t instanceCount) = 0;

	// Box-model rendering. `boxes` packs {worldPosX, worldPosY, worldPosZ,
	// sizeX, sizeY, sizeZ, r, g, b} per box (9 floats). Designed for entity
	// meshes: each box is an axis-aligned box in world space. Shares lighting
	// + shadow infrastructure with drawVoxels — boxes sample the shadow map
	// but do NOT cast shadows in this pipeline iteration (TODO: add a
	// box-model shadow pass). Must be called AFTER drawVoxels in the frame.
	// Multiple calls per frame are allowed and append to a per-frame buffer.
	virtual void drawBoxModel(const SceneParams& scene,
	                          const float* boxes,
	                          uint32_t boxCount) = 0;

	// Shadow pass — must run AFTER beginFrame and BEFORE the first
	// drawSky/drawVoxels of the frame. Renders the same instance data into
	// the shadow depth map from the sun's viewpoint; drawVoxels later
	// samples it with PCF. `sunVP` is the light's view-projection matrix
	// (ortho for a directional sun). No-op on backends that don't support
	// shadow maps (currently: GL stub).
	//
	// Multiple shadow-producing calls (renderShadows + renderBoxShadows) in
	// a single frame accumulate into the same shadow map — the backend
	// opens the depth-only render pass lazily on the first call and closes
	// it before the first lit-scene draw.
	virtual void renderShadows(const float sunVP[16],
	                           const float* instances,
	                           uint32_t instanceCount) = 0;

	// Box-model shadow pass. Same contract as renderShadows but consumes
	// the 9-float-per-instance box format (matches drawBoxModel). Must run
	// AFTER beginFrame and BEFORE the first drawSky/drawVoxels/drawBoxModel
	// of the frame. Appends to the same shadow depth map as renderShadows
	// so characters cast onto terrain and vice versa.
	virtual void renderBoxShadows(const float sunVP[16],
	                              const float* boxes,
	                              uint32_t boxCount) = 0;

	// Billboarded particle rendering. One instance per particle; the vertex
	// shader emits a 4-vertex screen-aligned quad. Format per particle:
	// {worldPos[3], size, color[4]} = 8 floats. Additive blending + depth
	// test (no depth write) so bloom picks up bright particles without
	// breaking terrain depth sorting. Must run AFTER the first
	// drawVoxels/drawBoxModel of the frame so depth is populated; multiple
	// calls per frame append into a per-frame buffer.
	virtual void drawParticles(const SceneParams& scene,
	                           const float* particles,
	                           uint32_t particleCount) = 0;

	// Camera-aligned ribbon trail — the modern replacement for
	// particle-cloud sword-slashes, magical bolts, motion trails. Input is
	// a polyline of control points in world space; the RHI expands each
	// point into two vertices offset ±width/2 along the side vector
	// (tangent × viewDir from camPos) so the ribbon faces the camera while
	// following the curve's shape. Point format: {worldPos[3], width,
	// rgba[4]} = 8 floats per point. Premultiplied additive blend + depth
	// test (no depth write), same post-process compatibility as particles.
	// pointCount must be >= 2. Call AFTER drawVoxels/drawBoxModel so depth
	// is populated; multiple calls per frame are allowed and each produces
	// one draw with its own control-point list.
	virtual void drawRibbon(const SceneParams& scene,
	                        const float* points,
	                        uint32_t pointCount) = 0;

	// 2D UI / text overlay. Low-level primitive: a triangle list in NDC
	// (+y up, matching the OpenGL convention — the Vulkan backend flips Y
	// internally). Vertex format: {pos.xy, uv.xy} = 4 floats per vertex.
	// Implicitly happens on top of the post-processed frame — the backend
	// transitions to the swapchain pass on the first call (idempotent with
	// imguiNewFrame's own transition). Blending: standard alpha.
	//
	// mode selects the fragment shader branch:
	//   0 = SDF text (samples font atlas, smooth AA edges)
	//   1 = solid fill (ignores UV, uses rgba directly)
	//   2 = SDF title (fill + dark outline + soft glow, for menu headers)
	virtual void drawUi2D(const float* vertsPosUV,
	                      uint32_t vertCount,
	                      int mode,
	                      const float rgba[4]) = 0;

	// Convenience wrappers around drawUi2D with CPU tessellation. All in
	// NDC, +y up. scale: 1.0 ≈ 24 px on a 900p screen. Non-virtual so the
	// tessellation lives in one place (rhi_ui.cpp).
	void drawText2D(const char* text, float x, float y, float scale,
	                const float rgba[4]);
	void drawTitle2D(const char* text, float x, float y, float scale,
	                 const float rgba[4]);
	void drawRect2D(float x, float y, float w, float h, const float rgba[4]);
	// Filled arc/ring segment. r_inner = 0 → filled disk. aspect corrects
	// for non-square screens so circles stay round.
	void drawArc2D(float cx, float cy, float r_inner, float r_outer,
	               float startRad, float endRad, const float rgba[4],
	               float aspect, int segments = 40);
};

// Backend factory. Exactly one is compiled in per binary.
IRhi* createRhi();

} // namespace civcraft::rhi
