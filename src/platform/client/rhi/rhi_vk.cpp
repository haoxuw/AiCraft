#include "rhi_vk.h"
#include "ui_font_8x8.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <vector>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace civcraft::rhi {

namespace {

const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

VKAPI_ATTR VkBool32 VKAPI_CALL debugCb(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT,
		const VkDebugUtilsMessengerCallbackDataEXT* data,
		void*) {
	if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		fprintf(stderr, "[vk] %s\n", data->pMessage);
	}
	return VK_FALSE;
}

bool hasLayer(const char* name) {
	uint32_t n = 0;
	vkEnumerateInstanceLayerProperties(&n, nullptr);
	std::vector<VkLayerProperties> props(n);
	vkEnumerateInstanceLayerProperties(&n, props.data());
	for (auto& p : props) if (strcmp(p.layerName, name) == 0) return true;
	return false;
}

} // namespace

IRhi* createRhi() { return new VkRhi(); }

bool VkRhi::init(const InitInfo& info) {
	m_window = info.window;
	m_width = info.width;
	m_height = info.height;
	m_validation = info.enableValidation && hasLayer(kValidationLayer);

	if (!createInstance(info.appName)) return false;
	if (!createSurface()) return false;
	if (!pickPhysicalDevice()) return false;
	if (!createDevice()) return false;
	if (!createSwapchain()) return false;
	if (!createDepth()) return false;
	if (!createRenderPass()) return false;
	if (!createFramebuffers()) return false;
	if (!createCommandPool()) return false;
	if (!createSyncObjects()) return false;
	if (!createCubeBuffers()) return false;
	if (!createCubePipeline()) return false;
	// Shadow resources must precede voxel pipeline — voxel layout includes
	// the voxel descriptor set (shadow sampler + shadow UBO at set=0).
	if (!createShadowResources()) return false;
	if (!createShadowPipeline()) return false;
	if (!createBoxShadowPipeline()) return false;
	if (!createVoxelPipeline()) return false;
	if (!createBoxModelPipeline()) return false;
	if (!createChunkPipelines()) return false;
	if (!createChunkShadowPipeline()) return false;
	if (!createSkyPipeline()) return false;
	if (!createParticlePipeline()) return false;
	if (!createRibbonPipeline()) return false;
	if (!createOffscreenRenderPass()) return false;
	if (!createOffscreen()) return false;
	if (!createCompositeResources()) return false;
	if (!createUi2DResources()) return false;

	printf("[vk] init ok — %ux%u, %zu swap images, validation=%d\n",
		m_swapExtent.width, m_swapExtent.height, m_swapImages.size(), (int)m_validation);
	return true;
}

bool VkRhi::createInstance(const char* appName) {
	VkApplicationInfo app{};
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = appName;
	app.applicationVersion = VK_MAKE_VERSION(0, 2, 0);
	app.pEngineName = "civcraft-rhi";
	app.engineVersion = VK_MAKE_VERSION(0, 2, 0);
	app.apiVersion = VK_API_VERSION_1_2;

	uint32_t glfwCount = 0;
	const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
	std::vector<const char*> exts(glfwExt, glfwExt + glfwCount);
	if (m_validation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	std::vector<const char*> layers;
	if (m_validation) layers.push_back(kValidationLayer);

	VkInstanceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	ci.pApplicationInfo = &app;
	ci.enabledExtensionCount = (uint32_t)exts.size();
	ci.ppEnabledExtensionNames = exts.data();
	ci.enabledLayerCount = (uint32_t)layers.size();
	ci.ppEnabledLayerNames = layers.data();

	if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS) {
		fprintf(stderr, "[vk] vkCreateInstance failed\n");
		return false;
	}

	if (m_validation) {
		auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
			vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
		if (fn) {
			VkDebugUtilsMessengerCreateInfoEXT dci{};
			dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			dci.messageSeverity =
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			dci.messageType =
				VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			dci.pfnUserCallback = debugCb;
			fn(m_instance, &dci, nullptr, &m_dbg);
		}
	}
	return true;
}

bool VkRhi::createSurface() {
	if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
		fprintf(stderr, "[vk] glfwCreateWindowSurface failed\n");
		return false;
	}
	return true;
}

bool VkRhi::pickPhysicalDevice() {
	uint32_t n = 0;
	vkEnumeratePhysicalDevices(m_instance, &n, nullptr);
	if (n == 0) { fprintf(stderr, "[vk] no GPU\n"); return false; }
	std::vector<VkPhysicalDevice> devs(n);
	vkEnumeratePhysicalDevices(m_instance, &n, devs.data());

	for (auto d : devs) {
		uint32_t qn = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
		std::vector<VkQueueFamilyProperties> qs(qn);
		vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
		for (uint32_t i = 0; i < qn; i++) {
			VkBool32 present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(d, i, m_surface, &present);
			if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
				m_phys = d;
				m_gfxQueueFamily = i;
				VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(d, &p);
				printf("[vk] picked GPU: %s (queue family %u)\n", p.deviceName, i);
				return true;
			}
		}
	}
	fprintf(stderr, "[vk] no suitable GPU\n");
	return false;
}

bool VkRhi::createDevice() {
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci{};
	qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	qci.queueFamilyIndex = m_gfxQueueFamily;
	qci.queueCount = 1;
	qci.pQueuePriorities = &prio;

	const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

	VkPhysicalDeviceFeatures feats{};
	VkDeviceCreateInfo dci{};
	dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = 1;
	dci.ppEnabledExtensionNames = devExts;
	dci.pEnabledFeatures = &feats;

	if (vkCreateDevice(m_phys, &dci, nullptr, &m_device) != VK_SUCCESS) {
		fprintf(stderr, "[vk] vkCreateDevice failed\n");
		return false;
	}
	vkGetDeviceQueue(m_device, m_gfxQueueFamily, 0, &m_gfxQueue);
	return true;
}

bool VkRhi::createSwapchain() {
	VkSurfaceCapabilitiesKHR caps{};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_phys, m_surface, &caps);

	uint32_t fn = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_phys, m_surface, &fn, nullptr);
	std::vector<VkSurfaceFormatKHR> fmts(fn);
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_phys, m_surface, &fn, fmts.data());
	VkSurfaceFormatKHR chosen = fmts[0];
	for (auto& f : fmts) {
		if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
		    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }
	}
	m_swapFormat = chosen.format;

	if (caps.currentExtent.width != UINT32_MAX) {
		m_swapExtent = caps.currentExtent;
	} else {
		m_swapExtent.width = std::clamp((uint32_t)m_width,
			caps.minImageExtent.width, caps.maxImageExtent.width);
		m_swapExtent.height = std::clamp((uint32_t)m_height,
			caps.minImageExtent.height, caps.maxImageExtent.height);
	}

	uint32_t imgCount = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;

	// Prefer MAILBOX (triple-buffered, tear-free, not bound to vblank) so we
	// can render above display refresh. Fall back to IMMEDIATE (may tear) and
	// finally FIFO (vsync, always supported). Frame rate is then clamped in
	// the main loop — see Shell::runOneFrame in main.cpp.
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	{
		uint32_t pn = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_phys, m_surface, &pn, nullptr);
		std::vector<VkPresentModeKHR> modes(pn);
		vkGetPhysicalDeviceSurfacePresentModesKHR(m_phys, m_surface, &pn, modes.data());
		bool hasMailbox = false, hasImmediate = false;
		for (auto m : modes) {
			if (m == VK_PRESENT_MODE_MAILBOX_KHR)   hasMailbox = true;
			if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) hasImmediate = true;
		}
		if      (hasMailbox)   presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		else if (hasImmediate) presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
	}

	VkSwapchainCreateInfoKHR sci{};
	sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	sci.surface = m_surface;
	sci.minImageCount = imgCount;
	sci.imageFormat = chosen.format;
	sci.imageColorSpace = chosen.colorSpace;
	sci.imageExtent = m_swapExtent;
	sci.imageArrayLayers = 1;
	sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	sci.preTransform = caps.currentTransform;
	sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	sci.presentMode = presentMode;
	sci.clipped = VK_TRUE;

	if (vkCreateSwapchainKHR(m_device, &sci, nullptr, &m_swapchain) != VK_SUCCESS) {
		fprintf(stderr, "[vk] vkCreateSwapchainKHR failed\n");
		return false;
	}

	uint32_t n = 0;
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &n, nullptr);
	m_swapImages.resize(n);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &n, m_swapImages.data());

	m_swapViews.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = m_swapImages[i];
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = m_swapFormat;
		vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vci.subresourceRange.levelCount = 1;
		vci.subresourceRange.layerCount = 1;
		if (vkCreateImageView(m_device, &vci, nullptr, &m_swapViews[i]) != VK_SUCCESS) return false;
	}
	return true;
}

bool VkRhi::createRenderPass() {
	VkAttachmentDescription color{};
	color.format = m_swapFormat;
	color.samples = VK_SAMPLE_COUNT_1_BIT;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentDescription depth{};
	depth.format = m_depthFormat;
	depth.samples = VK_SAMPLE_COUNT_1_BIT;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthRef{};
	depthRef.attachment = 1;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription sub{};
	sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sub.colorAttachmentCount = 1;
	sub.pColorAttachments = &colorRef;
	sub.pDepthStencilAttachment = &depthRef;

	VkSubpassDependency deps[2]{};
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = 0;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	VkAttachmentDescription atts[2] = { color, depth };
	VkRenderPassCreateInfo rci{};
	rci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rci.attachmentCount = 2;
	rci.pAttachments = atts;
	rci.subpassCount = 1;
	rci.pSubpasses = &sub;
	rci.dependencyCount = 2;
	rci.pDependencies = deps;

	return vkCreateRenderPass(m_device, &rci, nullptr, &m_renderPass) == VK_SUCCESS;
}

uint32_t VkRhi::findMemType(uint32_t typeBits, VkMemoryPropertyFlags props) {
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(m_phys, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
		if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
			return i;
	}
	return UINT32_MAX;
}

bool VkRhi::createDepth() {
	VkImageCreateInfo ici{};
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = m_depthFormat;
	ici.extent = { m_swapExtent.width, m_swapExtent.height, 1 };
	ici.mipLevels = 1;
	ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (vkCreateImage(m_device, &ici, nullptr, &m_depthImage) != VK_SUCCESS) return false;

	VkMemoryRequirements mr;
	vkGetImageMemoryRequirements(m_device, m_depthImage, &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &m_depthMem) != VK_SUCCESS) return false;
	vkBindImageMemory(m_device, m_depthImage, m_depthMem, 0);

	VkImageViewCreateInfo vci{};
	vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vci.image = m_depthImage;
	vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vci.format = m_depthFormat;
	vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	vci.subresourceRange.levelCount = 1;
	vci.subresourceRange.layerCount = 1;
	return vkCreateImageView(m_device, &vci, nullptr, &m_depthView) == VK_SUCCESS;
}

void VkRhi::destroyDepth() {
	if (m_depthView) { vkDestroyImageView(m_device, m_depthView, nullptr); m_depthView = VK_NULL_HANDLE; }
	if (m_depthImage) { vkDestroyImage(m_device, m_depthImage, nullptr); m_depthImage = VK_NULL_HANDLE; }
	if (m_depthMem) { vkFreeMemory(m_device, m_depthMem, nullptr); m_depthMem = VK_NULL_HANDLE; }
}

namespace {
std::vector<char> readFile(const char* path) {
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) { fprintf(stderr, "[vk] cannot open %s\n", path); return {}; }
	auto sz = (size_t)f.tellg();
	std::vector<char> buf(sz);
	f.seekg(0); f.read(buf.data(), sz);
	return buf;
}
VkShaderModule makeModule(VkDevice dev, const std::vector<char>& code) {
	VkShaderModuleCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	ci.codeSize = code.size();
	ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
	VkShaderModule m = VK_NULL_HANDLE;
	vkCreateShaderModule(dev, &ci, nullptr, &m);
	return m;
}
} // namespace

bool VkRhi::createCubeBuffers() {
	// 36 verts, 12 tris. [0,1] range so instPos+inPos = integer-aligned
	// world coords (keeps voxel shader's floor()/fract() block-edge aligned).
	struct V { float p[3]; float n[3]; };
	const float A[3] = {0,0,0}, B[3] = {1,0,0};
	const float C[3] = {1,1,0}, D[3] = {0,1,0};
	const float E[3] = {0,0,1}, F[3] = {1,0,1};
	const float G[3] = {1,1,1}, H[3] = {0,1,1};
	auto face = [](std::vector<V>& v, const float* a, const float* b, const float* c, const float* d, float nx, float ny, float nz) {
		V va{{a[0],a[1],a[2]},{nx,ny,nz}};
		V vb{{b[0],b[1],b[2]},{nx,ny,nz}};
		V vc{{c[0],c[1],c[2]},{nx,ny,nz}};
		V vd{{d[0],d[1],d[2]},{nx,ny,nz}};
		v.push_back(va); v.push_back(vb); v.push_back(vc);
		v.push_back(va); v.push_back(vc); v.push_back(vd);
	};
	std::vector<V> verts;
	face(verts, A,B,C,D,  0, 0,-1); // -Z
	face(verts, F,E,H,G,  0, 0, 1); // +Z
	face(verts, E,A,D,H, -1, 0, 0); // -X
	face(verts, B,F,G,C,  1, 0, 0); // +X
	face(verts, E,F,B,A,  0,-1, 0); // -Y
	face(verts, D,C,G,H,  0, 1, 0); // +Y
	m_cubeVertCount = (uint32_t)verts.size();

	VkDeviceSize size = sizeof(V) * verts.size();
	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = size;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &m_cubeVbo) != VK_SUCCESS) return false;

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(m_device, m_cubeVbo, &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &m_cubeVboMem) != VK_SUCCESS) return false;
	vkBindBufferMemory(m_device, m_cubeVbo, m_cubeVboMem, 0);

	void* mapped = nullptr;
	vkMapMemory(m_device, m_cubeVboMem, 0, size, 0, &mapped);
	memcpy(mapped, verts.data(), (size_t)size);
	vkUnmapMemory(m_device, m_cubeVboMem);
	return true;
}

bool VkRhi::createCubePipeline() {
	auto vsCode = readFile("shaders/vk/cube.vert.spv");
	auto fsCode = readFile("shaders/vk/cube.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs;
	stages[1].pName = "main";

	VkVertexInputBindingDescription bind{};
	bind.binding = 0;
	bind.stride = sizeof(float) * 6;
	bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	VkVertexInputAttributeDescription attrs[2]{};
	attrs[0].location = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
	attrs[1].location = 1; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = sizeof(float)*3;

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 1;
	vi.pVertexBindingDescriptions = &bind;
	vi.vertexAttributeDescriptionCount = 2;
	vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vp{};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1;
	vp.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_BACK_BIT;
	rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = VK_TRUE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendAttachmentState cba{};
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1;
	cb.pAttachments = &cba;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2;
	dynState.pDynamicStates = dyn;

	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pcr.size = sizeof(float) * 16;
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_cubeLayout) != VK_SUCCESS) return false;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2;
	gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vp;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_cubeLayout;
	gpci.renderPass = m_renderPass;
	gpci.subpass = 0;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_cubePipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

// Shadow mapping — depth-only pre-pass from sun's POV, sampled by voxel.frag.

