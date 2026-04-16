#pragma once

#include "rhi.h"

#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>

#include <unordered_map>
#include <vector>

namespace civcraft::rhi {

class VkRhi : public IRhi {
public:
	bool init(const InitInfo& info) override;
	void shutdown() override;
	void onResize(int width, int height) override;
	bool beginFrame() override;
	void endFrame() override;
	bool initImGui() override;
	void shutdownImGui() override;
	void imguiNewFrame() override;
	void imguiRender() override;
	void drawCube(const float mvp[16]) override;
	bool screenshot(const char* path) override;
	void drawSky(const float invVP[16],
	             const float sunDir[3], float sunStr) override;
	void drawVoxels(const SceneParams& scene,
	                const float* instances,
	                uint32_t instanceCount) override;
	void drawBoxModel(const SceneParams& scene,
	                  const float* boxes,
	                  uint32_t boxCount) override;
	void renderShadows(const float sunVP[16],
	                   const float* instances,
	                   uint32_t instanceCount) override;
	void renderBoxShadows(const float sunVP[16],
	                      const float* boxes,
	                      uint32_t boxCount) override;
	void drawParticles(const SceneParams& scene,
	                   const float* particles,
	                   uint32_t particleCount) override;
	void drawRibbon(const SceneParams& scene,
	                const float* points,
	                uint32_t pointCount) override;
	void drawUi2D(const float* vertsPosUV,
	              uint32_t vertCount,
	              int mode,
	              const float rgba[4]) override;

	MeshHandle createVoxelMesh(const float* instances,
	                           uint32_t instanceCount) override;
	void       updateVoxelMesh(MeshHandle mesh,
	                           const float* instances,
	                           uint32_t instanceCount) override;
	void       destroyMesh(MeshHandle mesh) override;
	void       drawVoxelsMesh(const SceneParams& scene, MeshHandle mesh) override;
	void       renderShadowsMesh(const float sunVP[16], MeshHandle mesh) override;

	MeshHandle createChunkMesh(const float* verts,
	                           uint32_t vertexCount) override;
	void       updateChunkMesh(MeshHandle mesh,
	                           const float* verts,
	                           uint32_t vertexCount) override;
	void       drawChunkMeshOpaque(const SceneParams& scene,
	                                const float fogColor[3],
	                                float fogStart, float fogEnd,
	                                MeshHandle mesh) override;
	void       drawChunkMeshTransparent(const SceneParams& scene,
	                                     const float fogColor[3],
	                                     float fogStart, float fogEnd,
	                                     MeshHandle mesh) override;

private:
	bool createInstance(const char* appName);
	bool pickPhysicalDevice();
	bool createSurface();
	bool createDevice();
	bool createSwapchain();
	bool createRenderPass();
	bool createFramebuffers();
	bool createCommandPool();
	bool createSyncObjects();
	bool createDepth();
	bool createCubePipeline();
	bool createCubeBuffers();
	bool createVoxelPipeline();
	bool createSkyPipeline();
	bool createBoxModelPipeline();
	bool createChunkPipelines();
	bool ensureInstanceCapacity(VkDeviceSize bytes);
	bool ensureBoxInstanceCapacity(int frame, VkDeviceSize bytes);
	uint32_t findMemType(uint32_t typeBits, VkMemoryPropertyFlags props);
	bool createOffscreenRenderPass();
	bool createOffscreen();
	void destroyOffscreen();
	bool createCompositeResources();
	void updateCompositeDescriptors();
	bool createShadowResources();
	bool createShadowPipeline();
	bool createBoxShadowPipeline();
	void destroyShadowResources();
	void ensureMainPass();
	void ensureShadowPass();
	void endShadowPassIfActive();
	bool ensureBoxShadowInstanceCapacity(int frame, VkDeviceSize bytes);
	bool createParticlePipeline();
	bool ensureParticleInstanceCapacity(int frame, VkDeviceSize bytes);
	bool createRibbonPipeline();
	bool ensureRibbonVertexCapacity(int frame, VkDeviceSize bytes);
	bool createUi2DResources();
	bool uploadFontAtlas();
	bool ensureUi2DVertexCapacity(int frame, VkDeviceSize bytes);
	void beginSwapchainPass();

	void destroySwapchain();
	void destroyDepth();
	bool recreateSwapchain();

