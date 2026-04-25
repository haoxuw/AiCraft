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
#include "agent/agent_client.h"
#include "debug/entity_log.h"
#include "debug/perf_registry.h"

#include <ctime>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
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

void scrollCb(GLFWwindow* w, double xoff, double yoff) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->game) s->game->onScroll(xoff, yoff);
}

void charCb(GLFWwindow* w, unsigned int codepoint) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->game) s->game->onChar(codepoint);
}

void keyCb(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->game) s->game->onKey(key, action);
}

// Ctrl-C in the terminal running `make game` used to orphan child processes
// (civcraft-server, llama-server) because the default SIGTERM/SIGINT handler
// kills us without running destructors. Flip a flag the main loop polls so
// we exit cleanly and Game::shutdown() reaps every child.
std::atomic<bool> g_sigQuit{false};
void onSignal(int) { g_sigQuit.store(true); }

} // namespace

int main(int argc, char** argv) {
	setvbuf(stdout, nullptr, _IONBF, 0);

	std::signal(SIGINT,  onSignal);
	std::signal(SIGTERM, onSignal);

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
	bool debugBehaviorMode = false;   // --debug-behavior: dump per-entity log list at exit
	std::string host = "127.0.0.1"; // --host: server hostname
	int  port = 0;                  // --port: server port (0 = spawn local)
	int  templateIndex = 0;         // --template: world template (default village)
	int  villagersOverride = 0;     // --villagers: override villager count (0 = leave template as-is)
	float simSpeed = 1.0f;          // --sim-speed: server sim multiplier (1=real, 4=4× faster)
	// --terminate-after: seconds; ≤0 = run forever. After the deadline the
	// main loop exits naturally — same path as the menu's Quit button — so
	// game.shutdown(), client perf dump, and server save all fire as usual.
	float terminateAfterSec = 0.0f;
	civcraft::AgentClient::Config agentCfg;  // DecidePacer cooldown knobs
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("civcraft-ui-vk - Vulkan-native CivCraft client\n\n"
			       "Always connects to civcraft-server. If --port is omitted, spawns\n"
			       "a local civcraft-server subprocess (village world, seed 42).\n\n"
			       "  --no-validation   Disable VK_LAYER_KHRONOS_validation\n"
			       "  --skip-menu       Jump straight into gameplay\n"
			       "  --log-only        Hidden window + echo events to stdout\n"
			       "  --host HOST       Server hostname (default 127.0.0.1)\n"
			       "  --port PORT       Server port (omit to spawn local server)\n"
			       "  --template N      World template (0=village 1=test_behaviors 4=test_chicken)\n"
			       "  --villagers N     Spawn N villagers (override template count; local server only)\n"
			       "  --sim-speed N     Sim-time multiplier (default 1; 4 = 4× faster; local server only)\n"
			       "  --terminate-after SEC  Self-quit after SEC seconds via the Quit-button path\n"
			       "                         (perf summary + save fire normally; ≤0 = run forever)\n"
			       "  --decide-base-cooldown SEC  Base Decide dispatch cooldown (default 0.10s)\n"
			       "  --decide-max-cooldown SEC   Max cooldown after repeated failures (default 10s)\n"
			       "  --decide-backoff-base N     Exponential backoff base (default 2.0)\n"
			       "  --debug-behavior  Isolated single-villager log-only smoke\n"
			       "                    (shorthand for --skip-menu --log-only --template 1\n"
			       "                     --villagers 1 --sim-speed 4). See\n"
			       "                     .claude/skills/testing-plan/SKILL.md\n\n"
			       "In-game:\n"
			       "  WASD        move         LMB          attack / click-to-move\n"
			       "  SPACE       jump         Mouse move   look\n"
			       "  V           cycle camera (FPS/TPS/RPG/RTS)\n"
			       "  H           handbook\n"
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
		else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) templateIndex = std::atoi(argv[++i]);
		else if (strcmp(argv[i], "--villagers") == 0 && i + 1 < argc) villagersOverride = std::atoi(argv[++i]);
		else if (strcmp(argv[i], "--sim-speed") == 0 && i + 1 < argc) simSpeed = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--terminate-after") == 0 && i + 1 < argc) terminateAfterSec = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--decide-base-cooldown") == 0 && i + 1 < argc)
			agentCfg.decideBaseCooldownSec = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--decide-max-cooldown") == 0 && i + 1 < argc)
			agentCfg.decideMaxCooldownSec = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--decide-backoff-base") == 0 && i + 1 < argc)
			agentCfg.decideBackoffBase = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--debug-behavior") == 0) {
			// Compose the isolated-villager debug preset. See the testing-plan
			// skill for what each piece buys you; this shorthand just keeps
			// the happy-path command short.
			skipMenu = true;
			logOnly = true;
			templateIndex = 1;         // test_behaviors (walled arena, dense trees)
			if (villagersOverride <= 0) villagersOverride = 1;
			if (simSpeed == 1.0f)       simSpeed = 4.0f;
			debugBehaviorMode = true;
		}
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

	// Spawn a civcraft-server (if --port absent) and connect over TCP, handing
	// the NetworkServer to Game BEFORE init() so chunks stream from the server
	// rather than being generated client-side.
	civcraft::AgentManager agentMgr;
	civcraft::LocalWorld localWorld;
	{
		civcraft::ArtifactRegistry artifacts;
		artifacts.loadAll("artifacts");
		localWorld.entityDefs().mergeArtifactTags(artifacts.livingTags());
		localWorld.entityDefs().applyLivingStats(artifacts.livingStats());
	}
	std::unique_ptr<civcraft::NetworkServer> net;
	{
		int connectPort = port;
		if (connectPort <= 0) {
			civcraft::AgentManager::Config cfg;
			cfg.seed = 42;
			cfg.templateIndex = templateIndex;
			cfg.execDir = execDir;
			cfg.villagersOverride = villagersOverride;
			cfg.simSpeed = simSpeed;
			connectPort = agentMgr.launchServer(cfg);
			if (connectPort < 0) {
				fprintf(stderr, "[vk] failed to launch civcraft-server\n");
				return 1;
			}
		}
		net = std::make_unique<civcraft::NetworkServer>(host, connectPort, localWorld);
		// Handshake (C_HELLO) is deferred until the menu's character-select
		// completes — we need the chosen creatureType before sending HELLO.
		// --skip-menu calls Game::skipMenu() which connects immediately as
		// the server-default playable.
		printf("[vk] civcraft-server ready at %s:%d (awaiting character pick)\n",
		       host.c_str(), connectPort);
	}

	civcraft::vk::Game game;
	game.setServer(net.get());  // must precede init()
	game.setPendingConnect(42, templateIndex);  // seed+template used on character-select confirm
	game.setAgentConfig(agentCfg);              // DecidePacer knobs — must precede init()
	if (!game.init(rhi.get(), win)) {
		fprintf(stderr, "game.init failed\n");
		return 1;
	}
	if (skipMenu) game.skipMenu();

	Shell shell{ rhi.get(), &game };
	glfwSetWindowUserPointer(win, &shell);
	glfwSetFramebufferSizeCallback(win, resizeCb);
	glfwSetScrollCallback(win, scrollCb);
	glfwSetCharCallback(win, charCb);
	glfwSetKeyCallback(win, keyCb);

	// 200 fps ceiling — matches the swapchain's MAILBOX/IMMEDIATE mode so
	// render rate isn't pinned to vsync while also not melting the GPU on
	// idle menus. Cheap sleep loop; precise enough for a 5 ms budget.
	constexpr double kFrameBudgetSec = 1.0 / 200.0;
	auto t0 = std::chrono::steady_clock::now();
	auto tPrev = t0;
	bool terminationLogged = false;
	while (!glfwWindowShouldClose(win) && !game.shouldQuit() && !g_sigQuit.load()) {
		glfwPollEvents();
		auto tNow = std::chrono::steady_clock::now();
		float dt = std::chrono::duration<float>(tNow - tPrev).count();
		tPrev = tNow;
		// Clamp dt so pause/drag doesn't fling the player across the map on resume.
		if (dt > 0.1f) dt = 0.1f;

		float wallTime = std::chrono::duration<float>(tNow - t0).count();

		// --terminate-after deadline. Break out of the loop the same way
		// the Quit menu does so post-loop cleanup (perf dump, save) runs.
		if (terminateAfterSec > 0.0f && wallTime >= terminateAfterSec) {
			if (!terminationLogged) {
				std::fprintf(stderr,
					"[main] --terminate-after %.1fs reached; quitting cleanly.\n",
					terminateAfterSec);
				terminationLogged = true;
			}
			break;
		}

		game.runOneFrame(dt, wallTime);

		auto tAfter = std::chrono::steady_clock::now();
		double elapsed = std::chrono::duration<double>(tAfter - tNow).count();
		if (elapsed < kFrameBudgetSec) {
			std::this_thread::sleep_for(
				std::chrono::duration<double>(kFrameBudgetSec - elapsed));
		}
	}

	game.shutdown();
	if (net) net->disconnect();
	rhi->shutdown();
	glfwDestroyWindow(win);
	glfwTerminate();

	if (debugBehaviorMode) {
		// Flush any buffered "[first..last xN] msg" summaries to disk, then tell
		// the caller where to grep. See .claude/skills/testing-plan/SKILL.md.
		civcraft::entityLogFlushAll();
		auto files = civcraft::entityLogProducedFiles();
		std::fprintf(stderr, "\n[debug-behavior] produced per-entity logs (%zu):\n",
		             files.size());
		for (const auto& p : files) std::fprintf(stderr, "  %s\n", p.c_str());
	}

