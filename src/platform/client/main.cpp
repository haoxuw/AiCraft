// civcraft-ui-vk entry point. Owns GLFW window + RHI; pumps one frame per loop.
// Game logic + input live in game_vk.cpp. If --port is omitted we spawn a local
// civcraft-server and connect to it over TCP (singleplayer uses the same code
// path as multiplayer — Rule: no in-process shortcut).

#include "client/rhi/rhi.h"
#include "client/rhi/rhi_vk.h"
#include "client/game_logger.h"
#include "client/local_world.h"
#include "client/network_server.h"
#include "logic/artifact_registry.h"
#include "client/process_manager.h"
#include "client/game_vk.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
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

void focusCb(GLFWwindow* w, int focused) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->game) s->game->onWindowFocus(focused != 0);
}

void scrollCb(GLFWwindow* w, double xoff, double yoff) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->game) s->game->onScroll(xoff, yoff);
}


} // namespace

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);

	// Resolve execDir from argv[0] BEFORE chdir, then chdir into it so
	// shader paths resolve regardless of invocation cwd. execDir also lets
	// AgentManager find civcraft-server once we've chdir'd.
	std::string execDir;
	{
		std::string exe(argv[0]);
		auto slash = exe.rfind('/');
		if (slash != std::string::npos) {
			std::string dir = exe.substr(0, slash);
			if (chdir(dir.c_str()) == 0)
				printf("[vk] cwd -> %s\n", dir.c_str());
		}
		execDir = std::filesystem::current_path().string();
	}

	bool noValidation = false;
	bool skipMenu    = false;
	bool logOnly     = false;
	std::string host = "127.0.0.1"; // --host: server hostname
	int  port = 0;                  // --port: server port (0 = spawn local)
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("civcraft-ui-vk - Vulkan-native CivCraft client\n\n"
			       "Always connects to civcraft-server. If --port is omitted, spawns\n"
			       "a local civcraft-server subprocess (village world, seed 42).\n\n"
			       "  --no-validation   Disable VK_LAYER_KHRONOS_validation\n"
			       "  --skip-menu       Jump straight into gameplay\n"
			       "  --log-only        Hidden window + echo events to stdout\n"
			       "  --host HOST       Server hostname (default 127.0.0.1)\n"
			       "  --port PORT       Server port (omit to spawn local server)\n\n"
			       "In-game:\n"
			       "  WASD        move         LMB          attack / click-to-move\n"
			       "  SPACE       jump         Mouse move   look\n"
			       "  V           cycle camera (FPS/TPS/RPG/RTS)\n"
			       "  Tab         inventory    H            handbook\n"
			       "  F3          debug overlay             F2  screenshot\n"
			       "  ESC         menu         R            respawn (when dead)\n"
			       "  F12         admin        F11          fly (admin)\n"
			       "  RTS mode: drag-select units, click to move group\n");
			return 0;
		}
		if (strcmp(argv[i], "--no-validation") == 0) noValidation = true;
		else if (strcmp(argv[i], "--skip-menu") == 0) skipMenu = true;
		else if (strcmp(argv[i], "--log-only") == 0) { logOnly = true; skipMenu = true; }
		else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) host = argv[++i];
		else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = std::atoi(argv[++i]);
	}

	civcraft::GameLogger::instance().init(/*echoStdout=*/logOnly);

	if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
	if (!glfwVulkanSupported()) { fprintf(stderr, "no vulkan\n"); glfwTerminate(); return 1; }

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	if (logOnly) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
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

	// Spawn a civcraft-server (if --port absent) and connect over TCP, handing
	// the NetworkServer to Game BEFORE init() so chunks stream from the server
	// rather than being generated client-side.
	civcraft::AgentManager agentMgr;
	civcraft::LocalWorld localWorld;
	{
		civcraft::ArtifactRegistry artifacts;
		artifacts.loadAll("artifacts");
		localWorld.entityDefs().mergeArtifactTags(artifacts.livingTags());
	}
	std::unique_ptr<civcraft::NetworkServer> net;
	{
		int connectPort = port;
		if (connectPort <= 0) {
			civcraft::AgentManager::Config cfg;
			cfg.seed = 42;
			cfg.templateIndex = 1;  // village world
			cfg.execDir = execDir;
			connectPort = agentMgr.launchServer(cfg);
			if (connectPort < 0) {
				fprintf(stderr, "[vk] failed to launch civcraft-server\n");
				return 1;
			}
		}
		net = std::make_unique<civcraft::NetworkServer>(host, connectPort, localWorld);
		if (!net->createGame(42, 1)) {
			fprintf(stderr, "[vk] handshake failed (%s:%d)\n",
			        host.c_str(), connectPort);
			return 1;
		}
		printf("[vk] connected to civcraft-server %s:%d (player eid=%d)\n",
		       host.c_str(), connectPort, (int)net->localPlayerId());
	}

	civcraft::vk::Game game;
	game.setServer(net.get());  // must precede init()
	if (!game.init(rhi.get(), win)) {
		fprintf(stderr, "game.init failed\n");
		return 1;
	}
	if (skipMenu) game.skipMenu();

	Shell shell{ rhi.get(), &game };
	glfwSetWindowUserPointer(win, &shell);
	glfwSetFramebufferSizeCallback(win, resizeCb);
	glfwSetWindowFocusCallback(win, focusCb);
	glfwSetScrollCallback(win, scrollCb);

	auto t0 = std::chrono::steady_clock::now();
	auto tPrev = t0;
	while (!glfwWindowShouldClose(win) && !game.shouldQuit()) {
		glfwPollEvents();
		auto tNow = std::chrono::steady_clock::now();
		float dt = std::chrono::duration<float>(tNow - tPrev).count();
		tPrev = tNow;
		// Clamp dt so pause/drag doesn't fling the player across the map on resume.
		if (dt > 0.1f) dt = 0.1f;

		float wallTime = std::chrono::duration<float>(tNow - t0).count();

		game.runOneFrame(dt, wallTime);
	}

	game.shutdown();
	if (net) net->disconnect();
	rhi->shutdown();
	glfwDestroyWindow(win);
	glfwTerminate();
	return 0;
}