	GLFWwindow* m_window = nullptr;
	int m_width = 0, m_height = 0;
	bool m_validation = false;

	VkInstance m_instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_dbg = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkPhysicalDevice m_phys = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	uint32_t m_gfxQueueFamily = 0;
	VkQueue m_gfxQueue = VK_NULL_HANDLE;

	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkFormat m_swapFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D m_swapExtent{};
	std::vector<VkImage> m_swapImages;
	std::vector<VkImageView> m_swapViews;
	std::vector<VkFramebuffer> m_framebuffers;

	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkCommandPool m_cmdPool = VK_NULL_HANDLE;

	static constexpr int kFramesInFlight = 2;
	std::vector<VkCommandBuffer> m_cmdBufs;
	std::vector<VkSemaphore> m_imgAvail;
	std::vector<VkSemaphore> m_renderDone;
	std::vector<VkFence> m_inFlight;

	uint32_t m_frame = 0;
	uint32_t m_imageIndex = 0;
	bool m_frameActive = false;
	bool m_resizePending = false;

	// Depth buffer (shared across swapchain images — recreated on resize).
	VkImage m_depthImage = VK_NULL_HANDLE;
	VkDeviceMemory m_depthMem = VK_NULL_HANDLE;
	VkImageView m_depthView = VK_NULL_HANDLE;
	VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;

	// Cube pipeline.
	VkPipelineLayout m_cubeLayout = VK_NULL_HANDLE;
	VkPipeline m_cubePipeline = VK_NULL_HANDLE;
	VkBuffer m_cubeVbo = VK_NULL_HANDLE;
	VkDeviceMemory m_cubeVboMem = VK_NULL_HANDLE;
	uint32_t m_cubeVertCount = 0;

	// Voxel (instanced) pipeline.
	VkPipelineLayout m_voxelLayout = VK_NULL_HANDLE;
	VkPipeline m_voxelPipeline = VK_NULL_HANDLE;
	VkBuffer m_instBuf = VK_NULL_HANDLE;
	VkDeviceMemory m_instMem = VK_NULL_HANDLE;
	VkDeviceSize m_instCap = 0;

	// Box-model pipeline. Shares m_voxelLayout (same push constant +
	// descriptor set — it samples the shadow map like voxels do). Instance
	// format is {worldPos[3], size[3], color[3]} = 9 floats per box. Separate
	// per-frame-in-flight instance buffer so multiple drawBoxModel calls in
	// one frame can append without overwriting each other.
	VkPipeline m_boxModelPipeline = VK_NULL_HANDLE;
	VkBuffer m_boxInstBuf[kFramesInFlight]{};
	VkDeviceMemory m_boxInstMem[kFramesInFlight]{};
	void* m_boxInstMapped[kFramesInFlight]{};
	VkDeviceSize m_boxInstCap[kFramesInFlight]{};
	uint32_t m_boxInstCount[kFramesInFlight]{};  // # of instances appended this frame

	// Shadow mapping. Single depth map per frame-in-flight (frames overlap
	// on-GPU, so the second frame's shadow write must not alias the first
	// frame's shadow read). Same pipeline regardless of frame — binds the
	// per-frame framebuffer.
	static constexpr uint32_t kShadowRes = 2048;
	VkRenderPass m_shadowRenderPass = VK_NULL_HANDLE;
	VkImage m_shadowImage[kFramesInFlight]{};
	VkDeviceMemory m_shadowMem[kFramesInFlight]{};
	VkImageView m_shadowView[kFramesInFlight]{};
	VkFramebuffer m_shadowFB[kFramesInFlight]{};
	VkSampler m_shadowSampler = VK_NULL_HANDLE;
	VkPipelineLayout m_shadowLayout = VK_NULL_HANDLE;
	VkPipeline m_shadowPipeline = VK_NULL_HANDLE;
	// Box-model shadow pipeline. Separate pipeline (different vertex input
	// format — 9 floats per instance instead of 6), shares m_shadowLayout
	// and m_shadowRenderPass with the voxel shadow pipeline. Per-frame
	// instance buffer so voxel + box shadow uploads don't collide.
	VkPipeline m_boxShadowPipeline = VK_NULL_HANDLE;
	VkBuffer m_boxShadowInstBuf[kFramesInFlight]{};
	VkDeviceMemory m_boxShadowInstMem[kFramesInFlight]{};
	void* m_boxShadowInstMapped[kFramesInFlight]{};
	VkDeviceSize m_boxShadowInstCap[kFramesInFlight]{};