#ifdef CIVCRAFT_PERF
	// End-of-session client perf dump. Mirrors the server's format so
	// `make game` surfaces both after quit. Skipped if no Playing frame was
	// ever recorded (game.perfSessionStart() == 0) — saves menu-only runs
	// from printing an empty block.
	{
		double anchor = game.perfSessionStart();
		if (anchor > 0.0) {
			double elapsed = std::chrono::duration<double>(
				std::chrono::steady_clock::now().time_since_epoch()).count()
				- anchor;
			std::string summary = civcraft::perf::formatSummary(
				"CIVCRAFT CLIENT PERF", elapsed);

			auto [topName, topV] = civcraft::perf::topByP99({
				"client.phase.net",     "client.phase.chunks",
				"client.phase.agent",   "client.phase.events",
				"client.phase.sim",     "client.phase.gpuWait",
				"client.phase.world",   "client.phase.ents",
				"client.phase.fx3d",    "client.phase.inv3d",
				"client.phase.hud",     "client.phase.panels",
				"client.phase.present",
			});
			char blline[128] = "";
			if (!topName.empty()) {
				std::snprintf(blline, sizeof(blline),
					"── bottleneck (highest p99): %s = %.2f ms\n",
					topName.c_str(), topV);
			}

			std::fprintf(stderr, "\n%s%s\n", summary.c_str(), blline);

			char ts[32];
			std::time_t tt = std::time(nullptr);
			std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", std::localtime(&tt));
			char path[96];
			std::snprintf(path, sizeof(path),
				"/tmp/civcraft_perf_client_%s.txt", ts);
			if (FILE* f = std::fopen(path, "w")) {
				std::fputs(summary.c_str(), f);
				std::fputs(blline, f);
				std::fclose(f);
				std::fprintf(stderr, "[Perf] summary written to %s\n", path);
			}
		}
	}
#endif

	return 0;
}