bool VkRhi::createShadowResources() {
	// finalLayout SHADER_READ_ONLY — image is sampled as a texture next pass.
	VkAttachmentDescription att{};
	att.format = m_depthFormat;
	att.samples = VK_SAMPLE_COUNT_1_BIT;
	att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
	VkSubpassDescription sub{};
	sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sub.pDepthStencilAttachment = &depthRef;

	// Deps: wait for prior frame's frag-shader read before writing, then
	// flush writes before next frame's sampling.
	VkSubpassDependency deps[2]{};
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	VkRenderPassCreateInfo rci{};
	rci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rci.attachmentCount = 1; rci.pAttachments = &att;
	rci.subpassCount = 1; rci.pSubpasses = &sub;
	rci.dependencyCount = 2; rci.pDependencies = deps;
	if (vkCreateRenderPass(m_device, &rci, nullptr, &m_shadowRenderPass) != VK_SUCCESS)
		return false;

	// One image+view+FB per frame-in-flight.
	for (int f = 0; f < kFramesInFlight; f++) {
		VkImageCreateInfo ici{};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = m_depthFormat;
		ici.extent = { kShadowRes, kShadowRes, 1 };
		ici.mipLevels = 1; ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (vkCreateImage(m_device, &ici, nullptr, &m_shadowImage[f]) != VK_SUCCESS) return false;

		VkMemoryRequirements mr;
		vkGetImageMemoryRequirements(m_device, m_shadowImage[f], &mr);
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = mr.size;
		mai.memoryTypeIndex = findMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (vkAllocateMemory(m_device, &mai, nullptr, &m_shadowMem[f]) != VK_SUCCESS) return false;
		vkBindImageMemory(m_device, m_shadowImage[f], m_shadowMem[f], 0);

		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = m_shadowImage[f];
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = m_depthFormat;
		vci.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		if (vkCreateImageView(m_device, &vci, nullptr, &m_shadowView[f]) != VK_SUCCESS) return false;

		VkFramebufferCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fci.renderPass = m_shadowRenderPass;
		fci.attachmentCount = 1;
		fci.pAttachments = &m_shadowView[f];
		fci.width = kShadowRes; fci.height = kShadowRes; fci.layers = 1;
		if (vkCreateFramebuffer(m_device, &fci, nullptr, &m_shadowFB[f]) != VK_SUCCESS) return false;
	}

	// Linear filter; border=1.0 so out-of-map lookups read "far" (unshadowed).
	VkSamplerCreateInfo sci{};
	sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sci.magFilter = VK_FILTER_LINEAR;
	sci.minFilter = VK_FILTER_LINEAR;
	sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	if (vkCreateSampler(m_device, &sci, nullptr, &m_shadowSampler) != VK_SUCCESS)
		return false;

	// Per-frame UBO, host-visible+coherent for direct memcpy.
	struct ShadowUBO { float shadowVP[16]; float shadowParams[4]; };
	for (int f = 0; f < kFramesInFlight; f++) {
		VkBufferCreateInfo bci{};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = sizeof(ShadowUBO);
		bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(m_device, &bci, nullptr, &m_shadowUbo[f]) != VK_SUCCESS) return false;

		VkMemoryRequirements mr;
		vkGetBufferMemoryRequirements(m_device, m_shadowUbo[f], &mr);
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = mr.size;
		mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (vkAllocateMemory(m_device, &mai, nullptr, &m_shadowUboMem[f]) != VK_SUCCESS) return false;
		vkBindBufferMemory(m_device, m_shadowUbo[f], m_shadowUboMem[f], 0);
		vkMapMemory(m_device, m_shadowUboMem[f], 0, sizeof(ShadowUBO), 0, &m_shadowUboMapped[f]);
	}

	// Voxel set=0: b0 sampler2D uShadowMap (frag), b1 uniform ShadowUBO (frag).
	VkDescriptorSetLayoutBinding binds[2]{};
	binds[0].binding = 0;
	binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binds[0].descriptorCount = 1;
	binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	binds[1].binding = 1;
	binds[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	binds[1].descriptorCount = 1;
	binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo slci{};
	slci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	slci.bindingCount = 2; slci.pBindings = binds;
	if (vkCreateDescriptorSetLayout(m_device, &slci, nullptr, &m_voxelSetLayout) != VK_SUCCESS)
		return false;

	VkDescriptorPoolSize poolSz[2] = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          kFramesInFlight },
	};
	VkDescriptorPoolCreateInfo dpci{};
	dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.maxSets = kFramesInFlight;
	dpci.poolSizeCount = 2; dpci.pPoolSizes = poolSz;
	if (vkCreateDescriptorPool(m_device, &dpci, nullptr, &m_voxelDescPool) != VK_SUCCESS)
		return false;

	VkDescriptorSetLayout layouts[kFramesInFlight];
	for (int f = 0; f < kFramesInFlight; f++) layouts[f] = m_voxelSetLayout;
	VkDescriptorSetAllocateInfo dsai{};
	dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsai.descriptorPool = m_voxelDescPool;
	dsai.descriptorSetCount = kFramesInFlight;
	dsai.pSetLayouts = layouts;
	if (vkAllocateDescriptorSets(m_device, &dsai, m_voxelDescSet) != VK_SUCCESS)
		return false;

	for (int f = 0; f < kFramesInFlight; f++) {
		VkDescriptorImageInfo imgInfo{};
		imgInfo.sampler = m_shadowSampler;
		imgInfo.imageView = m_shadowView[f];
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorBufferInfo bufInfo{};
		bufInfo.buffer = m_shadowUbo[f];
		bufInfo.offset = 0;
		bufInfo.range = sizeof(ShadowUBO);

		VkWriteDescriptorSet writes[2]{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_voxelDescSet[f];
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &imgInfo;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_voxelDescSet[f];
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[1].pBufferInfo = &bufInfo;
		vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
	}

	// One-time UNDEFINED → SHADER_READ_ONLY — voxel.frag samples on frame 0
	// before any shadow pass runs. Subsequent frames transition via render pass.
	VkCommandBufferAllocateInfo aci{};
	aci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	aci.commandPool = m_cmdPool;
	aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	aci.commandBufferCount = 1;
	VkCommandBuffer cb;
	vkAllocateCommandBuffers(m_device, &aci, &cb);
	VkCommandBufferBeginInfo bi{};
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cb, &bi);
	for (int f = 0; f < kFramesInFlight; f++) {
		VkImageMemoryBarrier b{};
		b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		b.srcAccessMask = 0;
		b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		b.image = m_shadowImage[f];
		b.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr, 0, nullptr, 1, &b);
	}
	vkEndCommandBuffer(cb);
	VkSubmitInfo si{};
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1; si.pCommandBuffers = &cb;
	vkQueueSubmit(m_gfxQueue, 1, &si, VK_NULL_HANDLE);
	vkQueueWaitIdle(m_gfxQueue);
	vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cb);

	return true;
}

bool VkRhi::createShadowPipeline() {
	auto vsCode = readFile("shaders/vk/shadow.vert.spv");
	auto fsCode = readFile("shaders/vk/shadow.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs; stages[1].pName = "main";

	// Matches voxel pipeline layout (per-vertex b0 + per-instance b1); shader
	// declares all attrs for validator even though it only reads positions.
	VkVertexInputBindingDescription binds[2]{};
	binds[0].binding = 0; binds[0].stride = sizeof(float)*6; binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	binds[1].binding = 1; binds[1].stride = sizeof(float)*6; binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
	VkVertexInputAttributeDescription attrs[4]{};
	attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
	attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = sizeof(float)*3;
	attrs[2].location = 2; attrs[2].binding = 1; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[2].offset = 0;
	attrs[3].location = 3; attrs[3].binding = 1; attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[3].offset = sizeof(float)*3;

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
	vi.vertexAttributeDescriptionCount = 4; vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vp{};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1; vp.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	// Front-face cull kills self-shadow acne (Peter Panning trade-off).
	rs.cullMode = VK_CULL_MODE_FRONT_BIT;
	rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rs.lineWidth = 1.0f;
	// Constant + slope bias — belt-and-suspenders with frag-shader slope bias.
	rs.depthBiasEnable = VK_TRUE;
	rs.depthBiasConstantFactor = 1.25f;
	rs.depthBiasSlopeFactor = 1.75f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = VK_TRUE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;

	// Shadow subpass has no color attachments.
	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 0;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pcr.size = sizeof(float) * 16; // shadowVP
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_shadowLayout) != VK_SUCCESS)
		return false;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vp;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_shadowLayout;
	gpci.renderPass = m_shadowRenderPass;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_shadowPipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

// Box-model shadow: shares m_shadowLayout + m_shadowRenderPass; different
// vertex input (19-float instance: mat4 model + vec3 color) per drawBoxModel.
bool VkRhi::createBoxShadowPipeline() {
	auto vsCode = readFile("shaders/vk/boxshadow.vert.spv");
	auto fsCode = readFile("shaders/vk/shadow.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs; stages[1].pName = "main";

	// b0 per-vertex unit cube (pos+normal); b1 per-instance (mat4+color).
	// Matches createBoxModelPipeline so one buffer feeds both passes.
	VkVertexInputBindingDescription binds[2]{};
	binds[0].binding = 0; binds[0].stride = sizeof(float)*6;  binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	binds[1].binding = 1; binds[1].stride = sizeof(float)*19; binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
	VkVertexInputAttributeDescription attrs[7]{};
	attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[0].offset = 0;
	attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[1].offset = sizeof(float)*3;
	attrs[2].location = 2; attrs[2].binding = 1; attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = sizeof(float)*0;
	attrs[3].location = 3; attrs[3].binding = 1; attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[3].offset = sizeof(float)*4;
	attrs[4].location = 4; attrs[4].binding = 1; attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[4].offset = sizeof(float)*8;
	attrs[5].location = 5; attrs[5].binding = 1; attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[5].offset = sizeof(float)*12;
	attrs[6].location = 6; attrs[6].binding = 1; attrs[6].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[6].offset = sizeof(float)*16;

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
	vi.vertexAttributeDescriptionCount = 7; vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vp{};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1; vp.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	// Back-face cull (not front): characters are too small for the front-cull
	// Peter-Panning trick — would hide the whole silhouette. Rely on bias.
	rs.cullMode = VK_CULL_MODE_BACK_BIT;
	rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rs.lineWidth = 1.0f;
	rs.depthBiasEnable = VK_TRUE;
	rs.depthBiasConstantFactor = 1.25f;
	rs.depthBiasSlopeFactor = 1.75f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = VK_TRUE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 0;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vp;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_shadowLayout;        // reuse voxel shadow layout
	gpci.renderPass = m_shadowRenderPass;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_boxShadowPipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

// Chunk-mesh shadow. Reads only inPos; unused attrs declared in shader to
// match chunk_terrain's layout (validator). Shares m_shadowLayout + pass.
bool VkRhi::createChunkShadowPipeline() {
	auto vsCode = readFile("shaders/vk/shadow_chunk.vert.spv");
	auto fsCode = readFile("shaders/vk/shadow.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs; stages[1].pName = "main";

	// 13 floats/vertex: pos[3] color[3] normal[3] ao shade alpha glow.
	VkVertexInputBindingDescription bind{};
	bind.binding = 0; bind.stride = sizeof(float)*13; bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	VkVertexInputAttributeDescription attrs[7]{};
	attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = sizeof(float)*0;
	attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = sizeof(float)*3;
	attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[2].offset = sizeof(float)*6;
	attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32_SFLOAT;       attrs[3].offset = sizeof(float)*9;
	attrs[4].location = 4; attrs[4].binding = 0; attrs[4].format = VK_FORMAT_R32_SFLOAT;       attrs[4].offset = sizeof(float)*10;
	attrs[5].location = 5; attrs[5].binding = 0; attrs[5].format = VK_FORMAT_R32_SFLOAT;       attrs[5].offset = sizeof(float)*11;
	attrs[6].location = 6; attrs[6].binding = 0; attrs[6].format = VK_FORMAT_R32_SFLOAT;       attrs[6].offset = sizeof(float)*12;

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
	vi.vertexAttributeDescriptionCount = 7; vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vp{};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1; vp.scissorCount = 1;

	// Front-face cull (Peter-Panning trick); CCW must match chunk opaque.
	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_FRONT_BIT;
	rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = VK_TRUE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 0;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vp;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_shadowLayout;
	gpci.renderPass = m_shadowRenderPass;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_chunkShadowPipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

bool VkRhi::ensureBoxShadowInstanceCapacity(int frame, VkDeviceSize bytes) {
	if (m_boxShadowInstCap[frame] >= bytes) return true;

	// Defer destroy — earlier renderBoxShadows in this frame may already
	// have bound the old buffer to the current cmdbuf.
	if (m_boxShadowInstMapped[frame]) { vkUnmapMemory(m_device, m_boxShadowInstMem[frame]); m_boxShadowInstMapped[frame] = nullptr; }
	if (m_boxShadowInstBuf[frame] || m_boxShadowInstMem[frame]) {
		m_pendingBufDestroy[frame].push_back({ m_boxShadowInstBuf[frame], m_boxShadowInstMem[frame] });
		m_boxShadowInstBuf[frame] = VK_NULL_HANDLE;
		m_boxShadowInstMem[frame] = VK_NULL_HANDLE;
	}

	VkDeviceSize cap = std::max<VkDeviceSize>(bytes, 64 * 1024);
	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = cap;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &m_boxShadowInstBuf[frame]) != VK_SUCCESS) return false;

	VkMemoryRequirements req{};
	vkGetBufferMemoryRequirements(m_device, m_boxShadowInstBuf[frame], &req);
	VkMemoryAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = findMemType(req.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &ai, nullptr, &m_boxShadowInstMem[frame]) != VK_SUCCESS) return false;
	vkBindBufferMemory(m_device, m_boxShadowInstBuf[frame], m_boxShadowInstMem[frame], 0);
	vkMapMemory(m_device, m_boxShadowInstMem[frame], 0, cap, 0, &m_boxShadowInstMapped[frame]);
	m_boxShadowInstCap[frame] = cap;
	return true;
}

void VkRhi::destroyShadowResources() {
	for (int f = 0; f < kFramesInFlight; f++) {
		if (m_shadowFB[f]) { vkDestroyFramebuffer(m_device, m_shadowFB[f], nullptr); m_shadowFB[f] = VK_NULL_HANDLE; }
		if (m_shadowView[f]) { vkDestroyImageView(m_device, m_shadowView[f], nullptr); m_shadowView[f] = VK_NULL_HANDLE; }
		if (m_shadowImage[f]) { vkDestroyImage(m_device, m_shadowImage[f], nullptr); m_shadowImage[f] = VK_NULL_HANDLE; }
		if (m_shadowMem[f]) { vkFreeMemory(m_device, m_shadowMem[f], nullptr); m_shadowMem[f] = VK_NULL_HANDLE; }
		if (m_shadowUboMapped[f]) { vkUnmapMemory(m_device, m_shadowUboMem[f]); m_shadowUboMapped[f] = nullptr; }
		if (m_shadowUbo[f]) { vkDestroyBuffer(m_device, m_shadowUbo[f], nullptr); m_shadowUbo[f] = VK_NULL_HANDLE; }
		if (m_shadowUboMem[f]) { vkFreeMemory(m_device, m_shadowUboMem[f], nullptr); m_shadowUboMem[f] = VK_NULL_HANDLE; }
		if (m_boxShadowInstMapped[f]) { vkUnmapMemory(m_device, m_boxShadowInstMem[f]); m_boxShadowInstMapped[f] = nullptr; }
		if (m_boxShadowInstBuf[f]) { vkDestroyBuffer(m_device, m_boxShadowInstBuf[f], nullptr); m_boxShadowInstBuf[f] = VK_NULL_HANDLE; }
		if (m_boxShadowInstMem[f]) { vkFreeMemory(m_device, m_boxShadowInstMem[f], nullptr); m_boxShadowInstMem[f] = VK_NULL_HANDLE; }
	}
	if (m_shadowSampler) { vkDestroySampler(m_device, m_shadowSampler, nullptr); m_shadowSampler = VK_NULL_HANDLE; }
	if (m_shadowPipeline) { vkDestroyPipeline(m_device, m_shadowPipeline, nullptr); m_shadowPipeline = VK_NULL_HANDLE; }
	if (m_boxShadowPipeline) { vkDestroyPipeline(m_device, m_boxShadowPipeline, nullptr); m_boxShadowPipeline = VK_NULL_HANDLE; }
	if (m_chunkShadowPipeline) { vkDestroyPipeline(m_device, m_chunkShadowPipeline, nullptr); m_chunkShadowPipeline = VK_NULL_HANDLE; }
	if (m_shadowLayout) { vkDestroyPipelineLayout(m_device, m_shadowLayout, nullptr); m_shadowLayout = VK_NULL_HANDLE; }
	if (m_shadowRenderPass) { vkDestroyRenderPass(m_device, m_shadowRenderPass, nullptr); m_shadowRenderPass = VK_NULL_HANDLE; }
	if (m_voxelDescPool) { vkDestroyDescriptorPool(m_device, m_voxelDescPool, nullptr); m_voxelDescPool = VK_NULL_HANDLE; }
	if (m_voxelSetLayout) { vkDestroyDescriptorSetLayout(m_device, m_voxelSetLayout, nullptr); m_voxelSetLayout = VK_NULL_HANDLE; }
}

// Lazy shadow pass open — both renderShadows/renderBoxShadows funnel here
// so terrain + box shadows accumulate into one depth image.
void VkRhi::ensureShadowPass() {
	if (m_shadowPassActive || !m_frameActive || !m_shadowRenderPass) return;

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkClearValue clear{};
	clear.depthStencil = { 1.0f, 0 };
	VkRenderPassBeginInfo rpi{};
	rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpi.renderPass = m_shadowRenderPass;
	rpi.framebuffer = m_shadowFB[m_frame];
	rpi.renderArea.extent = { kShadowRes, kShadowRes };
	rpi.clearValueCount = 1; rpi.pClearValues = &clear;
	vkCmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport vp{};
	vp.width = (float)kShadowRes;
	vp.height = (float)kShadowRes;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{ {0,0}, { kShadowRes, kShadowRes } };
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	m_shadowPassActive = true;
}

void VkRhi::endShadowPassIfActive() {
	if (!m_shadowPassActive) return;
	VkCommandBuffer cb = m_cmdBufs[m_frame];
	vkCmdEndRenderPass(cb);
	m_shadowPassActive = false;
	m_shadowValid[m_frame] = true;
}

void VkRhi::renderShadows(const float sunVP[16], const float* instances, uint32_t count) {
	if (!m_frameActive || !m_shadowPipeline || count == 0) return;

	// Upload into shared drawVoxels buffer — drawVoxels re-uploads later.
	VkDeviceSize need = (VkDeviceSize)count * sizeof(float) * 6;
	if (!ensureInstanceCapacity(need)) return;
	void* mapped = nullptr;
	vkMapMemory(m_device, m_instMem, 0, need, 0, &mapped);
	memcpy(mapped, instances, (size_t)need);
	vkUnmapMemory(m_device, m_instMem);

	// shadowVP + params into this frame's UBO for voxel.frag sampling.
	struct ShadowUBO { float shadowVP[16]; float shadowParams[4]; };
	ShadowUBO ubo{};
	memcpy(ubo.shadowVP, sunVP, sizeof(float) * 16);
	ubo.shadowParams[0] = 1.0f / (float)kShadowRes;  // texel size for PCF
	ubo.shadowParams[1] = 0.0008f;                    // constant bias
	ubo.shadowParams[2] = 0.0f;
	ubo.shadowParams[3] = 0.0f;
	memcpy(m_shadowUboMapped[m_frame], &ubo, sizeof(ubo));

	ensureShadowPass();

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
	VkBuffer bufs[2] = { m_cubeVbo, m_instBuf };
	VkDeviceSize offs[2] = { 0, 0 };
	vkCmdBindVertexBuffers(cb, 0, 2, bufs, offs);
	vkCmdPushConstants(cb, m_shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
		sizeof(float) * 16, sunVP);
	vkCmdDraw(cb, m_cubeVertCount, count, 0, 0);
}

void VkRhi::renderBoxShadows(const float sunVP[16], const float* boxes, uint32_t count) {
	if (!m_frameActive || !m_boxShadowPipeline || count == 0) return;

	// Separate from drawBoxModel's buffer so append cursors don't fight —
	// shadow pass writes same boxes main pass reads, via independent buffer.
	VkDeviceSize need = (VkDeviceSize)count * sizeof(float) * 19;
	if (!ensureBoxShadowInstanceCapacity(m_frame, need)) return;
	memcpy(m_boxShadowInstMapped[m_frame], boxes, (size_t)need);

	// Fallback UBO init if only box shadows run (renderShadows normally
	// runs first and does this).
	struct ShadowUBO { float shadowVP[16]; float shadowParams[4]; };
	ShadowUBO ubo{};
	memcpy(ubo.shadowVP, sunVP, sizeof(float) * 16);
	ubo.shadowParams[0] = 1.0f / (float)kShadowRes;
	ubo.shadowParams[1] = 0.0008f;
	memcpy(m_shadowUboMapped[m_frame], &ubo, sizeof(ubo));

	ensureShadowPass();

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boxShadowPipeline);
	VkBuffer bufs[2] = { m_cubeVbo, m_boxShadowInstBuf[m_frame] };
	VkDeviceSize offs[2] = { 0, 0 };
	vkCmdBindVertexBuffers(cb, 0, 2, bufs, offs);
	vkCmdPushConstants(cb, m_shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
		sizeof(float) * 16, sunVP);
	vkCmdDraw(cb, m_cubeVertCount, count, 0, 0);
}