	// Per-frame descriptor set bound to the voxel pipeline at set=0.
	//   binding 0: combined image sampler → shadow map
	//   binding 1: UBO → { mat4 shadowVP; vec4 shadowParams; }
	VkDescriptorSetLayout m_voxelSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_voxelDescPool = VK_NULL_HANDLE;
	VkDescriptorSet m_voxelDescSet[kFramesInFlight]{};
	VkBuffer m_shadowUbo[kFramesInFlight]{};
	VkDeviceMemory m_shadowUboMem[kFramesInFlight]{};
	void* m_shadowUboMapped[kFramesInFlight]{};

	// Lazy main-pass tracking. beginFrame no longer begins the offscreen
	// pass; the first draw call of a frame begins it so a shadow pass can
	// precede it cleanly.
	bool m_mainPassActive = false;
	bool m_shadowValid[kFramesInFlight]{};  // true once frame has shadow data

	// Lazy shadow-pass tracking. renderShadows + renderBoxShadows share a
	// single pass (clear on first open, load on subsequent opens… actually
	// we keep it simpler: the first call starts and subsequent calls append
	// because the pass stays open until any main-pass draw forces it closed).
	bool m_shadowPassActive = false;

	// Sky pipeline (fullscreen triangle, no VBO).
	VkPipelineLayout m_skyLayout = VK_NULL_HANDLE;
	VkPipeline m_skyPipeline = VK_NULL_HANDLE;

	// Particle pipeline. Additive-blended billboards. Per-frame instance
	// buffer so multiple drawParticles calls in one frame append without
	// overwriting (same pattern as box model).
	VkPipelineLayout m_particleLayout = VK_NULL_HANDLE;
	VkPipeline m_particlePipeline = VK_NULL_HANDLE;
	VkBuffer m_particleInstBuf[kFramesInFlight]{};
	VkDeviceMemory m_particleInstMem[kFramesInFlight]{};
	void* m_particleInstMapped[kFramesInFlight]{};
	VkDeviceSize m_particleInstCap[kFramesInFlight]{};
	uint32_t m_particleInstCount[kFramesInFlight]{};

	// Ribbon pipeline. Additive trails (sword slashes, magic bolts). Each
	// drawRibbon call expands control points into a per-frame vertex buffer
	// and submits a triangle-strip draw. Multiple calls per frame share the
	// same buffer — we track a byte cursor and each draw binds at its own
	// offset. Vertex format (post-expansion): {pos[3], rgba[4]} = 7 floats.
	VkPipelineLayout m_ribbonLayout = VK_NULL_HANDLE;
	VkPipeline m_ribbonPipeline = VK_NULL_HANDLE;
	VkBuffer m_ribbonVtxBuf[kFramesInFlight]{};
	VkDeviceMemory m_ribbonVtxMem[kFramesInFlight]{};
	void* m_ribbonVtxMapped[kFramesInFlight]{};
	VkDeviceSize m_ribbonVtxCap[kFramesInFlight]{};
	VkDeviceSize m_ribbonVtxCursor[kFramesInFlight]{};  // bytes written this frame

	// 2D UI / text pipeline. Font atlas is a 512×192 R8 SDF texture
	// generated once at init and bound via a single descriptor set. Each
	// drawUi2D appends {pos.xy, uv.xy} vertices into a per-frame buffer
	// (cursor-tracked, mirrors the ribbon pattern) and issues one draw.
	// Push constant: { vec4 color; ivec4 mode }. Depth off, alpha blend.
	VkImage m_fontImage = VK_NULL_HANDLE;
	VkDeviceMemory m_fontMem = VK_NULL_HANDLE;
	VkImageView m_fontView = VK_NULL_HANDLE;
	VkSampler m_fontSampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_uiSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_uiDescPool = VK_NULL_HANDLE;
	VkDescriptorSet m_uiDescSet = VK_NULL_HANDLE;
	VkPipelineLayout m_uiLayout = VK_NULL_HANDLE;
	VkPipeline m_uiPipeline = VK_NULL_HANDLE;
	VkBuffer m_uiVtxBuf[kFramesInFlight]{};
	VkDeviceMemory m_uiVtxMem[kFramesInFlight]{};
	void* m_uiVtxMapped[kFramesInFlight]{};
	VkDeviceSize m_uiVtxCap[kFramesInFlight]{};
	VkDeviceSize m_uiVtxCursor[kFramesInFlight]{};
	// Deferred-destroy queue for UI vertex buffers that got replaced (grow).
	// When ensureUi2DVertexCapacity reallocates mid-frame, the old buffer may
	// still be bound by this frame's command buffer from an earlier drawUi2D
	// call. We can't destroy it until the GPU has finished the frame, so we
	// hand it to this queue keyed by frame index and drain at the TOP of the
	// next beginFrame for the same index (after the fence wait, where the
	// buffer is guaranteed idle).
	struct UiVtxPending {
		VkBuffer buf;
		VkDeviceMemory mem;
	};
	std::vector<UiVtxPending> m_uiVtxPending[kFramesInFlight];

