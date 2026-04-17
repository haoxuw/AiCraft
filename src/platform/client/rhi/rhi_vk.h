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
	void setGrading(const GradingParams& g) override;
	bool screenshot(const char* path) override;
	void drawSky(const float invVP[16],
	             const float skyColor[3],
	             const float horizonColor[3],
	             const float sunDir[3],
	             float sunStrength,
	             float time) override;
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
	void       renderShadowsChunkMesh(const float sunVP[16], MeshHandle mesh) override;

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
	bool createChunkShadowPipeline();
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

	// Shared across swapchain images; recreated on resize.
	VkImage m_depthImage = VK_NULL_HANDLE;
	VkDeviceMemory m_depthMem = VK_NULL_HANDLE;
	VkImageView m_depthView = VK_NULL_HANDLE;
	VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;

	VkPipelineLayout m_cubeLayout = VK_NULL_HANDLE;
	VkPipeline m_cubePipeline = VK_NULL_HANDLE;
	VkBuffer m_cubeVbo = VK_NULL_HANDLE;
	VkDeviceMemory m_cubeVboMem = VK_NULL_HANDLE;
	uint32_t m_cubeVertCount = 0;

	VkPipelineLayout m_voxelLayout = VK_NULL_HANDLE;
	VkPipeline m_voxelPipeline = VK_NULL_HANDLE;
	VkBuffer m_instBuf = VK_NULL_HANDLE;
	VkDeviceMemory m_instMem = VK_NULL_HANDLE;
	VkDeviceSize m_instCap = 0;

	// Shares m_voxelLayout (same PC + descriptor — samples shadow map).
	// Per-frame buffers so multiple drawBoxModel calls append safely.
	VkPipeline m_boxModelPipeline = VK_NULL_HANDLE;
	VkBuffer m_boxInstBuf[kFramesInFlight]{};
	VkDeviceMemory m_boxInstMem[kFramesInFlight]{};
	void* m_boxInstMapped[kFramesInFlight]{};
	VkDeviceSize m_boxInstCap[kFramesInFlight]{};
	uint32_t m_boxInstCount[kFramesInFlight]{};

	// Per-frame-in-flight depth map — prevents frame N+1 shadow write
	// aliasing frame N shadow read (frames overlap on-GPU).
	static constexpr uint32_t kShadowRes = 2048;
	VkRenderPass m_shadowRenderPass = VK_NULL_HANDLE;
	VkImage m_shadowImage[kFramesInFlight]{};
	VkDeviceMemory m_shadowMem[kFramesInFlight]{};
	VkImageView m_shadowView[kFramesInFlight]{};
	VkFramebuffer m_shadowFB[kFramesInFlight]{};
	VkSampler m_shadowSampler = VK_NULL_HANDLE;
	VkPipelineLayout m_shadowLayout = VK_NULL_HANDLE;
	VkPipeline m_shadowPipeline = VK_NULL_HANDLE;
	// Box-model shadow — 19 floats/instance vs voxel's 6, so separate
	// pipeline + per-frame buffer. Shares m_shadowLayout + render pass.
	VkPipeline m_boxShadowPipeline = VK_NULL_HANDLE;
	VkBuffer m_boxShadowInstBuf[kFramesInFlight]{};
	VkDeviceMemory m_boxShadowInstMem[kFramesInFlight]{};
	void* m_boxShadowInstMapped[kFramesInFlight]{};
	VkDeviceSize m_boxShadowInstCap[kFramesInFlight]{};
	// Chunk-mesh shadow — reads pos from 13-float stream. Shares
	// m_shadowLayout + render pass so all three accumulate into one map.
	VkPipeline m_chunkShadowPipeline = VK_NULL_HANDLE;

	// Voxel pipeline set=0: binding 0 = shadow map (combined sampler),
	// binding 1 = UBO { mat4 shadowVP; vec4 shadowParams }.
	VkDescriptorSetLayout m_voxelSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_voxelDescPool = VK_NULL_HANDLE;
	VkDescriptorSet m_voxelDescSet[kFramesInFlight]{};
	VkBuffer m_shadowUbo[kFramesInFlight]{};
	VkDeviceMemory m_shadowUboMem[kFramesInFlight]{};
	void* m_shadowUboMapped[kFramesInFlight]{};

	// Lazy main-pass: first draw opens the offscreen pass, letting a
	// shadow pass cleanly precede it.
	bool m_mainPassActive = false;
	bool m_shadowValid[kFramesInFlight]{};

	// Lazy shadow-pass — all shadow calls share one pass, closed when a
	// main-pass draw forces it closed.
	bool m_shadowPassActive = false;

	// Sky (fullscreen triangle, no VBO).
	VkPipelineLayout m_skyLayout = VK_NULL_HANDLE;
	VkPipeline m_skyPipeline = VK_NULL_HANDLE;

	// Particles — additive billboards. Per-frame append buffer.
	VkPipelineLayout m_particleLayout = VK_NULL_HANDLE;
	VkPipeline m_particlePipeline = VK_NULL_HANDLE;
	VkBuffer m_particleInstBuf[kFramesInFlight]{};
	VkDeviceMemory m_particleInstMem[kFramesInFlight]{};
	void* m_particleInstMapped[kFramesInFlight]{};
	VkDeviceSize m_particleInstCap[kFramesInFlight]{};
	uint32_t m_particleInstCount[kFramesInFlight]{};

	// Ribbon trails (sword slashes, magic bolts). Byte cursor so multiple
	// calls per frame bind the same buffer at their own offsets.
	VkPipelineLayout m_ribbonLayout = VK_NULL_HANDLE;
	VkPipeline m_ribbonPipeline = VK_NULL_HANDLE;
	VkBuffer m_ribbonVtxBuf[kFramesInFlight]{};
	VkDeviceMemory m_ribbonVtxMem[kFramesInFlight]{};
	void* m_ribbonVtxMapped[kFramesInFlight]{};
	VkDeviceSize m_ribbonVtxCap[kFramesInFlight]{};
	VkDeviceSize m_ribbonVtxCursor[kFramesInFlight]{};  // bytes written this frame

	// 2D UI / text. 512×192 R8 SDF atlas bound once. Per-frame cursor
	// buffer (mirrors ribbon pattern). PC: { vec4 color; ivec4 mode }.
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
	// Deferred-destroy for grow-replaced UI buffers — the old buffer may
	// still be bound by earlier drawUi2D in this frame's cmdbuf. Drain at
	// top of next beginFrame for the same slot (after fence wait).
	struct UiVtxPending {
		VkBuffer buf;
		VkDeviceMemory mem;
	};
	std::vector<UiVtxPending> m_uiVtxPending[kFramesInFlight];

	// beginSwapchainPass() is idempotent — imguiNewFrame + drawUi2D both
	// call it; first call does offscreen→swapchain + composite quad.
	bool m_swapchainPassActive = false;

	// Offscreen targets (per-frame double-buffered).
	VkRenderPass m_offRenderPass = VK_NULL_HANDLE;
	VkImage m_offColor[kFramesInFlight]{};
	VkDeviceMemory m_offColorMem[kFramesInFlight]{};
	VkImageView m_offColorView[kFramesInFlight]{};
	VkImage m_offDepth[kFramesInFlight]{};
	VkDeviceMemory m_offDepthMem[kFramesInFlight]{};
	VkImageView m_offDepthView[kFramesInFlight]{};
	VkFramebuffer m_offFB[kFramesInFlight]{};

	// Composite (post-process).
	VkSampler m_linearSampler = VK_NULL_HANDLE;
	VkSampler m_nearestSampler = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_compSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_compDescPool = VK_NULL_HANDLE;
	VkDescriptorSet m_compDescSet[kFramesInFlight]{};
	VkPipelineLayout m_compLayout = VK_NULL_HANDLE;
	VkPipeline m_compPipeline = VK_NULL_HANDLE;
	struct CompPC { float invVP[16]; float vp[16]; };
	CompPC m_compPC{};

	// Grading UBO — mirrors GradingParams (32B). Composite reads at
	// set=0 binding=2. Written per frame in setGrading().
	VkBuffer m_gradUbo[kFramesInFlight]{};
	VkDeviceMemory m_gradUboMem[kFramesInFlight]{};
	void* m_gradUboMapped[kFramesInFlight]{};
	GradingParams m_grading{};

	// Persistent voxel meshes — monotonic id keys (outlive map rehashes).
	// updateVoxelMesh in-place if capBytes fits; only growth reallocates.
	struct PersistentMesh {
		VkBuffer       buf      = VK_NULL_HANDLE;
		VkDeviceMemory mem      = VK_NULL_HANDLE;
		uint32_t       instCount = 0;
		VkDeviceSize   capBytes  = 0;
	};
	std::unordered_map<uint64_t, PersistentMesh> m_meshes;
	uint64_t m_nextMeshId = 1;
	// destroyMesh enqueues here; drained in beginFrame after fence wait.
	std::vector<PersistentMesh> m_meshPending[kFramesInFlight];

	// Chunk meshes — separate map (52B vs 24B stride; pipelines must not
	// see each other's buffers). Shares ID space with m_meshes; destroyMesh
	// checks both. Reuses PersistentMesh: instCount = vertexCount.
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