// Lazily begin offscreen main pass on first scene draw — lets renderShadows
// precede beginFrame's content cleanly.
void VkRhi::ensureMainPass() {
	if (m_mainPassActive || !m_frameActive) return;
	// Close shadow pass first — only one active render pass per cmd buffer.
	endShadowPassIfActive();
	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkClearValue clears[2]{};
	clears[0].color = { {0.07f, 0.09f, 0.13f, 1.0f} };
	clears[1].depthStencil = { 1.0f, 0 };
	VkRenderPassBeginInfo rpi{};
	rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpi.renderPass = m_offRenderPass;
	rpi.framebuffer = m_offFB[m_frame];
	rpi.renderArea.extent = m_swapExtent;
	rpi.clearValueCount = 2; rpi.pClearValues = clears;
	vkCmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);
	m_mainPassActive = true;
}

bool VkRhi::createVoxelPipeline() {
	auto vsCode = readFile("shaders/vk/voxel.vert.spv");
	auto fsCode = readFile("shaders/vk/voxel.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs; stages[1].pName = "main";

	// b0 per-vertex (cube VBO), b1 per-instance.
	VkVertexInputBindingDescription binds[2]{};
	binds[0].binding = 0; binds[0].stride = sizeof(float)*6; binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	binds[1].binding = 1; binds[1].stride = sizeof(float)*6; binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

	VkVertexInputAttributeDescription attrs[4]{};
	attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;
	attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = sizeof(float)*3;
	attrs[2].location = 2; attrs[2].binding = 1; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[2].offset = 0;
	attrs[3].location = 3; attrs[3].binding = 1; attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[3].offset = sizeof(float)*3;

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
	vi.vertexAttributeDescriptionCount = 4; vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vp{};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1; vp.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_BACK_BIT;
	rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendAttachmentState cba{};
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1; cb.pAttachments = &cba;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pcr.size = sizeof(IRhi::SceneParams); // 96B
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &m_voxelSetLayout;
	plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_voxelLayout) != VK_SUCCESS) return false;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vp;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_voxelLayout;
	gpci.renderPass = m_renderPass;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_voxelPipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

bool VkRhi::createBoxModelPipeline() {
	// Reuses m_voxelLayout + voxel.frag — only VS + per-instance input differ.
	auto vsCode = readFile("shaders/vk/boxmodel.vert.spv");
	auto fsCode = readFile("shaders/vk/voxel.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs; stages[1].pName = "main";

	// b0 unit cube (shared m_cubeVbo); b1 per-instance 19 floats —
	// mat4 as 4 column vec4s (GLM column-major) + vec3 color.
	VkVertexInputBindingDescription binds[2]{};
	binds[0].binding = 0; binds[0].stride = sizeof(float)*6;  binds[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	binds[1].binding = 1; binds[1].stride = sizeof(float)*19; binds[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

	VkVertexInputAttributeDescription attrs[7]{};
	attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[0].offset = 0;
	attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[1].offset = sizeof(float)*3;
	attrs[2].location = 2; attrs[2].binding = 1; attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = sizeof(float)*0;
	attrs[3].location = 3; attrs[3].binding = 1; attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[3].offset = sizeof(float)*4;
	attrs[4].location = 4; attrs[4].binding = 1; attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[4].offset = sizeof(float)*8;
	attrs[5].location = 5; attrs[5].binding = 1; attrs[5].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[5].offset = sizeof(float)*12;
	attrs[6].location = 6; attrs[6].binding = 1; attrs[6].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[6].offset = sizeof(float)*16;

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 2; vi.pVertexBindingDescriptions = binds;
	vi.vertexAttributeDescriptionCount = 7; vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vp{};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1; vp.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_BACK_BIT;
	rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendAttachmentState cba{};
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1; cb.pAttachments = &cba;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vp;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_voxelLayout;
	gpci.renderPass = m_renderPass;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_boxModelPipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

bool VkRhi::ensureBoxInstanceCapacity(int frame, VkDeviceSize bytes) {
	if (bytes <= m_boxInstCap[frame]) return true;
	VkDeviceSize cap = m_boxInstCap[frame] ? m_boxInstCap[frame] : 64 * sizeof(float)*19;
	while (cap < bytes) cap *= 2;
	// Defer destroy — earlier drawBoxModel in this frame may have bound
	// the old buffer to the current cmdbuf. vkDeviceWaitIdle wouldn't help
	// here (the cmdbuf is being recorded, not executing).
	if (m_boxInstMapped[frame]) { vkUnmapMemory(m_device, m_boxInstMem[frame]); m_boxInstMapped[frame] = nullptr; }
	if (m_boxInstBuf[frame] || m_boxInstMem[frame]) {
		m_pendingBufDestroy[frame].push_back({ m_boxInstBuf[frame], m_boxInstMem[frame] });
		m_boxInstBuf[frame] = VK_NULL_HANDLE;
		m_boxInstMem[frame] = VK_NULL_HANDLE;
	}

	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = cap;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &m_boxInstBuf[frame]) != VK_SUCCESS) return false;

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(m_device, m_boxInstBuf[frame], &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &m_boxInstMem[frame]) != VK_SUCCESS) return false;
	vkBindBufferMemory(m_device, m_boxInstBuf[frame], m_boxInstMem[frame], 0);
	vkMapMemory(m_device, m_boxInstMem[frame], 0, cap, 0, &m_boxInstMapped[frame]);
	m_boxInstCap[frame] = cap;
	return true;
}

bool VkRhi::ensureInstanceCapacity(VkDeviceSize bytes) {
	if (bytes <= m_instCap) return true;
	VkDeviceSize cap = m_instCap ? m_instCap : 64 * sizeof(float)*6;
	while (cap < bytes) cap *= 2;
	// Defer destroy — earlier renderShadows/drawVoxels in this frame (or
	// prior in-flight frames) may have bound the old buffer. Push to the
	// current slot's pending list so it survives until that slot's next
	// beginFrame fence wait + cmdbuf reset.
	if (m_instBuf || m_instMem) {
		m_pendingBufDestroy[m_frame].push_back({ m_instBuf, m_instMem });
		m_instBuf = VK_NULL_HANDLE;
		m_instMem = VK_NULL_HANDLE;
	}

	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = cap;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &m_instBuf) != VK_SUCCESS) return false;

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(m_device, m_instBuf, &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &m_instMem) != VK_SUCCESS) return false;
	vkBindBufferMemory(m_device, m_instBuf, m_instMem, 0);
	m_instCap = cap;
	return true;
}

void VkRhi::drawVoxels(const SceneParams& scene, const float* instances, uint32_t count) {
	if (!m_frameActive || !m_voxelPipeline || count == 0) return;
	ensureMainPass();

	VkDeviceSize need = (VkDeviceSize)count * sizeof(float) * 6;
	if (!ensureInstanceCapacity(need)) return;

	// Redundant if renderShadows already uploaded — cheap, keep simple.
	void* mapped = nullptr;
	vkMapMemory(m_device, m_instMem, 0, need, 0, &mapped);
	memcpy(mapped, instances, (size_t)need);
	vkUnmapMemory(m_device, m_instMem);

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0,0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_voxelPipeline);
	// Per-frame set (shadow map + UBO). First frame reads before any shadow
	// pass — harmless, shader bounds check returns 1.0 (unshadowed).
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_voxelLayout, 0, 1, &m_voxelDescSet[m_frame], 0, nullptr);
	VkBuffer bufs[2] = { m_cubeVbo, m_instBuf };
	VkDeviceSize offs[2] = { 0, 0 };
	vkCmdBindVertexBuffers(cb, 0, 2, bufs, offs);
	vkCmdPushConstants(cb, m_voxelLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(SceneParams), &scene);
	vkCmdDraw(cb, m_cubeVertCount, count, 0, 0);

	// Stash viewProj + invVP for composite pass.
	glm::mat4 vpMat;
	memcpy(&vpMat[0][0], scene.viewProj, sizeof(float) * 16);
	glm::mat4 invVP = glm::inverse(vpMat);
	memcpy(m_compPC.invVP, &invVP[0][0], sizeof(float) * 16);
	memcpy(m_compPC.vp, scene.viewProj, sizeof(float) * 16);
}

// Persistent voxel meshes — one-time upload for static terrain; same 6-float
// instance format as drawVoxels so pipelines bind without changes.

IRhi::MeshHandle VkRhi::createVoxelMesh(const float* instances, uint32_t count) {
	if (!m_device || count == 0 || !instances) return kInvalidMesh;

	PersistentMesh mesh{};
	mesh.instCount = count;
	VkDeviceSize bytes = (VkDeviceSize)count * sizeof(float) * 6;

	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = bytes;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &mesh.buf) != VK_SUCCESS)
		return kInvalidMesh;

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(m_device, mesh.buf, &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &mesh.mem) != VK_SUCCESS) {
		vkDestroyBuffer(m_device, mesh.buf, nullptr);
		return kInvalidMesh;
	}
	vkBindBufferMemory(m_device, mesh.buf, mesh.mem, 0);

	void* mapped = nullptr;
	vkMapMemory(m_device, mesh.mem, 0, bytes, 0, &mapped);
	memcpy(mapped, instances, (size_t)bytes);
	vkUnmapMemory(m_device, mesh.mem);
	mesh.capBytes = bytes;

	uint64_t id = m_nextMeshId++;
	m_meshes[id] = mesh;
	return id;
}