	// Swapchain-pass tracking. beginSwapchainPass() is idempotent — both
	// imguiNewFrame and drawUi2D call it, and the first call does the
	// offscreen→swapchain transition + composite quad.
	bool m_swapchainPassActive = false;

	// Offscreen targets (per-frame for double buffering).
	VkRenderPass m_offRenderPass = VK_NULL_HANDLE;
	VkImage m_offColor[kFramesInFlight]{};
	VkDeviceMemory m_offColorMem[kFramesInFlight]{};
	VkImageView m_offColorView[kFramesInFlight]{};
	VkImage m_offDepth[kFramesInFlight]{};
	VkDeviceMemory m_offDepthMem[kFramesInFlight]{};
	VkImageView m_offDepthView[kFramesInFlight]{};
	VkFramebuffer m_offFB[kFramesInFlight]{};

	// Composite (post-process) pipeline.
	VkSampler m_linearSampler = VK_NULL_HANDLE;
	VkSampler m_nearestSampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_compSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_compDescPool = VK_NULL_HANDLE;
	VkDescriptorSet m_compDescSet[kFramesInFlight]{};
	VkPipelineLayout m_compLayout = VK_NULL_HANDLE;
	VkPipeline m_compPipeline = VK_NULL_HANDLE;
	struct CompPC { float invVP[16]; float vp[16]; };
	CompPC m_compPC{};

	// Persistent voxel meshes (createVoxelMesh / drawVoxelsMesh /
	// renderShadowsMesh). The static playable-slice village uploads once
	// here instead of re-streaming 6 floats per voxel every frame; future
	// chunked terrain (chunk_mesher port) uses the same path. Keyed by
	// monotonic id so handles can outlive map rehashes.
	struct PersistentMesh {
		VkBuffer       buf      = VK_NULL_HANDLE;
		VkDeviceMemory mem      = VK_NULL_HANDLE;
		uint32_t       instCount = 0;
		// Allocated capacity in bytes. Lets updateVoxelMesh skip the realloc
		// path when the new data still fits — chunk re-meshes are common
		// and most shrink-or-stay-same; only growth needs a new buffer.
		VkDeviceSize   capBytes  = 0;
	};
	std::unordered_map<uint64_t, PersistentMesh> m_meshes;
	uint64_t m_nextMeshId = 1;
	// destroyMesh moves entries here, drained per-frame in beginFrame after
	// the fence wait so the buffer is guaranteed idle on the GPU.
	std::vector<PersistentMesh> m_meshPending[kFramesInFlight];

	// Chunk meshes (rich per-vertex 13-float format, no instancing). Separate
	// map from m_meshes because the stride differs (52 vs 24 bytes), so the
	// two pipelines must not see each other's buffers. Same defer-destroy
	// pattern. Handles share the kInvalidMesh sentinel + uint64 ID space —
	// destroyMesh checks both maps. PersistentMesh is reused: `instCount`
	// here means "vertexCount", `capBytes` is buffer capacity in bytes.
	VkPipelineLayout m_chunkLayout = VK_NULL_HANDLE;
	VkPipeline m_chunkPipelineOpaque = VK_NULL_HANDLE;
	VkPipeline m_chunkPipelineTransparent = VK_NULL_HANDLE;
	std::unordered_map<uint64_t, PersistentMesh> m_chunkMeshes;
	std::vector<PersistentMesh> m_chunkMeshPending[kFramesInFlight];

	// ImGui
	VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
	bool m_imguiInited = false;
};

} // namespace civcraft::rhi
