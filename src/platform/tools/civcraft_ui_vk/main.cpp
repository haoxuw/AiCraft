// civcraft-ui-vk — Vulkan-native playable slice.
//
// Thin entry point. All game logic lives in game_vk.cpp; this file just
// owns the GLFW window + RHI instance and pumps one frame per loop iteration.
//
// The Game class owns its own input handling (it reads raw glfwGetKey /
// cursor pos inside runOneFrame), so this shell stays refreshingly small.

#include "client/rhi/rhi.h"
#include "client/rhi/rhi_vk.h"
#include "game_vk.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unistd.h>

namespace {

struct Shell {
	civcraft::rhi::IRhi* rhi = nullptr;
	civcraft::vk::Game*  game = nullptr;
};

void resizeCb(GLFWwindow* w, int width, int height) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->rhi) s->rhi->onResize(width, height);
}

} // namespace

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);

	// chdir to the directory containing the executable so shader paths
	// ("shaders/vk/...") resolve regardless of where the user invokes from.
	{
		std::string exe(argv[0]);
		auto slash = exe.rfind('/');
		if (slash != std::string::npos) {
			std::string dir = exe.substr(0, slash);
			if (chdir(dir.c_str()) == 0)
				printf("[vk] cwd -> %s\n", dir.c_str());
		}
	}

	bool noValidation = false;
	bool skipMenu    = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("civcraft-ui-vk - Vulkan-native playable slice\n\n"
			       "  --no-validation   Disable VK_LAYER_KHRONOS_validation\n"
			       "  --skip-menu       Jump straight into gameplay\n\n"
			       "In-game:\n"
			       "  WASD        move         LMB          attack\n"
			       "  SPACE       jump         Mouse move   look\n"
			       "  ESC         menu         R            respawn (when dead)\n"
			       "  F2          screenshot\n");
			return 0;
		}
		if (strcmp(argv[i], "--no-validation") == 0) noValidation = true;
		if (strcmp(argv[i], "--skip-menu")     == 0) skipMenu     = true;
	}

	if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
	if (!glfwVulkanSupported()) { fprintf(stderr, "no vulkan\n"); glfwTerminate(); return 1; }

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* win = glfwCreateWindow(1280, 800, "CivCraft (Vulkan)", nullptr, nullptr);
	if (!win) { glfwTerminate(); return 1; }

	int fbw = 0, fbh = 0;
	glfwGetFramebufferSize(win, &fbw, &fbh);

	std::unique_ptr<civcraft::rhi::IRhi> rhi(civcraft::rhi::createRhi());
	civcraft::rhi::InitInfo ii;
	ii.window = win; ii.width = fbw; ii.height = fbh;
	ii.appName = "civcraft-ui-vk";
	ii.enableValidation = !noValidation;
	if (!rhi->init(ii)) return 1;
	if (!rhi->initImGui()) return 1;

	civcraft::vk::Game game;
	if (!game.init(rhi.get(), win)) {
		fprintf(stderr, "game.init failed\n");
		return 1;
	}
	if (skipMenu) game.skipMenu();

	Shell shell{ rhi.get(), &game };
	glfwSetWindowUserPointer(win, &shell);
	glfwSetFramebufferSizeCallback(win, resizeCb);

	auto t0 = std::chrono::steady_clock::now();
	auto tPrev = t0;
	while (!glfwWindowShouldClose(win) && !game.shouldQuit()) {
		glfwPollEvents();
		auto tNow = std::chrono::steady_clock::now();
		float dt = std::chrono::duration<float>(tNow - tPrev).count();
		tPrev = tNow;
		// Clamp dt so a paused process (dragging the window, breakpoint)
		// doesn't fling the player across the map on resume.
		if (dt > 0.1f) dt = 0.1f;

		float wallTime = std::chrono::duration<float>(tNow - t0).count();

		game.runOneFrame(dt, wallTime);
	}

	game.shutdown();
	rhi->shutdown();
	glfwDestroyWindow(win);
	glfwTerminate();
	return 0;
}