void VkRhi::updateVoxelMesh(MeshHandle mesh, const float* instances, uint32_t count) {
	auto it = m_meshes.find(mesh);
	if (it == m_meshes.end() || !instances) return;
	PersistentMesh& m = it->second;

	VkDeviceSize bytes = (VkDeviceSize)count * sizeof(float) * 6;

	// Fast path — fits in existing buffer. Safe without sync because chunk
	// re-meshes happen between frames, not after an in-frame bind.
	if (bytes <= m.capBytes) {
		if (count > 0) {
			void* mapped = nullptr;
			vkMapMemory(m_device, m.mem, 0, bytes, 0, &mapped);
			memcpy(mapped, instances, (size_t)bytes);
			vkUnmapMemory(m_device, m.mem);
		}
		m.instCount = count;
		return;
	}

	// Grow: 2x-min new buffer; old goes to deferred-destroy queue.
	VkDeviceSize newCap = m.capBytes ? m.capBytes : bytes;
	while (newCap < bytes) newCap *= 2;

	VkBuffer newBuf = VK_NULL_HANDLE;
	VkDeviceMemory newMem = VK_NULL_HANDLE;
	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = newCap;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &newBuf) != VK_SUCCESS) return;

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(m_device, newBuf, &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &newMem) != VK_SUCCESS) {
		vkDestroyBuffer(m_device, newBuf, nullptr);
		return;
	}
	vkBindBufferMemory(m_device, newBuf, newMem, 0);

	void* mapped = nullptr;
	vkMapMemory(m_device, newMem, 0, bytes, 0, &mapped);
	memcpy(mapped, instances, (size_t)bytes);
	vkUnmapMemory(m_device, newMem);

	// Pick the slot whose fence signals AFTER the last submission that could
	// reference the old buffer — see destroyMesh() for details.
	const uint32_t pendingSlot = m_frameActive
		? m_frame
		: ((m_frame + kFramesInFlight - 1) % kFramesInFlight);
	m_meshPending[pendingSlot].push_back(PersistentMesh{ m.buf, m.mem, m.instCount, m.capBytes });
	m.buf       = newBuf;
	m.mem       = newMem;
	m.capBytes  = newCap;
	m.instCount = count;
}

void VkRhi::destroyMesh(MeshHandle mesh) {
	// Defer release — in-flight cmd bufs may still reference it. Push to the
	// slot whose fence signals AFTER all submissions that touched this buffer:
	// recording → m_frame (fence set at this frame's endFrame);
	// between frames → prior slot (most recent submission).
	// Handles share an ID space — try voxel then chunk meshes.
	const uint32_t pendingSlot = m_frameActive
		? m_frame
		: ((m_frame + kFramesInFlight - 1) % kFramesInFlight);
	auto it = m_meshes.find(mesh);
	if (it != m_meshes.end()) {
		m_meshPending[pendingSlot].push_back(it->second);
		m_meshes.erase(it);
		return;
	}
	auto cit = m_chunkMeshes.find(mesh);
	if (cit != m_chunkMeshes.end()) {
		m_chunkMeshPending[pendingSlot].push_back(cit->second);
		m_chunkMeshes.erase(cit);
	}
}

void VkRhi::drawVoxelsMesh(const SceneParams& scene, MeshHandle mesh) {
	if (!m_frameActive || !m_voxelPipeline) return;
	auto it = m_meshes.find(mesh);
	if (it == m_meshes.end() || it->second.instCount == 0) return;
	const PersistentMesh& m = it->second;

	ensureMainPass();

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0,0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_voxelPipeline);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_voxelLayout, 0, 1, &m_voxelDescSet[m_frame], 0, nullptr);
	VkBuffer bufs[2] = { m_cubeVbo, m.buf };
	VkDeviceSize offs[2] = { 0, 0 };
	vkCmdBindVertexBuffers(cb, 0, 2, bufs, offs);
	vkCmdPushConstants(cb, m_voxelLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(SceneParams), &scene);
	vkCmdDraw(cb, m_cubeVertCount, m.instCount, 0, 0);

	// Stash VP/invVP for composite — same as drawVoxels.
	glm::mat4 vpMat;
	memcpy(&vpMat[0][0], scene.viewProj, sizeof(float) * 16);
	glm::mat4 invVP = glm::inverse(vpMat);
	memcpy(m_compPC.invVP, &invVP[0][0], sizeof(float) * 16);
	memcpy(m_compPC.vp, scene.viewProj, sizeof(float) * 16);
}

void VkRhi::renderShadowsMesh(const float sunVP[16], MeshHandle mesh) {
	if (!m_frameActive || !m_shadowPipeline) return;
	auto it = m_meshes.find(mesh);
	if (it == m_meshes.end() || it->second.instCount == 0) return;
	const PersistentMesh& m = it->second;

	// voxel.frag reads UBO every lit draw — must land before drawVoxelsMesh.
	struct ShadowUBO { float shadowVP[16]; float shadowParams[4]; };
	ShadowUBO ubo{};
	memcpy(ubo.shadowVP, sunVP, sizeof(float) * 16);
	ubo.shadowParams[0] = 1.0f / (float)kShadowRes;
	ubo.shadowParams[1] = 0.0008f;
	ubo.shadowParams[2] = 0.0f;
	ubo.shadowParams[3] = 0.0f;
	memcpy(m_shadowUboMapped[m_frame], &ubo, sizeof(ubo));

	ensureShadowPass();

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
	VkBuffer bufs[2] = { m_cubeVbo, m.buf };
	VkDeviceSize offs[2] = { 0, 0 };
	vkCmdBindVertexBuffers(cb, 0, 2, bufs, offs);
	vkCmdPushConstants(cb, m_shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
		sizeof(float) * 16, sunVP);
	vkCmdDraw(cb, m_cubeVertCount, m.instCount, 0, 0);
}

// Chunk meshes (13-float/vertex). Dual pipeline (opaque+transparent); same
// defer-destroy pattern as voxel meshes.

static constexpr uint32_t kChunkVertStride = sizeof(float) * 13;

bool VkRhi::createChunkPipelines() {
	auto vsCode = readFile("shaders/vk/chunk_terrain.vert.spv");
	auto fsCode = readFile("shaders/vk/chunk_terrain.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs; stages[1].pName = "main";

	// 13 floats/vertex: pos[3] color[3] normal[3] ao shade alpha glow.
	VkVertexInputBindingDescription bind{};
	bind.binding = 0; bind.stride = kChunkVertStride; bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attrs[7]{};
	attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = sizeof(float)*0;
	attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[1].offset = sizeof(float)*3;
	attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[2].offset = sizeof(float)*6;
	attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32_SFLOAT;       attrs[3].offset = sizeof(float)*9;
	attrs[4].location = 4; attrs[4].binding = 0; attrs[4].format = VK_FORMAT_R32_SFLOAT;       attrs[4].offset = sizeof(float)*10;
	attrs[5].location = 5; attrs[5].binding = 0; attrs[5].format = VK_FORMAT_R32_SFLOAT;       attrs[5].offset = sizeof(float)*11;
	attrs[6].location = 6; attrs[6].binding = 0; attrs[6].format = VK_FORMAT_R32_SFLOAT;       attrs[6].offset = sizeof(float)*12;

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
	vi.vertexAttributeDescriptionCount = 7; vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vp{};
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1; vp.scissorCount = 1;

	// Opaque: cull back, CCW. chunk_mesher emits CCW outward in world space;
	// proj Y-flip + VK Y-down framebuffer preserves winding end-to-end.
	// Voxel cube template is CW-from-outside and uses a separate pipeline.
	VkPipelineRasterizationStateCreateInfo rsOpaque{};
	rsOpaque.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rsOpaque.polygonMode = VK_POLYGON_MODE_FILL;
	rsOpaque.cullMode = VK_CULL_MODE_BACK_BIT;
	rsOpaque.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rsOpaque.lineWidth = 1.0f;

	// Transparent: no cull (thin glass / portal panes).
	VkPipelineRasterizationStateCreateInfo rsTrans = rsOpaque;
	rsTrans.cullMode = VK_CULL_MODE_NONE;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo dsOpaque{};
	dsOpaque.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	dsOpaque.depthTestEnable = VK_TRUE; dsOpaque.depthWriteEnable = VK_TRUE;
	dsOpaque.depthCompareOp = VK_COMPARE_OP_LESS;

	// Transparent: test on, write off for clean multi-layer compose.
	VkPipelineDepthStencilStateCreateInfo dsTrans = dsOpaque;
	dsTrans.depthWriteEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState cbaOpaque{};
	cbaOpaque.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                         | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cbOpaque{};
	cbOpaque.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cbOpaque.attachmentCount = 1; cbOpaque.pAttachments = &cbaOpaque;

	// Transparent: standard alpha blend.
	VkPipelineColorBlendAttachmentState cbaTrans = cbaOpaque;
	cbaTrans.blendEnable = VK_TRUE;
	cbaTrans.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	cbaTrans.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	cbaTrans.colorBlendOp = VK_BLEND_OP_ADD;
	cbaTrans.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cbaTrans.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	cbaTrans.alphaBlendOp = VK_BLEND_OP_ADD;
	VkPipelineColorBlendStateCreateInfo cbTrans = cbOpaque;
	cbTrans.pAttachments = &cbaTrans;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	// 128-byte push constant: SceneParams (96) + fog (16) + fogExtra (16).
	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pcr.size = 128;
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_chunkLayout) != VK_SUCCESS) {
		vkDestroyShaderModule(m_device, vs, nullptr);
		vkDestroyShaderModule(m_device, fs, nullptr);
		return false;
	}

	auto buildPipeline = [&](VkPipelineRasterizationStateCreateInfo& rs,
	                         VkPipelineDepthStencilStateCreateInfo& ds,
	                         VkPipelineColorBlendStateCreateInfo& cb,
	                         VkPipeline& outPipeline) -> bool {
		VkGraphicsPipelineCreateInfo gpci{};
		gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		gpci.stageCount = 2; gpci.pStages = stages;
		gpci.pVertexInputState = &vi;
		gpci.pInputAssemblyState = &ia;
		gpci.pViewportState = &vp;
		gpci.pRasterizationState = &rs;
		gpci.pMultisampleState = &ms;
		gpci.pDepthStencilState = &ds;
		gpci.pColorBlendState = &cb;
		gpci.pDynamicState = &dynState;
		gpci.layout = m_chunkLayout;
		gpci.renderPass = m_renderPass;
		return vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &outPipeline) == VK_SUCCESS;
	};

	bool ok = buildPipeline(rsOpaque, dsOpaque, cbOpaque, m_chunkPipelineOpaque)
	       && buildPipeline(rsTrans,  dsTrans,  cbTrans,  m_chunkPipelineTransparent);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return ok;
}

IRhi::MeshHandle VkRhi::createChunkMesh(const float* verts, uint32_t vertexCount) {
	if (!m_device || vertexCount == 0 || !verts) return kInvalidMesh;

	PersistentMesh mesh{};
	mesh.instCount = vertexCount;  // semantic: vertex count here
	VkDeviceSize bytes = (VkDeviceSize)vertexCount * kChunkVertStride;

	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = bytes;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &mesh.buf) != VK_SUCCESS) return kInvalidMesh;

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(m_device, mesh.buf, &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &mesh.mem) != VK_SUCCESS) {
		vkDestroyBuffer(m_device, mesh.buf, nullptr);
		return kInvalidMesh;
	}
	vkBindBufferMemory(m_device, mesh.buf, mesh.mem, 0);

	void* mapped = nullptr;
	vkMapMemory(m_device, mesh.mem, 0, bytes, 0, &mapped);
	memcpy(mapped, verts, (size_t)bytes);
	vkUnmapMemory(m_device, mesh.mem);
	mesh.capBytes = bytes;

	uint64_t id = m_nextMeshId++;
	m_chunkMeshes[id] = mesh;
	return id;
}

void VkRhi::updateChunkMesh(MeshHandle mesh, const float* verts, uint32_t vertexCount) {
	auto it = m_chunkMeshes.find(mesh);
	if (it == m_chunkMeshes.end() || !verts) return;
	PersistentMesh& m = it->second;

	VkDeviceSize bytes = (VkDeviceSize)vertexCount * kChunkVertStride;
	if (bytes <= m.capBytes) {
		if (vertexCount > 0) {
			void* mapped = nullptr;
			vkMapMemory(m_device, m.mem, 0, bytes, 0, &mapped);
			memcpy(mapped, verts, (size_t)bytes);
			vkUnmapMemory(m_device, m.mem);
		}
		m.instCount = vertexCount;
		return;
	}

	VkDeviceSize newCap = m.capBytes ? m.capBytes : bytes;
	while (newCap < bytes) newCap *= 2;

	VkBuffer newBuf = VK_NULL_HANDLE;
	VkDeviceMemory newMem = VK_NULL_HANDLE;
	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = newCap;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &newBuf) != VK_SUCCESS) return;

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(m_device, newBuf, &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &newMem) != VK_SUCCESS) {
		vkDestroyBuffer(m_device, newBuf, nullptr);
		return;
	}
	vkBindBufferMemory(m_device, newBuf, newMem, 0);

	void* mapped = nullptr;
	vkMapMemory(m_device, newMem, 0, bytes, 0, &mapped);
	memcpy(mapped, verts, (size_t)bytes);
	vkUnmapMemory(m_device, newMem);

	// Defer destroy of the old buffer — see destroyMesh() for slot reasoning.
	const uint32_t pendingSlot = m_frameActive
		? m_frame
		: ((m_frame + kFramesInFlight - 1) % kFramesInFlight);
	m_chunkMeshPending[pendingSlot].push_back(PersistentMesh{ m.buf, m.mem, m.instCount, m.capBytes });
	m.buf = newBuf; m.mem = newMem;
	m.capBytes = newCap;
	m.instCount = vertexCount;
}

namespace {
struct ChunkPC {
	float viewProj[16];
	float camPos[4];   // xyz + time
	float sunDir[4];   // xyz + sunStrength
	float fog[4];      // rgb + fogStart
	float fogExtra[4]; // x = fogEnd
};
static_assert(sizeof(ChunkPC) == 128, "Chunk push constant must be 128 bytes");
}  // namespace

static void packChunkPC(ChunkPC& pc, const IRhi::SceneParams& scene,
                         const float fogColor[3], float fogStart, float fogEnd) {
	memcpy(pc.viewProj, scene.viewProj, sizeof(float) * 16);
	pc.camPos[0] = scene.camPos[0]; pc.camPos[1] = scene.camPos[1];
	pc.camPos[2] = scene.camPos[2]; pc.camPos[3] = scene.time;
	pc.sunDir[0] = scene.sunDir[0]; pc.sunDir[1] = scene.sunDir[1];
	pc.sunDir[2] = scene.sunDir[2]; pc.sunDir[3] = scene.sunStr;
	pc.fog[0] = fogColor[0]; pc.fog[1] = fogColor[1]; pc.fog[2] = fogColor[2];
	pc.fog[3] = fogStart;
	pc.fogExtra[0] = fogEnd;
	pc.fogExtra[1] = scene.seasonPhase;  // 0..4 — frag tints grass-top faces
	pc.fogExtra[2] = 0.0f;  // reserved — rain desat moved to composite UBO
	pc.fogExtra[3] = 0.0f;
}

void VkRhi::drawChunkMeshOpaque(const SceneParams& scene, const float fogColor[3],
                                 float fogStart, float fogEnd, MeshHandle mesh) {
	if (!m_frameActive || !m_chunkPipelineOpaque) return;
	auto it = m_chunkMeshes.find(mesh);
	if (it == m_chunkMeshes.end() || it->second.instCount == 0) return;
	const PersistentMesh& m = it->second;

	ensureMainPass();

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0,0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_chunkPipelineOpaque);
	VkDeviceSize off = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &m.buf, &off);

	ChunkPC pc;
	packChunkPC(pc, scene, fogColor, fogStart, fogEnd);
	vkCmdPushConstants(cb, m_chunkLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(pc), &pc);
	vkCmdDraw(cb, m.instCount, 1, 0, 0);

	// Stash VP for composite — same as drawVoxels{,Mesh}.
	glm::mat4 vpMat;
	memcpy(&vpMat[0][0], scene.viewProj, sizeof(float) * 16);
	glm::mat4 invVP = glm::inverse(vpMat);
	memcpy(m_compPC.invVP, &invVP[0][0], sizeof(float) * 16);
	memcpy(m_compPC.vp, scene.viewProj, sizeof(float) * 16);
}

void VkRhi::drawChunkMeshTransparent(const SceneParams& scene, const float fogColor[3],
                                      float fogStart, float fogEnd, MeshHandle mesh) {
	if (!m_frameActive || !m_chunkPipelineTransparent) return;
	auto it = m_chunkMeshes.find(mesh);
	if (it == m_chunkMeshes.end() || it->second.instCount == 0) return;
	const PersistentMesh& m = it->second;

	ensureMainPass();

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0,0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_chunkPipelineTransparent);
	VkDeviceSize off = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &m.buf, &off);

	ChunkPC pc;
	packChunkPC(pc, scene, fogColor, fogStart, fogEnd);
	vkCmdPushConstants(cb, m_chunkLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(pc), &pc);
	vkCmdDraw(cb, m.instCount, 1, 0, 0);
}

void VkRhi::renderShadowsChunkMesh(const float sunVP[16], MeshHandle mesh) {
	if (!m_frameActive || !m_chunkShadowPipeline) return;
	auto it = m_chunkMeshes.find(mesh);
	if (it == m_chunkMeshes.end() || it->second.instCount == 0) return;
	const PersistentMesh& m = it->second;

	// Keep UBO in sync — chunk_terrain.frag ignores, but voxel/box lit passes read.
	struct ShadowUBO { float shadowVP[16]; float shadowParams[4]; };
	ShadowUBO ubo{};
	memcpy(ubo.shadowVP, sunVP, sizeof(float) * 16);
	ubo.shadowParams[0] = 1.0f / (float)kShadowRes;
	ubo.shadowParams[1] = 0.0008f;
	ubo.shadowParams[2] = 0.0f;
	ubo.shadowParams[3] = 0.0f;
	memcpy(m_shadowUboMapped[m_frame], &ubo, sizeof(ubo));

	ensureShadowPass();

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_chunkShadowPipeline);
	VkDeviceSize off = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &m.buf, &off);
	vkCmdPushConstants(cb, m_shadowLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
		sizeof(float) * 16, sunVP);
	vkCmdDraw(cb, m.instCount, 1, 0, 0);
}

void VkRhi::drawBoxModel(const SceneParams& scene, const float* boxes, uint32_t count) {
	if (!m_frameActive || !m_boxModelPipeline || count == 0) return;
	ensureMainPass();

	uint32_t firstInstance = m_boxInstCount[m_frame];
	VkDeviceSize need = ((VkDeviceSize)firstInstance + count) * sizeof(float) * 19;
	if (!ensureBoxInstanceCapacity(m_frame, need)) return;

	// Append via m_boxInstCount cursor so multiple calls don't collide.
	// 19 floats/instance = mat4(16) + color(3).
	float* dst = (float*)m_boxInstMapped[m_frame] + (size_t)firstInstance * 19;
	memcpy(dst, boxes, (size_t)count * sizeof(float) * 19);
	m_boxInstCount[m_frame] = firstInstance + count;

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0,0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boxModelPipeline);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_voxelLayout, 0, 1, &m_voxelDescSet[m_frame], 0, nullptr);
	VkBuffer bufs[2] = { m_cubeVbo, m_boxInstBuf[m_frame] };
	VkDeviceSize offs[2] = { 0, 0 };
	vkCmdBindVertexBuffers(cb, 0, 2, bufs, offs);
	vkCmdPushConstants(cb, m_voxelLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(SceneParams), &scene);
	vkCmdDraw(cb, m_cubeVertCount, count, 0, firstInstance);
}

bool VkRhi::screenshot(const char* path) {
	if (!m_frameActive) return false;

	// End pass so we can copy from the swapchain image.
	VkCommandBuffer cb = m_cmdBufs[m_frame];
	vkCmdEndRenderPass(cb);

	uint32_t w = m_swapExtent.width, h = m_swapExtent.height;

	// Host-visible staging buffer.
	VkDeviceSize bufSize = (VkDeviceSize)w * h * 4;
	VkBuffer buf = VK_NULL_HANDLE;
	VkDeviceMemory mem = VK_NULL_HANDLE;
	VkBufferCreateInfo bci{}; bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = bufSize; bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	vkCreateBuffer(m_device, &bci, nullptr, &buf);
	VkMemoryRequirements mr; vkGetBufferMemoryRequirements(m_device, buf, &mr);
	VkMemoryAllocateInfo mai{}; mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	vkAllocateMemory(m_device, &mai, nullptr, &mem);
	vkBindBufferMemory(m_device, buf, mem, 0);

	// PRESENT_SRC → TRANSFER_SRC.
	VkImageMemoryBarrier b1{};
	b1.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	b1.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	b1.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b1.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	b1.image = m_swapImages[m_imageIndex];
	b1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b1);

	VkBufferImageCopy region{};
	region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	region.imageExtent = {w, h, 1};
	vkCmdCopyImageToBuffer(cb, m_swapImages[m_imageIndex],
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);

	// Back to PRESENT_SRC for present.
	VkImageMemoryBarrier b2 = b1;
	b2.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b2.dstAccessMask = 0;
	b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	b2.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b2);

	// Submit + wait so CPU can read.
	vkEndCommandBuffer(cb);
	VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.waitSemaphoreCount = 1; si.pWaitSemaphores = &m_imgAvail[m_frame];
	si.pWaitDstStageMask = &wait;
	si.commandBufferCount = 1; si.pCommandBuffers = &cb;
	si.signalSemaphoreCount = 1; si.pSignalSemaphores = &m_renderDone[m_frame];
	vkQueueSubmit(m_gfxQueue, 1, &si, m_inFlight[m_frame]);
	vkQueueWaitIdle(m_gfxQueue);

	// Write PPM.
	void* mapped = nullptr;
	vkMapMemory(m_device, mem, 0, bufSize, 0, &mapped);
	uint8_t* px = (uint8_t*)mapped;

	FILE* f = fopen(path, "wb");
	bool ok = false;
	if (f) {
		fprintf(f, "P6\n%u %u\n255\n", w, h);
		// B8G8R8A8 → RGB.
		for (uint32_t i = 0; i < w * h; i++) {
			uint8_t rgb[3] = { px[i*4+2], px[i*4+1], px[i*4+0] };
			fwrite(rgb, 3, 1, f);
		}
		fclose(f);
		ok = true;
	}
	vkUnmapMemory(m_device, mem);
	vkDestroyBuffer(m_device, buf, nullptr);
	vkFreeMemory(m_device, mem, nullptr);

	// Consumed endFrame's submit — present here and flag frame inactive so
	// endFrame skips its own submit+present.
	VkPresentInfoKHR pi{}; pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &m_renderDone[m_frame];
	pi.swapchainCount = 1; pi.pSwapchains = &m_swapchain; pi.pImageIndices = &m_imageIndex;
	vkQueuePresentKHR(m_gfxQueue, &pi);
	m_frame = (m_frame + 1) % kFramesInFlight;
	m_frameActive = false;
	if (ok) printf("[vk] Screenshot: %s\n", path);
	return ok;
}

bool VkRhi::createSkyPipeline() {
	auto vsCode = readFile("shaders/vk/sky.vert.spv");
	auto fsCode = readFile("shaders/vk/sky.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vpState{};
	vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vpState.viewportCount = 1; vpState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_NONE;
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_FALSE;
	ds.depthWriteEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState cba{};
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1; cb.pAttachments = &cba;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	// PC: invVP(mat4) + sunDir(vec4) + skyParams(vec4) = 96B.
	// skyParams.x = timeSec (cloud/star anim); yzw reserved.
	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pcr.size = sizeof(float) * (16 + 4 + 4);
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_skyLayout) != VK_SUCCESS) return false;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vpState;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_skyLayout;
	gpci.renderPass = m_renderPass;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_skyPipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

// Post-process: offscreen render → composite (SSAO + bloom + tone map).

bool VkRhi::createOffscreenRenderPass() {
	VkAttachmentDescription atts[2]{};
	atts[0].format = m_swapFormat;
	atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
	atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	atts[1].format = m_depthFormat;
	atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
	atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	atts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	atts[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	atts[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	VkSubpassDescription sub{};
	sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sub.colorAttachmentCount = 1;
	sub.pColorAttachments = &colorRef;
	sub.pDepthStencilAttachment = &depthRef;

	VkSubpassDependency deps[2]{};
	deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	deps[0].dstSubpass = 0;
	deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	deps[0].srcAccessMask = 0;
	deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].srcSubpass = 0;
	deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	VkRenderPassCreateInfo rci{};
	rci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rci.attachmentCount = 2;
	rci.pAttachments = atts;
	rci.subpassCount = 1;
	rci.pSubpasses = &sub;
	rci.dependencyCount = 2;
	rci.pDependencies = deps;
	return vkCreateRenderPass(m_device, &rci, nullptr, &m_offRenderPass) == VK_SUCCESS;
}

bool VkRhi::createOffscreen() {
	for (int f = 0; f < kFramesInFlight; f++) {
		VkImageCreateInfo ici{};
		ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		ici.imageType = VK_IMAGE_TYPE_2D;
		ici.format = m_swapFormat;
		ici.extent = {m_swapExtent.width, m_swapExtent.height, 1};
		ici.mipLevels = 1; ici.arrayLayers = 1;
		ici.samples = VK_SAMPLE_COUNT_1_BIT;
		ici.tiling = VK_IMAGE_TILING_OPTIMAL;
		ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (vkCreateImage(m_device, &ici, nullptr, &m_offColor[f]) != VK_SUCCESS) return false;

		VkMemoryRequirements mr;
		vkGetImageMemoryRequirements(m_device, m_offColor[f], &mr);
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = mr.size;
		mai.memoryTypeIndex = findMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (vkAllocateMemory(m_device, &mai, nullptr, &m_offColorMem[f]) != VK_SUCCESS) return false;
		vkBindImageMemory(m_device, m_offColor[f], m_offColorMem[f], 0);

		VkImageViewCreateInfo vci{};
		vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		vci.image = m_offColor[f];
		vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
		vci.format = m_swapFormat;
		vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		if (vkCreateImageView(m_device, &vci, nullptr, &m_offColorView[f]) != VK_SUCCESS) return false;

		// Depth image — SAMPLED for composite shader.
		ici.format = m_depthFormat;
		ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		if (vkCreateImage(m_device, &ici, nullptr, &m_offDepth[f]) != VK_SUCCESS) return false;

		vkGetImageMemoryRequirements(m_device, m_offDepth[f], &mr);
		mai.allocationSize = mr.size;
		mai.memoryTypeIndex = findMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (vkAllocateMemory(m_device, &mai, nullptr, &m_offDepthMem[f]) != VK_SUCCESS) return false;
		vkBindImageMemory(m_device, m_offDepth[f], m_offDepthMem[f], 0);

		vci.image = m_offDepth[f];
		vci.format = m_depthFormat;
		vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (vkCreateImageView(m_device, &vci, nullptr, &m_offDepthView[f]) != VK_SUCCESS) return false;

		VkImageView fbAtts[2] = {m_offColorView[f], m_offDepthView[f]};
		VkFramebufferCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fci.renderPass = m_offRenderPass;
		fci.attachmentCount = 2;
		fci.pAttachments = fbAtts;
		fci.width = m_swapExtent.width;
		fci.height = m_swapExtent.height;
		fci.layers = 1;
		if (vkCreateFramebuffer(m_device, &fci, nullptr, &m_offFB[f]) != VK_SUCCESS) return false;
	}
	return true;
}

void VkRhi::destroyOffscreen() {
	for (int f = 0; f < kFramesInFlight; f++) {
		if (m_offFB[f]) { vkDestroyFramebuffer(m_device, m_offFB[f], nullptr); m_offFB[f] = VK_NULL_HANDLE; }
		if (m_offColorView[f]) { vkDestroyImageView(m_device, m_offColorView[f], nullptr); m_offColorView[f] = VK_NULL_HANDLE; }
		if (m_offColor[f]) { vkDestroyImage(m_device, m_offColor[f], nullptr); m_offColor[f] = VK_NULL_HANDLE; }
		if (m_offColorMem[f]) { vkFreeMemory(m_device, m_offColorMem[f], nullptr); m_offColorMem[f] = VK_NULL_HANDLE; }
		if (m_offDepthView[f]) { vkDestroyImageView(m_device, m_offDepthView[f], nullptr); m_offDepthView[f] = VK_NULL_HANDLE; }
		if (m_offDepth[f]) { vkDestroyImage(m_device, m_offDepth[f], nullptr); m_offDepth[f] = VK_NULL_HANDLE; }
		if (m_offDepthMem[f]) { vkFreeMemory(m_device, m_offDepthMem[f], nullptr); m_offDepthMem[f] = VK_NULL_HANDLE; }
	}
}

bool VkRhi::createCompositeResources() {
	VkSamplerCreateInfo sci{};
	sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sci.magFilter = VK_FILTER_LINEAR;
	sci.minFilter = VK_FILTER_LINEAR;
	sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if (vkCreateSampler(m_device, &sci, nullptr, &m_linearSampler) != VK_SUCCESS) return false;
	sci.magFilter = VK_FILTER_NEAREST;
	sci.minFilter = VK_FILTER_NEAREST;
	if (vkCreateSampler(m_device, &sci, nullptr, &m_nearestSampler) != VK_SUCCESS) return false;

	// 2 samplers (color, depth) + 1 UBO (RenderTuning).
	VkDescriptorSetLayoutBinding binds[3]{};
	binds[0].binding = 0;
	binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binds[0].descriptorCount = 1;
	binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	binds[1].binding = 1;
	binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binds[1].descriptorCount = 1;
	binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	binds[2].binding = 2;
	binds[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	binds[2].descriptorCount = 1;
	binds[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo slci{};
	slci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	slci.bindingCount = 3;
	slci.pBindings = binds;
	if (vkCreateDescriptorSetLayout(m_device, &slci, nullptr, &m_compSetLayout) != VK_SUCCESS) return false;

	// Pool: samplers + UBO per frame-in-flight.
	VkDescriptorPoolSize poolSizes[2]{
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)(kFramesInFlight * 2)},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         (uint32_t)(kFramesInFlight * 1)},
	};
	VkDescriptorPoolCreateInfo dpci{};
	dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.maxSets = kFramesInFlight;
	dpci.poolSizeCount = 2;
	dpci.pPoolSizes = poolSizes;
	if (vkCreateDescriptorPool(m_device, &dpci, nullptr, &m_compDescPool) != VK_SUCCESS) return false;

	// Persistent-mapped RenderTuning UBO; rewritten each frame pre-draw.
	for (int f = 0; f < kFramesInFlight; f++) {
		VkBufferCreateInfo bci{};
		bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bci.size = sizeof(GradingParams);
		bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(m_device, &bci, nullptr, &m_gradUbo[f]) != VK_SUCCESS) return false;
		VkMemoryRequirements mr;
		vkGetBufferMemoryRequirements(m_device, m_gradUbo[f], &mr);
		VkMemoryAllocateInfo mai{};
		mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		mai.allocationSize = mr.size;
		mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (vkAllocateMemory(m_device, &mai, nullptr, &m_gradUboMem[f]) != VK_SUCCESS) return false;
		vkBindBufferMemory(m_device, m_gradUbo[f], m_gradUboMem[f], 0);
		vkMapMemory(m_device, m_gradUboMem[f], 0, mr.size, 0, &m_gradUboMapped[f]);
		GradingParams initial = GradingParams::Vivid();
		memcpy(m_gradUboMapped[f], &initial, sizeof(initial));
	}

	VkDescriptorSetLayout layouts[kFramesInFlight];
	for (int i = 0; i < kFramesInFlight; i++) layouts[i] = m_compSetLayout;
	VkDescriptorSetAllocateInfo dsai{};
	dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsai.descriptorPool = m_compDescPool;
	dsai.descriptorSetCount = kFramesInFlight;
	dsai.pSetLayouts = layouts;
	if (vkAllocateDescriptorSets(m_device, &dsai, m_compDescSet) != VK_SUCCESS) return false;

	updateCompositeDescriptors();

	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pcr.size = 128;
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1;
	plci.pSetLayouts = &m_compSetLayout;
	plci.pushConstantRangeCount = 1;
	plci.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_compLayout) != VK_SUCCESS) return false;

	auto vsCode = readFile("shaders/vk/composite.vert.spv");
	auto fsCode = readFile("shaders/vk/composite.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo vpSt{};
	vpSt.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vpSt.viewportCount = 1; vpSt.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo rsSt{};
	rsSt.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rsSt.polygonMode = VK_POLYGON_MODE_FILL;
	rsSt.cullMode = VK_CULL_MODE_NONE;
	rsSt.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo msSt{};
	msSt.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	msSt.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineDepthStencilStateCreateInfo dsSt{};
	dsSt.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	dsSt.depthTestEnable = VK_FALSE;
	dsSt.depthWriteEnable = VK_FALSE;
	VkPipelineColorBlendAttachmentState cba{};
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cbSt{};
	cbSt.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cbSt.attachmentCount = 1; cbSt.pAttachments = &cba;
	VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynSt{};
	dynSt.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynSt.dynamicStateCount = 2; dynSt.pDynamicStates = dyn;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vpSt;
	gpci.pRasterizationState = &rsSt;
	gpci.pMultisampleState = &msSt;
	gpci.pDepthStencilState = &dsSt;
	gpci.pColorBlendState = &cbSt;
	gpci.pDynamicState = &dynSt;
	gpci.layout = m_compLayout;
	gpci.renderPass = m_renderPass;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_compPipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

void VkRhi::updateCompositeDescriptors() {
	for (int f = 0; f < kFramesInFlight; f++) {
		VkDescriptorImageInfo colorInfo{};
		colorInfo.sampler = m_linearSampler;
		colorInfo.imageView = m_offColorView[f];
		colorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo depthInfo{};
		depthInfo.sampler = m_nearestSampler;
		depthInfo.imageView = m_offDepthView[f];
		depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorBufferInfo gradInfo{};
		gradInfo.buffer = m_gradUbo[f];
		gradInfo.offset = 0;
		gradInfo.range  = sizeof(GradingParams);

		VkWriteDescriptorSet writes[3]{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_compDescSet[f];
		writes[0].dstBinding = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].pImageInfo = &colorInfo;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_compDescSet[f];
		writes[1].dstBinding = 1;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].pImageInfo = &depthInfo;
		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = m_compDescSet[f];
		writes[2].dstBinding = 2;
		writes[2].descriptorCount = 1;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[2].pBufferInfo = &gradInfo;
		vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
	}
}

void VkRhi::setGrading(const GradingParams& g) {
	m_grading = g;
}

// Particle pipeline — additive billboards for magical effects.

bool VkRhi::createParticlePipeline() {
	auto vsCode = readFile("shaders/vk/particle.vert.spv");
	auto fsCode = readFile("shaders/vk/particle.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs; stages[1].pName = "main";

	// Per-instance only, no per-vertex VBO. 8 floats/particle.
	VkVertexInputBindingDescription bind{};
	bind.binding = 0; bind.stride = sizeof(float)*8; bind.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
	VkVertexInputAttributeDescription attrs[3]{};
	attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT; attrs[0].offset = 0;  // worldPos
	attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32_SFLOAT;       attrs[1].offset = sizeof(float)*3;  // size
	attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = sizeof(float)*4; // rgba

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
	vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	VkPipelineViewportStateCreateInfo vpState{};
	vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vpState.viewportCount = 1; vpState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_NONE;       // 2-sided billboards
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Test on (terrain occludes), write off (overlap blends naturally).
	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = VK_FALSE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;

	// Additive; frag shader pre-multiplies rgb*=alpha, so srcRGB=srcAlpha=ONE.
	VkPipelineColorBlendAttachmentState cba{};
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.colorBlendOp = VK_BLEND_OP_ADD;
	cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.alphaBlendOp = VK_BLEND_OP_ADD;
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1; cb.pAttachments = &cba;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	// PC: viewProj(mat4) + camRight(vec4) + camUp(vec4) = 96B.
	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pcr.size = sizeof(float) * (16 + 4 + 4);
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_particleLayout) != VK_SUCCESS)
		return false;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vpState;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_particleLayout;
	// Swapchain pass handle works here — offscreen pass is format-compatible.
	gpci.renderPass = m_renderPass;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_particlePipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

bool VkRhi::ensureParticleInstanceCapacity(int frame, VkDeviceSize bytes) {
	if (m_particleInstCap[frame] >= bytes) return true;

	// Defer destroy — earlier drawParticles in this frame may have bound
	// the old buffer to the current cmdbuf.
	if (m_particleInstMapped[frame]) { vkUnmapMemory(m_device, m_particleInstMem[frame]); m_particleInstMapped[frame] = nullptr; }
	if (m_particleInstBuf[frame] || m_particleInstMem[frame]) {
		m_pendingBufDestroy[frame].push_back({ m_particleInstBuf[frame], m_particleInstMem[frame] });
		m_particleInstBuf[frame] = VK_NULL_HANDLE;
		m_particleInstMem[frame] = VK_NULL_HANDLE;
	}

	VkDeviceSize cap = std::max<VkDeviceSize>(bytes, 64 * 1024);
	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = cap;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &m_particleInstBuf[frame]) != VK_SUCCESS) return false;

	VkMemoryRequirements req{};
	vkGetBufferMemoryRequirements(m_device, m_particleInstBuf[frame], &req);
	VkMemoryAllocateInfo ai{};
	ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = findMemType(req.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &ai, nullptr, &m_particleInstMem[frame]) != VK_SUCCESS) return false;
	vkBindBufferMemory(m_device, m_particleInstBuf[frame], m_particleInstMem[frame], 0);
	vkMapMemory(m_device, m_particleInstMem[frame], 0, cap, 0, &m_particleInstMapped[frame]);
	m_particleInstCap[frame] = cap;
	return true;
}

void VkRhi::drawParticles(const SceneParams& scene, const float* particles, uint32_t count) {
	if (!m_frameActive || !m_particlePipeline || count == 0) return;
	ensureMainPass();

	VkDeviceSize stride = (VkDeviceSize)sizeof(float) * 8;
	VkDeviceSize needBytes = (VkDeviceSize)(m_particleInstCount[m_frame] + count) * stride;
	if (!ensureParticleInstanceCapacity(m_frame, needBytes)) return;
	uint8_t* base = (uint8_t*)m_particleInstMapped[m_frame];
	memcpy(base + (VkDeviceSize)m_particleInstCount[m_frame] * stride,
		particles, (size_t)(count * stride));
	uint32_t firstInstance = m_particleInstCount[m_frame];
	m_particleInstCount[m_frame] += count;

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0,0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_particlePipeline);

	// Recover world-space camRight/camUp from VP rows. For an axis-aligned
	// perspective projection, VP row 0 (m[0], m[4], m[8]) is proportional to
	// world-space right; row 1 → up. Normalize to drop projection scale.
	float R[3], U[3];
	{
		const float* m = scene.viewProj;
		R[0] = m[0]; R[1] = m[4]; R[2] = m[8];
		U[0] = m[1]; U[1] = m[5]; U[2] = m[9];
		auto norm = [](float* v) {
			float l = sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
			if (l > 0) { v[0]/=l; v[1]/=l; v[2]/=l; }
		};
		norm(R); norm(U);
	}

	struct { float viewProj[16]; float camRight[4]; float camUp[4]; } pc{};
	memcpy(pc.viewProj, scene.viewProj, sizeof(float)*16);
	pc.camRight[0] = R[0]; pc.camRight[1] = R[1]; pc.camRight[2] = R[2]; pc.camRight[3] = 0;
	pc.camUp[0]    = U[0]; pc.camUp[1]    = U[1]; pc.camUp[2]    = U[2]; pc.camUp[3]    = 0;
	vkCmdPushConstants(cb, m_particleLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
		sizeof(pc), &pc);

	VkDeviceSize offs = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &m_particleInstBuf[m_frame], &offs);

	// 4 verts × count instances; firstInstance skips prior-call data.
	vkCmdDraw(cb, 4, count, 0, firstInstance);
}

// Ribbon pipeline — camera-facing additive trails.

bool VkRhi::createRibbonPipeline() {
	auto vsCode = readFile("shaders/vk/ribbon.vert.spv");
	auto fsCode = readFile("shaders/vk/ribbon.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs; stages[1].pName = "main";

	// 7 floats/vertex: pos[3] rgba[4].
	VkVertexInputBindingDescription bind{};
	bind.binding = 0; bind.stride = sizeof(float)*7; bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	VkVertexInputAttributeDescription attrs[2]{};
	attrs[0].location = 0; attrs[0].binding = 0;
	attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;    attrs[0].offset = 0;
	attrs[1].location = 1; attrs[1].binding = 0;
	attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[1].offset = sizeof(float)*3;

	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
	vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

	VkPipelineViewportStateCreateInfo vpState{};
	vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vpState.viewportCount = 1; vpState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_NONE;       // flat 2-sided
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Test on, write off — terrain occludes but no z-fight fingerprint.
	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = VK_FALSE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendAttachmentState cba{};
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.colorBlendOp = VK_BLEND_OP_ADD;
	cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.alphaBlendOp = VK_BLEND_OP_ADD;
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1; cb.pAttachments = &cba;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	// PC: viewProj only.
	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pcr.size = sizeof(float) * 16;
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_ribbonLayout) != VK_SUCCESS)
		return false;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vpState;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_ribbonLayout;
	gpci.renderPass = m_renderPass;

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_ribbonPipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

bool VkRhi::ensureRibbonVertexCapacity(int frame, VkDeviceSize bytes) {
	if (m_ribbonVtxCap[frame] >= bytes) return true;

	// Defer destroy — earlier drawRibbon in this frame may have bound the
	// old buffer to the current cmdbuf.
	if (m_ribbonVtxMapped[frame]) { vkUnmapMemory(m_device, m_ribbonVtxMem[frame]); m_ribbonVtxMapped[frame] = nullptr; }
	if (m_ribbonVtxBuf[frame] || m_ribbonVtxMem[frame]) {
		m_pendingBufDestroy[frame].push_back({ m_ribbonVtxBuf[frame], m_ribbonVtxMem[frame] });
		m_ribbonVtxBuf[frame] = VK_NULL_HANDLE;
		m_ribbonVtxMem[frame] = VK_NULL_HANDLE;
	}

	VkDeviceSize cap = bytes < 4096 ? 4096 : bytes * 2;
	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = cap;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &m_ribbonVtxBuf[frame]) != VK_SUCCESS) return false;

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(m_device, m_ribbonVtxBuf[frame], &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &m_ribbonVtxMem[frame]) != VK_SUCCESS) return false;
	vkBindBufferMemory(m_device, m_ribbonVtxBuf[frame], m_ribbonVtxMem[frame], 0);
	vkMapMemory(m_device, m_ribbonVtxMem[frame], 0, cap, 0, &m_ribbonVtxMapped[frame]);
	m_ribbonVtxCap[frame] = cap;
	return true;
}

void VkRhi::drawRibbon(const SceneParams& scene, const float* points, uint32_t pointCount) {
	if (!m_frameActive || !m_ribbonPipeline || pointCount < 2) return;
	ensureMainPass();

	// CPU expand: each point emits 2 verts offset ±width/2 along side =
	// tangent × viewDir (view-plane-aligned ribbon — flat toward camera).
	const float camX = scene.camPos[0], camY = scene.camPos[1], camZ = scene.camPos[2];
	const size_t vertStride = sizeof(float) * 7;
	const size_t vertCount = (size_t)pointCount * 2;
	const VkDeviceSize bytes = (VkDeviceSize)(vertCount * vertStride);

	VkDeviceSize needBytes = m_ribbonVtxCursor[m_frame] + bytes;
	if (!ensureRibbonVertexCapacity(m_frame, needBytes)) return;
	uint8_t* base = (uint8_t*)m_ribbonVtxMapped[m_frame] + m_ribbonVtxCursor[m_frame];
	VkDeviceSize drawOffset = m_ribbonVtxCursor[m_frame];
	m_ribbonVtxCursor[m_frame] = needBytes;

	auto sub3 = [](const float* a, const float* b, float* o) {
		o[0] = a[0]-b[0]; o[1] = a[1]-b[1]; o[2] = a[2]-b[2];
	};
	auto cross3 = [](const float* a, const float* b, float* o) {
		o[0] = a[1]*b[2] - a[2]*b[1];
		o[1] = a[2]*b[0] - a[0]*b[2];
		o[2] = a[0]*b[1] - a[1]*b[0];
	};
	auto norm3 = [](float* v) {
		float l = sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
		if (l > 1e-6f) { v[0]/=l; v[1]/=l; v[2]/=l; }
		else { v[0]=v[1]=v[2]=0.0f; }
	};

	float* dst = (float*)base;
	for (uint32_t i = 0; i < pointCount; i++) {
		const float* p = points + (size_t)i * 8;
		float pos[3] = { p[0], p[1], p[2] };
		float width = p[3];
		float rgba[4] = { p[4], p[5], p[6], p[7] };

		// Tangent: forward diff, with clamp at endpoints.
		float tangent[3];
		if (i + 1 < pointCount) {
			const float* pn = points + (size_t)(i + 1) * 8;
			sub3(pn, p, tangent);
		} else {
			const float* pp = points + (size_t)(i - 1) * 8;
			sub3(p, pp, tangent);
		}
		norm3(tangent);

		// View dir from point → camera.
		float view[3] = { camX - pos[0], camY - pos[1], camZ - pos[2] };
		norm3(view);

		// Side = tangent × view. If tangent ≈ view (degenerate — ribbon
		// pointing at camera), fall back to world up × tangent.
		float side[3];
		cross3(tangent, view, side);
		if (side[0]*side[0] + side[1]*side[1] + side[2]*side[2] < 1e-8f) {
			float up[3] = {0, 1, 0};
			cross3(tangent, up, side);
		}
		norm3(side);
		float hw = width * 0.5f;

		// Vertex A (side -hw) then Vertex B (side +hw) — triangle strip pair.
		dst[0] = pos[0] - side[0]*hw;
		dst[1] = pos[1] - side[1]*hw;
		dst[2] = pos[2] - side[2]*hw;
		dst[3] = rgba[0]; dst[4] = rgba[1]; dst[5] = rgba[2]; dst[6] = rgba[3];
		dst += 7;
		dst[0] = pos[0] + side[0]*hw;
		dst[1] = pos[1] + side[1]*hw;
		dst[2] = pos[2] + side[2]*hw;
		dst[3] = rgba[0]; dst[4] = rgba[1]; dst[5] = rgba[2]; dst[6] = rgba[3];
		dst += 7;
	}

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0,0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ribbonPipeline);
	vkCmdPushConstants(cb, m_ribbonLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
		sizeof(float)*16, scene.viewProj);

	VkDeviceSize offs = drawOffset;
	vkCmdBindVertexBuffers(cb, 0, 1, &m_ribbonVtxBuf[m_frame], &offs);

	vkCmdDraw(cb, (uint32_t)vertCount, 1, 0, 0);
}

// 2D UI / text pipeline. One-time: 512×192 R8 SDF atlas + font sampler set.
// Per-frame: persistent-mapped VB with byte cursor (reset in beginFrame),
// appended by each drawUi2D. Runs in the swapchain pass.

bool VkRhi::uploadFontAtlas() {
	std::vector<uint8_t> sdf;
	generateUiFontAtlas(sdf);

	// 512×192 R8 sampled image.
	VkImageCreateInfo ici{};
	ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ici.imageType = VK_IMAGE_TYPE_2D;
	ici.format = VK_FORMAT_R8_UNORM;
	ici.extent = { kFontAtlasW, kFontAtlasH, 1 };
	ici.mipLevels = 1; ici.arrayLayers = 1;
	ici.samples = VK_SAMPLE_COUNT_1_BIT;
	ici.tiling = VK_IMAGE_TILING_OPTIMAL;
	ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (vkCreateImage(m_device, &ici, nullptr, &m_fontImage) != VK_SUCCESS)
		return false;

	VkMemoryRequirements mr;
	vkGetImageMemoryRequirements(m_device, m_fontImage, &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &m_fontMem) != VK_SUCCESS) return false;
	vkBindImageMemory(m_device, m_fontImage, m_fontMem, 0);

	VkImageViewCreateInfo vci{};
	vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vci.image = m_fontImage;
	vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vci.format = VK_FORMAT_R8_UNORM;
	vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	if (vkCreateImageView(m_device, &vci, nullptr, &m_fontView) != VK_SUCCESS)
		return false;

	// Host-visible staging; one-shot cmd buf does the copy.
	VkBuffer staging = VK_NULL_HANDLE;
	VkDeviceMemory stagingMem = VK_NULL_HANDLE;
	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = sdf.size();
	bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &staging) != VK_SUCCESS) return false;

	VkMemoryRequirements sMr;
	vkGetBufferMemoryRequirements(m_device, staging, &sMr);
	VkMemoryAllocateInfo sMai{};
	sMai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	sMai.allocationSize = sMr.size;
	sMai.memoryTypeIndex = findMemType(sMr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &sMai, nullptr, &stagingMem) != VK_SUCCESS) return false;
	vkBindBufferMemory(m_device, staging, stagingMem, 0);

	void* mapped = nullptr;
	vkMapMemory(m_device, stagingMem, 0, sdf.size(), 0, &mapped);
	memcpy(mapped, sdf.data(), sdf.size());
	vkUnmapMemory(m_device, stagingMem);

	// One-shot cmd buf.
	VkCommandBufferAllocateInfo aci{};
	aci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	aci.commandPool = m_cmdPool;
	aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	aci.commandBufferCount = 1;
	VkCommandBuffer cb;
	if (vkAllocateCommandBuffers(m_device, &aci, &cb) != VK_SUCCESS) return false;

	VkCommandBufferBeginInfo cbi{};
	cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cb, &cbi);

	// UNDEFINED → TRANSFER_DST_OPTIMAL
	VkImageMemoryBarrier pre{};
	pre.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	pre.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	pre.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	pre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	pre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	pre.image = m_fontImage;
	pre.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	pre.srcAccessMask = 0;
	pre.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &pre);

	VkBufferImageCopy copy{};
	copy.bufferOffset = 0;
	copy.bufferRowLength = 0;
	copy.bufferImageHeight = 0;
	copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copy.imageOffset = { 0, 0, 0 };
	copy.imageExtent = { kFontAtlasW, kFontAtlasH, 1 };
	vkCmdCopyBufferToImage(cb, staging, m_fontImage,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

	// TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
	VkImageMemoryBarrier post = pre;
	post.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	post.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cb,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &post);

	vkEndCommandBuffer(cb);

	VkSubmitInfo si{};
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cb;
	vkQueueSubmit(m_gfxQueue, 1, &si, VK_NULL_HANDLE);
	vkQueueWaitIdle(m_gfxQueue);

	vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cb);
	vkDestroyBuffer(m_device, staging, nullptr);
	vkFreeMemory(m_device, stagingMem, nullptr);
	return true;
}

bool VkRhi::createUi2DResources() {
	if (!uploadFontAtlas()) return false;

	// Linear, clamp to edge — SDF handles edge blur; no cross-cell bleed.
	VkSamplerCreateInfo sci{};
	sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sci.magFilter = VK_FILTER_LINEAR;
	sci.minFilter = VK_FILTER_LINEAR;
	sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if (vkCreateSampler(m_device, &sci, nullptr, &m_fontSampler) != VK_SUCCESS)
		return false;

	// Set layout: one combined image sampler (frag).
	VkDescriptorSetLayoutBinding bind{};
	bind.binding = 0;
	bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bind.descriptorCount = 1;
	bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo slci{};
	slci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	slci.bindingCount = 1; slci.pBindings = &bind;
	if (vkCreateDescriptorSetLayout(m_device, &slci, nullptr, &m_uiSetLayout) != VK_SUCCESS)
		return false;

	VkDescriptorPoolSize poolSz = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
	VkDescriptorPoolCreateInfo dpci{};
	dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dpci.maxSets = 1;
	dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSz;
	if (vkCreateDescriptorPool(m_device, &dpci, nullptr, &m_uiDescPool) != VK_SUCCESS)
		return false;

	VkDescriptorSetAllocateInfo dsai{};
	dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	dsai.descriptorPool = m_uiDescPool;
	dsai.descriptorSetCount = 1; dsai.pSetLayouts = &m_uiSetLayout;
	if (vkAllocateDescriptorSets(m_device, &dsai, &m_uiDescSet) != VK_SUCCESS)
		return false;

	VkDescriptorImageInfo ii{};
	ii.sampler = m_fontSampler;
	ii.imageView = m_fontView;
	ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet w{};
	w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	w.dstSet = m_uiDescSet;
	w.dstBinding = 0;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	w.pImageInfo = &ii;
	vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);

	auto vsCode = readFile("shaders/vk/text2d.vert.spv");
	auto fsCode = readFile("shaders/vk/text2d.frag.spv");
	if (vsCode.empty() || fsCode.empty()) return false;
	VkShaderModule vs = makeModule(m_device, vsCode);
	VkShaderModule fs = makeModule(m_device, fsCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vs; stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fs; stages[1].pName = "main";

	// 4 floats/vertex: pos.xy, uv.xy.
	VkVertexInputBindingDescription vbind{};
	vbind.binding = 0; vbind.stride = sizeof(float) * 4;
	vbind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	VkVertexInputAttributeDescription vattrs[2]{};
	vattrs[0].location = 0; vattrs[0].binding = 0;
	vattrs[0].format = VK_FORMAT_R32G32_SFLOAT; vattrs[0].offset = 0;
	vattrs[1].location = 1; vattrs[1].binding = 0;
	vattrs[1].format = VK_FORMAT_R32G32_SFLOAT; vattrs[1].offset = sizeof(float) * 2;
	VkPipelineVertexInputStateCreateInfo vi{};
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vbind;
	vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = vattrs;

	VkPipelineInputAssemblyStateCreateInfo ia{};
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vpState{};
	vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vpState.viewportCount = 1; vpState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rs{};
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_NONE;
	rs.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo ms{};
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// UI draws on top of composite — no depth.
	VkPipelineDepthStencilStateCreateInfo ds{};
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	ds.depthTestEnable = VK_FALSE;
	ds.depthWriteEnable = VK_FALSE;

	// Standard alpha blend (non-premul).
	VkPipelineColorBlendAttachmentState cba{};
	cba.blendEnable = VK_TRUE;
	cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	cba.colorBlendOp = VK_BLEND_OP_ADD;
	cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	cba.alphaBlendOp = VK_BLEND_OP_ADD;
	cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
	                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo cb{};
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1; cb.pAttachments = &cba;

	VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynState{};
	dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynState.dynamicStateCount = 2; dynState.pDynamicStates = dyn;

	// PC: { vec4 color; ivec4 mode } = 32B.
	VkPushConstantRange pcr{};
	pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pcr.size = 32;
	VkPipelineLayoutCreateInfo plci{};
	plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	plci.setLayoutCount = 1; plci.pSetLayouts = &m_uiSetLayout;
	plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
	if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_uiLayout) != VK_SUCCESS)
		return false;

	VkGraphicsPipelineCreateInfo gpci{};
	gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gpci.stageCount = 2; gpci.pStages = stages;
	gpci.pVertexInputState = &vi;
	gpci.pInputAssemblyState = &ia;
	gpci.pViewportState = &vpState;
	gpci.pRasterizationState = &rs;
	gpci.pMultisampleState = &ms;
	gpci.pDepthStencilState = &ds;
	gpci.pColorBlendState = &cb;
	gpci.pDynamicState = &dynState;
	gpci.layout = m_uiLayout;
	gpci.renderPass = m_renderPass;   // swapchain pass

	VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci,
		nullptr, &m_uiPipeline);
	vkDestroyShaderModule(m_device, vs, nullptr);
	vkDestroyShaderModule(m_device, fs, nullptr);
	return r == VK_SUCCESS;
}

bool VkRhi::ensureUi2DVertexCapacity(int frame, VkDeviceSize bytes) {
	if (m_uiVtxCap[frame] >= bytes) return true;

	// Old buffer may already be bound this frame — queue for deferred destroy
	// at next beginFrame(frame).
	if (m_uiVtxMapped[frame]) { vkUnmapMemory(m_device, m_uiVtxMem[frame]); m_uiVtxMapped[frame] = nullptr; }
	if (m_uiVtxBuf[frame] || m_uiVtxMem[frame]) {
		m_pendingBufDestroy[frame].push_back({ m_uiVtxBuf[frame], m_uiVtxMem[frame] });
		m_uiVtxBuf[frame] = VK_NULL_HANDLE;
		m_uiVtxMem[frame] = VK_NULL_HANDLE;
	}

	VkDeviceSize cap = bytes < 4096 ? 4096 : bytes * 2;
	VkBufferCreateInfo bci{};
	bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bci.size = cap;
	bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (vkCreateBuffer(m_device, &bci, nullptr, &m_uiVtxBuf[frame]) != VK_SUCCESS) return false;

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(m_device, m_uiVtxBuf[frame], &mr);
	VkMemoryAllocateInfo mai{};
	mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mai.allocationSize = mr.size;
	mai.memoryTypeIndex = findMemType(mr.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	if (vkAllocateMemory(m_device, &mai, nullptr, &m_uiVtxMem[frame]) != VK_SUCCESS) return false;
	vkBindBufferMemory(m_device, m_uiVtxBuf[frame], m_uiVtxMem[frame], 0);
	vkMapMemory(m_device, m_uiVtxMem[frame], 0, cap, 0, &m_uiVtxMapped[frame]);
	m_uiVtxCap[frame] = cap;
	return true;
}

void VkRhi::drawUi2D(const float* vertsPosUV, uint32_t vertCount,
                     int mode, const float rgba[4]) {
	if (!m_frameActive || !m_uiPipeline || vertCount == 0) return;

	// Idempotent — safe to call many times per frame; only the first opens
	// the swapchain pass + composites the main pass into it.
	beginSwapchainPass();

	const VkDeviceSize vertStride = sizeof(float) * 4;
	const VkDeviceSize bytes = vertCount * vertStride;
	VkDeviceSize need = m_uiVtxCursor[m_frame] + bytes;
	if (!ensureUi2DVertexCapacity(m_frame, need)) return;

	uint8_t* base = (uint8_t*)m_uiVtxMapped[m_frame] + m_uiVtxCursor[m_frame];
	VkDeviceSize drawOffset = m_uiVtxCursor[m_frame];
	memcpy(base, vertsPosUV, (size_t)bytes);
	m_uiVtxCursor[m_frame] = need;

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0, 0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_uiPipeline);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		m_uiLayout, 0, 1, &m_uiDescSet, 0, nullptr);

	// Matches shader: { vec4 color; ivec4 mode }.
	struct PC { float color[4]; int mode; int _pad[3]; } pc{};
	pc.color[0] = rgba[0]; pc.color[1] = rgba[1];
	pc.color[2] = rgba[2]; pc.color[3] = rgba[3];
	pc.mode = mode;
	vkCmdPushConstants(cb, m_uiLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(pc), &pc);

	VkDeviceSize offs = drawOffset;
	vkCmdBindVertexBuffers(cb, 0, 1, &m_uiVtxBuf[m_frame], &offs);

	vkCmdDraw(cb, vertCount, 1, 0, 0);
}

void VkRhi::drawSky(const float invVP[16],
                    const float sunDir[3],
                    float sunStrength,
                    float time) {
	// All sky gradients derived in-shader from sunDir + sunStrength; time
	// drives star twinkle + cloud drift.
	if (!m_frameActive || !m_skyPipeline) return;
	ensureMainPass();
	VkCommandBuffer cb = m_cmdBufs[m_frame];

	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0,0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);

	struct {
		float invVP[16];
		float sunDir[4];     // xyz + sunStr
		float skyParams[4];  // x=timeSec, yzw reserved
	} pc;
	memcpy(pc.invVP, invVP, sizeof(float)*16);
	pc.sunDir[0] = sunDir[0]; pc.sunDir[1] = sunDir[1]; pc.sunDir[2] = sunDir[2];
	pc.sunDir[3] = sunStrength;
	pc.skyParams[0] = time;
	pc.skyParams[1] = 0.0f;
	pc.skyParams[2] = 0.0f;
	pc.skyParams[3] = 0.0f;
	vkCmdPushConstants(cb, m_skyLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		0, sizeof(pc), &pc);

	vkCmdDraw(cb, 3, 1, 0, 0);
}

void VkRhi::drawCube(const float mvp[16]) {
	if (!m_frameActive || !m_cubePipeline) return;
	ensureMainPass();
	VkCommandBuffer cb = m_cmdBufs[m_frame];

	VkViewport vp{};
	vp.width = (float)m_swapExtent.width;
	vp.height = (float)m_swapExtent.height;
	vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
	VkRect2D sc{{0,0}, m_swapExtent};
	vkCmdSetViewport(cb, 0, 1, &vp);
	vkCmdSetScissor(cb, 0, 1, &sc);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_cubePipeline);
	VkDeviceSize off = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &m_cubeVbo, &off);
	vkCmdPushConstants(cb, m_cubeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float)*16, mvp);
	vkCmdDraw(cb, m_cubeVertCount, 1, 0, 0);
}

bool VkRhi::createFramebuffers() {
	m_framebuffers.resize(m_swapViews.size());
	for (size_t i = 0; i < m_swapViews.size(); i++) {
		VkImageView atts[2] = { m_swapViews[i], m_depthView };
		VkFramebufferCreateInfo fci{};
		fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fci.renderPass = m_renderPass;
		fci.attachmentCount = 2;
		fci.pAttachments = atts;
		fci.width = m_swapExtent.width;
		fci.height = m_swapExtent.height;
		fci.layers = 1;
		if (vkCreateFramebuffer(m_device, &fci, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
			return false;
	}
	return true;
}

bool VkRhi::createCommandPool() {
	VkCommandPoolCreateInfo pci{};
	pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pci.queueFamilyIndex = m_gfxQueueFamily;
	if (vkCreateCommandPool(m_device, &pci, nullptr, &m_cmdPool) != VK_SUCCESS) return false;

	m_cmdBufs.resize(kFramesInFlight);
	VkCommandBufferAllocateInfo aci{};
	aci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	aci.commandPool = m_cmdPool;
	aci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	aci.commandBufferCount = kFramesInFlight;
	return vkAllocateCommandBuffers(m_device, &aci, m_cmdBufs.data()) == VK_SUCCESS;
}

bool VkRhi::createSyncObjects() {
	m_imgAvail.resize(kFramesInFlight);
	m_renderDone.resize(kFramesInFlight);
	m_inFlight.resize(kFramesInFlight);
	VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	for (int i = 0; i < kFramesInFlight; i++) {
		if (vkCreateSemaphore(m_device, &sci, nullptr, &m_imgAvail[i]) != VK_SUCCESS) return false;
		if (vkCreateSemaphore(m_device, &sci, nullptr, &m_renderDone[i]) != VK_SUCCESS) return false;
		if (vkCreateFence(m_device, &fci, nullptr, &m_inFlight[i]) != VK_SUCCESS) return false;
	}
	return true;
}

void VkRhi::destroySwapchain() {
	for (auto fb : m_framebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
	m_framebuffers.clear();
	for (auto v : m_swapViews) vkDestroyImageView(m_device, v, nullptr);
	m_swapViews.clear();
	if (m_swapchain) { vkDestroySwapchainKHR(m_device, m_swapchain, nullptr); m_swapchain = VK_NULL_HANDLE; }
}

bool VkRhi::recreateSwapchain() {
	int w = 0, h = 0;
	glfwGetFramebufferSize(m_window, &w, &h);
	while (w == 0 || h == 0) {
		glfwWaitEvents();
		glfwGetFramebufferSize(m_window, &w, &h);
	}
	m_width = w; m_height = h;
	vkDeviceWaitIdle(m_device);
	destroySwapchain();
	destroyDepth();
	destroyOffscreen();
	if (!createSwapchain()) return false;
	if (!createDepth()) return false;
	if (!createFramebuffers()) return false;
	if (!createOffscreen()) return false;
	updateCompositeDescriptors();
	return true;
}

void VkRhi::onResize(int w, int h) {
	m_width = w; m_height = h;
	m_resizePending = true;
}

bool VkRhi::beginFrame() {
	vkWaitForFences(m_device, 1, &m_inFlight[m_frame], VK_TRUE, UINT64_MAX);

	// Fence signalled → last submit at this frame index is retired; drain
	// all deferred-destroy queues (per-frame buffer replacements +
	// persistent meshes).
	for (auto& p : m_pendingBufDestroy[m_frame]) {
		if (p.buf) vkDestroyBuffer(m_device, p.buf, nullptr);
		if (p.mem) vkFreeMemory(m_device, p.mem, nullptr);
	}
	m_pendingBufDestroy[m_frame].clear();
	for (auto& p : m_meshPending[m_frame]) {
		if (p.buf) vkDestroyBuffer(m_device, p.buf, nullptr);
		if (p.mem) vkFreeMemory(m_device, p.mem, nullptr);
	}
	m_meshPending[m_frame].clear();
	for (auto& p : m_chunkMeshPending[m_frame]) {
		if (p.buf) vkDestroyBuffer(m_device, p.buf, nullptr);
		if (p.mem) vkFreeMemory(m_device, p.mem, nullptr);
	}
	m_chunkMeshPending[m_frame].clear();

	VkResult r = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
		m_imgAvail[m_frame], VK_NULL_HANDLE, &m_imageIndex);
	if (r == VK_ERROR_OUT_OF_DATE_KHR || m_resizePending) {
		m_resizePending = false;
		recreateSwapchain();
		return false;
	}
	if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;

	vkResetFences(m_device, 1, &m_inFlight[m_frame]);

	VkCommandBuffer cb = m_cmdBufs[m_frame];
	vkResetCommandBuffer(cb, 0);
	VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(cb, &bi);

	// No pass begun here — shadow pass may run before main. First lit draw
	// lazily opens main via ensureMainPass().
	m_frameActive = true;
	m_mainPassActive = false;
	m_shadowPassActive = false;
	m_swapchainPassActive = false;
	// Per-frame scratchpad cursors — each draw*() appends at current offset.
	m_boxInstCount[m_frame] = 0;
	m_particleInstCount[m_frame] = 0;
	m_ribbonVtxCursor[m_frame] = 0;
	m_uiVtxCursor[m_frame] = 0;
	return true;
}

void VkRhi::endFrame() {
	if (!m_frameActive) return;
	VkCommandBuffer cb = m_cmdBufs[m_frame];
	vkCmdEndRenderPass(cb);
	vkEndCommandBuffer(cb);

	VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo si{};
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.waitSemaphoreCount = 1;
	si.pWaitSemaphores = &m_imgAvail[m_frame];
	si.pWaitDstStageMask = &wait;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cb;
	si.signalSemaphoreCount = 1;
	si.pSignalSemaphores = &m_renderDone[m_frame];
	vkQueueSubmit(m_gfxQueue, 1, &si, m_inFlight[m_frame]);

	VkPresentInfoKHR pi{};
	pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = &m_renderDone[m_frame];
	pi.swapchainCount = 1;
	pi.pSwapchains = &m_swapchain;
	pi.pImageIndices = &m_imageIndex;
	VkResult r = vkQueuePresentKHR(m_gfxQueue, &pi);
	if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || m_resizePending) {
		m_resizePending = false;
		recreateSwapchain();
	}
	m_frame = (m_frame + 1) % kFramesInFlight;
	m_frameActive = false;
}

void VkRhi::beginSwapchainPass() {
	if (!m_frameActive || m_swapchainPassActive) return;

	VkCommandBuffer cb = m_cmdBufs[m_frame];

	// Open offscreen empty if menu-only frame (composite needs valid input).
	ensureMainPass();

	// End offscreen (→ SHADER_READ_ONLY); begin swapchain for composite + UI.
	vkCmdEndRenderPass(cb);
	m_mainPassActive = false;


	VkClearValue clears[2]{};
	clears[0].color = {{0, 0, 0, 1}};
	clears[1].depthStencil = {1.0f, 0};
	VkRenderPassBeginInfo rpi{};
	rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpi.renderPass = m_renderPass;
	rpi.framebuffer = m_framebuffers[m_imageIndex];
	rpi.renderArea.extent = m_swapExtent;
	rpi.clearValueCount = 2;
	rpi.pClearValues = clears;
	vkCmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);

	// Composite fullscreen quad: SSAO + bloom + tone map.
	if (m_compPipeline) {
		VkViewport vp{};
		vp.width = (float)m_swapExtent.width;
		vp.height = (float)m_swapExtent.height;
		vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
		VkRect2D sc{{0, 0}, m_swapExtent};
		vkCmdSetViewport(cb, 0, 1, &vp);
		vkCmdSetScissor(cb, 0, 1, &sc);

		// Stamp RenderTuning UBO from latest setGrading() before shader reads.
		if (m_gradUboMapped[m_frame]) {
			memcpy(m_gradUboMapped[m_frame], &m_grading, sizeof(GradingParams));
		}

		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_compPipeline);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_compLayout, 0, 1, &m_compDescSet[m_frame], 0, nullptr);
		vkCmdPushConstants(cb, m_compLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(m_compPC), &m_compPC);
		vkCmdDraw(cb, 3, 1, 0, 0);
	}

	m_swapchainPassActive = true;
}

void VkRhi::shutdown() {
	if (m_device) vkDeviceWaitIdle(m_device);
	for (int i = 0; i < kFramesInFlight; i++) {
		if (m_imgAvail.size() > (size_t)i) vkDestroySemaphore(m_device, m_imgAvail[i], nullptr);
		if (m_renderDone.size() > (size_t)i) vkDestroySemaphore(m_device, m_renderDone[i], nullptr);
		if (m_inFlight.size() > (size_t)i) vkDestroyFence(m_device, m_inFlight[i], nullptr);
	}
	if (m_compPipeline) vkDestroyPipeline(m_device, m_compPipeline, nullptr);
	if (m_compLayout) vkDestroyPipelineLayout(m_device, m_compLayout, nullptr);
	if (m_compDescPool) vkDestroyDescriptorPool(m_device, m_compDescPool, nullptr);
	if (m_compSetLayout) vkDestroyDescriptorSetLayout(m_device, m_compSetLayout, nullptr);
	for (int f = 0; f < kFramesInFlight; f++) {
		if (m_gradUboMapped[f]) vkUnmapMemory(m_device, m_gradUboMem[f]);
		if (m_gradUbo[f])       vkDestroyBuffer(m_device, m_gradUbo[f], nullptr);
		if (m_gradUboMem[f])    vkFreeMemory(m_device, m_gradUboMem[f], nullptr);
	}
	if (m_linearSampler) vkDestroySampler(m_device, m_linearSampler, nullptr);
	if (m_nearestSampler) vkDestroySampler(m_device, m_nearestSampler, nullptr);
	destroyOffscreen();
	if (m_offRenderPass) vkDestroyRenderPass(m_device, m_offRenderPass, nullptr);
	if (m_skyPipeline) vkDestroyPipeline(m_device, m_skyPipeline, nullptr);
	if (m_skyLayout) vkDestroyPipelineLayout(m_device, m_skyLayout, nullptr);
	if (m_boxModelPipeline) vkDestroyPipeline(m_device, m_boxModelPipeline, nullptr);
	for (int f = 0; f < kFramesInFlight; f++) {
		if (m_boxInstMapped[f]) vkUnmapMemory(m_device, m_boxInstMem[f]);
		if (m_boxInstBuf[f]) vkDestroyBuffer(m_device, m_boxInstBuf[f], nullptr);
		if (m_boxInstMem[f]) vkFreeMemory(m_device, m_boxInstMem[f], nullptr);
	}
	if (m_particlePipeline) vkDestroyPipeline(m_device, m_particlePipeline, nullptr);
	if (m_particleLayout) vkDestroyPipelineLayout(m_device, m_particleLayout, nullptr);
	for (int f = 0; f < kFramesInFlight; f++) {
		if (m_particleInstMapped[f]) vkUnmapMemory(m_device, m_particleInstMem[f]);
		if (m_particleInstBuf[f]) vkDestroyBuffer(m_device, m_particleInstBuf[f], nullptr);
		if (m_particleInstMem[f]) vkFreeMemory(m_device, m_particleInstMem[f], nullptr);
	}
	if (m_ribbonPipeline) vkDestroyPipeline(m_device, m_ribbonPipeline, nullptr);
	if (m_ribbonLayout) vkDestroyPipelineLayout(m_device, m_ribbonLayout, nullptr);
	for (int f = 0; f < kFramesInFlight; f++) {
		if (m_ribbonVtxMapped[f]) vkUnmapMemory(m_device, m_ribbonVtxMem[f]);
		if (m_ribbonVtxBuf[f]) vkDestroyBuffer(m_device, m_ribbonVtxBuf[f], nullptr);
		if (m_ribbonVtxMem[f]) vkFreeMemory(m_device, m_ribbonVtxMem[f], nullptr);
	}
	// 2D UI / text resources.
	if (m_uiPipeline) vkDestroyPipeline(m_device, m_uiPipeline, nullptr);
	if (m_uiLayout) vkDestroyPipelineLayout(m_device, m_uiLayout, nullptr);
	if (m_uiDescPool) vkDestroyDescriptorPool(m_device, m_uiDescPool, nullptr);
	if (m_uiSetLayout) vkDestroyDescriptorSetLayout(m_device, m_uiSetLayout, nullptr);
	if (m_fontSampler) vkDestroySampler(m_device, m_fontSampler, nullptr);
	if (m_fontView) vkDestroyImageView(m_device, m_fontView, nullptr);
	if (m_fontImage) vkDestroyImage(m_device, m_fontImage, nullptr);
	if (m_fontMem) vkFreeMemory(m_device, m_fontMem, nullptr);
	for (int f = 0; f < kFramesInFlight; f++) {
		if (m_uiVtxMapped[f]) vkUnmapMemory(m_device, m_uiVtxMem[f]);
		if (m_uiVtxBuf[f]) vkDestroyBuffer(m_device, m_uiVtxBuf[f], nullptr);
		if (m_uiVtxMem[f]) vkFreeMemory(m_device, m_uiVtxMem[f], nullptr);
		// Drain final-frame deferred-destroy queues (safe after WaitIdle).
		for (auto& p : m_pendingBufDestroy[f]) {
			if (p.buf) vkDestroyBuffer(m_device, p.buf, nullptr);
			if (p.mem) vkFreeMemory(m_device, p.mem, nullptr);
		}
		m_pendingBufDestroy[f].clear();
		// Persistent mesh pending queues for this frame index.
		for (auto& p : m_meshPending[f]) {
			if (p.buf) vkDestroyBuffer(m_device, p.buf, nullptr);
			if (p.mem) vkFreeMemory(m_device, p.mem, nullptr);
		}
		m_meshPending[f].clear();
		for (auto& p : m_chunkMeshPending[f]) {
			if (p.buf) vkDestroyBuffer(m_device, p.buf, nullptr);
			if (p.mem) vkFreeMemory(m_device, p.mem, nullptr);
		}
		m_chunkMeshPending[f].clear();
	}
	// Leaked meshes (caller skipped destroyMesh) — release directly.
	for (auto& kv : m_meshes) {
		if (kv.second.buf) vkDestroyBuffer(m_device, kv.second.buf, nullptr);
		if (kv.second.mem) vkFreeMemory(m_device, kv.second.mem, nullptr);
	}
	m_meshes.clear();
	for (auto& kv : m_chunkMeshes) {
		if (kv.second.buf) vkDestroyBuffer(m_device, kv.second.buf, nullptr);
		if (kv.second.mem) vkFreeMemory(m_device, kv.second.mem, nullptr);
	}
	m_chunkMeshes.clear();
	if (m_chunkPipelineOpaque) vkDestroyPipeline(m_device, m_chunkPipelineOpaque, nullptr);
	if (m_chunkPipelineTransparent) vkDestroyPipeline(m_device, m_chunkPipelineTransparent, nullptr);
	if (m_chunkLayout) vkDestroyPipelineLayout(m_device, m_chunkLayout, nullptr);
	if (m_voxelPipeline) vkDestroyPipeline(m_device, m_voxelPipeline, nullptr);
	if (m_voxelLayout) vkDestroyPipelineLayout(m_device, m_voxelLayout, nullptr);
	// Shadow resources share voxelSetLayout/descPool → destroy after pipelines.
	destroyShadowResources();
	if (m_instBuf) vkDestroyBuffer(m_device, m_instBuf, nullptr);
	if (m_instMem) vkFreeMemory(m_device, m_instMem, nullptr);
	if (m_cubePipeline) vkDestroyPipeline(m_device, m_cubePipeline, nullptr);
	if (m_cubeLayout) vkDestroyPipelineLayout(m_device, m_cubeLayout, nullptr);
	if (m_cubeVbo) vkDestroyBuffer(m_device, m_cubeVbo, nullptr);
	if (m_cubeVboMem) vkFreeMemory(m_device, m_cubeVboMem, nullptr);
	if (m_cmdPool) vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
	destroyDepth();
	destroySwapchain();
	if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
	if (m_device) vkDestroyDevice(m_device, nullptr);
	if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	if (m_dbg) {
		auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
			vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
		if (fn) fn(m_instance, m_dbg, nullptr);
	}
	if (m_instance) vkDestroyInstance(m_instance, nullptr);
}

} // namespace civcraft::rhi
