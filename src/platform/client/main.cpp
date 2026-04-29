// solarium-ui-vk entry point. Owns GLFW window + RHI; pumps one frame per loop.
// Game logic + input live in game_vk.cpp. If --port is omitted we spawn a local
// solarium-server and connect to it over TCP (singleplayer uses the same code
// path as multiplayer — Rule: no in-process shortcut).

#include "client/rhi/rhi.h"
#include "client/rhi/rhi_vk.h"
#include "client/ui/components.h"
#include "client/ui/pages.h"
#include "client/ui/action_router.h"
#include "client/game_logger.h"
#include "client/local_world.h"
#include "client/network_server.h"
#include "logic/artifact_registry.h"
#include "logic/world_templates.h"
#include "client/save_browser.h"
#include "client/process_manager.h"
#include "client/game_vk.h"
#include "client/cef_browser_host.h"
#include "client/cef_app.h"
#include "agent/agent_client.h"
#include "debug/entity_log.h"
#include "debug/perf_registry.h"

#include "include/cef_app.h"

#include <ctime>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

struct Shell {
	solarium::rhi::IRhi*       rhi = nullptr;
	solarium::vk::Game*        game = nullptr;
	solarium::vk::CefHost*     cef = nullptr;  // optional; null when --cef-menu absent
	// LMB=0x1, MMB=0x2, RMB=0x4 — passed in mouse-move modifiers so CEF
	// recognises a click-and-drag as a drag (otherwise the thumb of the
	// scrollbar can be clicked but never moved).
	uint32_t                   mouseButtonsHeld = 0;
};

void resizeCb(GLFWwindow* w, int width, int height) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->rhi) s->rhi->onResize(width, height);
}

void scrollCb(GLFWwindow* w, double xoff, double yoff) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (!s) return;
	// Route the wheel to the CEF overlay when it's active so the handbook
	// stat panel scrolls; otherwise fall through to the game (camera zoom).
	if (s->cef && s->game && s->game->cefMenuActive()) {
		double x, y; glfwGetCursorPos(w, &x, &y);
		// Chromium expects wheel deltas in "pixels of scroll" — match the
		// browser default of 100 px per notch (×3 for horizontal trackpads).
		s->cef->sendMouseWheel((int)x, (int)y,
			(int)(xoff * 100), (int)(yoff * 100));
		return;
	}
	if (s->game) s->game->onScroll(xoff, yoff);
}

void charCb(GLFWwindow* w, unsigned int codepoint) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->game) s->game->onChar(codepoint);
}

void keyCb(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->game) s->game->onKey(key, action);
}

// Route GLFW pointer input to CEF only while the overlay is active.
//
// Mouse-button state must be carried into every move event for drags
// (scrollbar thumb, slider) to work — Chromium uses the modifiers field
// to distinguish "hovering" from "dragging". Keep a tiny held-button
// bitmask on Shell and pass it through.
namespace {
inline int cefModifiersFor(uint32_t held) {
	int m = 0;
	if (held & 0x1) m |= 1 << 4;  // EVENTFLAG_LEFT_MOUSE_BUTTON
	if (held & 0x2) m |= 1 << 5;  // EVENTFLAG_MIDDLE_MOUSE_BUTTON
	if (held & 0x4) m |= 1 << 6;  // EVENTFLAG_RIGHT_MOUSE_BUTTON
	return m;
}
} // namespace

void cursorPosCb(GLFWwindow* w, double x, double y) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->cef && s->game && s->game->cefMenuActive())
		s->cef->sendMouseMove((int)x, (int)y, false,
			cefModifiersFor(s->mouseButtonsHeld));
}
void mouseBtnCb(GLFWwindow* w, int button, int action, int /*mods*/) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (!s || !s->cef || !s->game || !s->game->cefMenuActive()) return;
	int b = (button == GLFW_MOUSE_BUTTON_RIGHT) ? 2
	       : (button == GLFW_MOUSE_BUTTON_MIDDLE) ? 1 : 0;
	uint32_t bit = (b == 0) ? 0x1 : (b == 1) ? 0x2 : 0x4;
	if (action == GLFW_PRESS) s->mouseButtonsHeld |= bit;
	else                       s->mouseButtonsHeld &= ~bit;
	double x, y; glfwGetCursorPos(w, &x, &y);
	s->cef->sendMouseClick((int)x, (int)y, b, action == GLFW_PRESS, 1,
		cefModifiersFor(s->mouseButtonsHeld));
}

// Ctrl-C in the terminal running `make game` used to orphan child processes
// (solarium-server, llama-server) because the default SIGTERM/SIGINT handler
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
	// AgentManager find solarium-server once we've chdir'd.
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
	bool cefMenu     = false;         // --cef-menu: spin up an OSR CEF browser at startup
	std::string cefUrl;               // optional explicit URL override
	int  cefMouseX = -1, cefMouseY = -1;  // --cef-mouse=X,Y: synthetic hover after 4s
	int  cefClickX = -1, cefClickY = -1;  // --cef-click=X,Y: synthetic click 1s after hover
	// Toggled false by the action callback to dismiss the overlay so the user
	// sees the native menu/game underneath (demos the bridge controlling state).
	// (cefOverlayActive removed — game.cefMenuActive() is the sole gate.)
	std::string host = "127.0.0.1"; // --host: server hostname
	int  port = 0;                  // --port: server port (0 = spawn local)
	int  templateIndex = 0;         // --template: world template (default village)
	bool templateExplicit = false;  // true iff --template was passed on the command line
	int  debugMobCount = 0;         // --villagers / --mobs: cap each mob-type spawn count (0 = leave template as-is)
	float simSpeed = 1.0f;          // --sim-speed: server sim multiplier (1=real, 4=4× faster)
	// --terminate-after: seconds; ≤0 = run forever. After the deadline the
	// main loop exits naturally — same path as the menu's Quit button — so
	// game.shutdown(), client perf dump, and server save all fire as usual.
	float terminateAfterSec = 0.0f;
	// --auto-screenshot: drop a single screenshot to <path> as soon as CEF
	// has composited a few stable frames, then quit. The whole point is
	// headless smoke tests (src/tests/ui_smoke.sh) that boot one page,
	// capture proof, and tear down — no leftover process holding the cef
	// cache lock or hogging FDs across runs.
	std::string autoScreenshotPath;
	int         autoScreenshotMinFrames = 30;  // ~0.5 s at 60 FPS
	// --cam-pitch DEG: clamp camera lookPitch each frame so the screenshot
	// captures a tilted-down view. Headless polish only.
	bool        camPitchOn  = false;
	float       camPitchDeg = 0.0f;
	solarium::AgentClient::Config agentCfg;  // DecidePacer cooldown knobs
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("solarium-ui-vk - Vulkan-native Solarium client\n\n"
			       "Always connects to solarium-server. If --port is omitted, spawns\n"
			       "a local solarium-server subprocess (village world, seed 42).\n\n"
			       "  --no-validation   Disable VK_LAYER_KHRONOS_validation\n"
			       "  --skip-menu       Jump straight into gameplay\n"
			       "  --log-only        Hidden window + echo events to stdout\n"
			       "  --host HOST       Server hostname (default 127.0.0.1)\n"
			       "  --port PORT       Server port (omit to spawn local server)\n"
			       "  --template N      World template (0=village 1=test_behaviors 4=test_chicken)\n"
			       "  --villagers N     Spawn N villagers (override template count; local server only)\n"
			       "  --sim-speed N     Sim-time multiplier (default 1; 4 = 4× faster; local server only)\n"
			       "  --terminate-after SEC  Self-quit after SEC seconds via the Quit-button path\n"
		       "  --auto-screenshot PATH  Once CEF has composited, write a screenshot\n"
		       "                          to PATH and exit cleanly. Headless smoke-test\n"
		       "                          hook — pairs with SOLARIUM_BOOT_PAGE=<page>.\n"
		       "  --auto-screenshot-min-frames N  Frames to wait after first paint\n"
		       "                                  (default 30 ~ 0.5s).\n"
		       "  --cam-pitch DEG   Clamp camera lookPitch each frame (degrees;\n"
		       "                    -30 tilts down for cinematic frames).\n"
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
		else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) {
			templateIndex = std::atoi(argv[++i]);
			templateExplicit = true;
		}
		else if ((strcmp(argv[i], "--villagers") == 0 ||
		          strcmp(argv[i], "--mobs") == 0) && i + 1 < argc)
			debugMobCount = std::atoi(argv[++i]);
		else if (strcmp(argv[i], "--sim-speed") == 0 && i + 1 < argc) simSpeed = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--terminate-after") == 0 && i + 1 < argc) terminateAfterSec = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--auto-screenshot") == 0 && i + 1 < argc) autoScreenshotPath = argv[++i];
		else if (strcmp(argv[i], "--auto-screenshot-min-frames") == 0 && i + 1 < argc) autoScreenshotMinFrames = std::atoi(argv[++i]);
		else if (strcmp(argv[i], "--cam-pitch") == 0 && i + 1 < argc) {
			camPitchOn  = true;
			camPitchDeg = (float)std::atof(argv[++i]);
		}
		else if (strcmp(argv[i], "--decide-base-cooldown") == 0 && i + 1 < argc)
			agentCfg.decideBaseCooldownSec = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--decide-max-cooldown") == 0 && i + 1 < argc)
			agentCfg.decideMaxCooldownSec = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--decide-backoff-base") == 0 && i + 1 < argc)
			agentCfg.decideBackoffBase = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--cef-menu") == 0) cefMenu = true;
		else if (strcmp(argv[i], "--cef-url") == 0 && i + 1 < argc) {
			cefMenu = true;
			cefUrl = argv[++i];
		}
		else if (strcmp(argv[i], "--cef-mouse") == 0 && i + 1 < argc) {
			std::sscanf(argv[++i], "%d,%d", &cefMouseX, &cefMouseY);
		}
		else if (strcmp(argv[i], "--cef-click") == 0 && i + 1 < argc) {
			std::sscanf(argv[++i], "%d,%d", &cefClickX, &cefClickY);
		}
		else if (strcmp(argv[i], "--debug-behavior") == 0) {
			// Compose the isolated-villager debug preset. See the testing-plan
			// skill for what each piece buys you; this shorthand just keeps
			// the happy-path command short.
			skipMenu = true;
			logOnly = true;
			templateIndex = 1;         // test_behaviors (walled arena, dense trees)
			if (debugMobCount <= 0) debugMobCount = 1;
			if (simSpeed == 1.0f)       simSpeed = 4.0f;
			debugBehaviorMode = true;
		}
	}

	// CRITICAL: GameLogger init opens /tmp/solarium_game.log at FD 3. Chromium
	// expects FDs 3 and 4 to be free at CefInitialize so its child FD layout
	// (--field-trial-handle=3, --metrics-shmem-handle=4) lands at the right
	// slots. Holding FD 3 here causes the subprocess GlobalDescriptors lookup
	// to fail with "Failed global descriptor lookup: 7" and OnPaint never
	// fires. We move GameLogger init to AFTER CefInitialize.

	// FD-table snapshot before CefInitialize. Helps diagnose subprocess
	// "Failed global descriptor lookup" errors: the parent posix_spawns
	// children with dup2 actions on these FDs, and any unexpected open
	// file descriptor in this slot (3, 4, 7) breaks the handoff.
	{
		FILE* fp = std::fopen("/tmp/solarium_cef_parent_fds.txt", "w");
		if (fp) {
			std::fprintf(fp, "pid=%d\n", (int)getpid());
			DIR* d = opendir("/proc/self/fd");
			if (d) {
				while (struct dirent* e = readdir(d)) {
					if (e->d_name[0] == '.') continue;
					char src[128];
					std::snprintf(src, sizeof(src), "/proc/self/fd/%s", e->d_name);
					char dst[512] = "?";
					ssize_t n = readlink(src, dst, sizeof(dst) - 1);
					if (n >= 0) dst[n] = 0;
					std::fprintf(fp, "fd %s -> %s\n", e->d_name, dst);
				}
				closedir(d);
			}
			std::fclose(fp);
		}
	}

	// ── CEF init (must run before any GLFW/Vulkan init) ──────────────────
	// We initialize CEF unconditionally so the browser-host class can be
	// created on demand by gameplay code (handbook, dialog, future menus).
	// `--cef-menu` only gates whether we *create* a browser at startup.
	//
	// browser_subprocess_path points at our companion ./solarium-cef-subprocess
	// binary so renderer/GPU/utility children don't re-enter solarium-ui-vk
	// (which would re-init Python, GLFW, audio, the LLM sidecars).
	//
	// root_cache_path MUST be set to a unique per-app directory. Without it,
	// Chromium uses ~/.config/cef_user_data which collides with any other
	// CEF or Chromium instance — the second instance hits the user-data-dir
	// singleton lock and CefInitialize silently fails. We use the build dir.
	std::unique_ptr<solarium::vk::CefHost> cefHost;
	bool cefInitOk = false;
	{
		CefMainArgs cefMain(argc, argv);
		CefSettings cs;
		cs.no_sandbox = true;        // dev mode — skip chrome-sandbox SUID dance
		cs.windowless_rendering_enabled = true;
		// Let CEF own its UI thread. Single-threaded (false) requires us to
		// drive CefDoMessageLoopWork at a steady cadence — a pattern that
		// has produced zero OnPaint deliveries in our embedding for reasons
		// we haven't fully tracked down yet (likely FD-table contention with
		// GLFW/Vulkan/Python). Multi-threaded sidesteps that entirely.
		cs.multi_threaded_message_loop = true;
		cs.log_severity = LOGSEVERITY_WARNING;
		CefString(&cs.log_file) = "/tmp/solarium_cef.log";
		std::string subPath = execDir + "/solarium-cef-subprocess";
		CefString(&cs.browser_subprocess_path) = subPath;
		std::string cachePath = execDir + "/cef_cache";
		std::filesystem::create_directories(cachePath);
		CefString(&cs.root_cache_path) = cachePath;

		CefRefPtr<CefApp> app = solarium::vk::makeOsrApp();
		cefInitOk = CefInitialize(cefMain, cs, app,
		                          /*windows_sandbox_info=*/nullptr);
		if (!cefInitOk) {
			fprintf(stderr, "[cef] CefInitialize failed — continuing without CEF\n");
		} else {
			printf("[cef] initialized (subprocess=%s, cache=%s)\n",
			       subPath.c_str(), cachePath.c_str());
		}
	}

	// Now safe to open log files; CEF has already snapshotted the FD layout
	// it needs for child processes.
	solarium::GameLogger::instance().init(/*echoStdout=*/logOnly);

	if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }
	if (!glfwVulkanSupported()) { fprintf(stderr, "no vulkan\n"); glfwTerminate(); return 1; }

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	if (logOnly) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	GLFWwindow* win = glfwCreateWindow(1280, 800, "Solarium", nullptr, nullptr);
	if (!win) { glfwTerminate(); return 1; }

	int fbw = 0, fbh = 0;
	glfwGetFramebufferSize(win, &fbw, &fbh);

	std::unique_ptr<solarium::rhi::IRhi> rhi(solarium::rhi::createRhi());
	solarium::rhi::InitInfo ii;
	ii.window = win; ii.width = fbw; ii.height = fbh;
	ii.appName = "solarium-ui-vk";
	ii.enableValidation = !noValidation;
	if (!rhi->init(ii)) return 1;

	// Lazy server spawn: the NetworkServer is constructed with no target; the
	// menu flow calls Game::hostLocalServer (Singleplayer/Host) or Game::
	// joinRemoteServer (LAN row) to set host:port, then character pick fires
	// the HELLO handshake. The agent-side AgentManager lives inside Game so
	// quitting the client tears down whatever server we spawned.
	// Game now composites all three siblings: LocalWorldManager,
	// EntityManager, ModelManager. main.cpp wires NetworkServer with
	// refs INTO Game so artifact loading + entity-def population happens
	// in exactly one place (Game::init), no duplication with main.
	solarium::vk::Game game;
	auto net = std::make_unique<solarium::NetworkServer>(
		host, /*port=*/0, game.localWorld(), game.entityMgr());

	game.setServer(net.get());  // must precede init()
	game.setPendingConnect(42, templateIndex);  // overridden by hostLocalServer
	game.setAgentConfig(agentCfg);              // DecidePacer knobs — must precede init()
	game.setExecDir(execDir);                   // hostLocalServer needs it for the server-binary path
	if (camPitchOn) game.setForcedCameraPitch(camPitchDeg);
	if (!game.init(rhi.get(), win)) {
		fprintf(stderr, "game.init failed\n");
		return 1;
	}

	// Boot routing — three paths:
	//   1) --port N  → join a remote server at host:N, no local subprocess.
	//   2) --skip-menu → host locally with the CLI-supplied template
	//      (matches `make game`, `make test-*`).
	//   3) Default    → wait for the CEF menu to drive Singleplayer/Multiplayer.
	if (port > 0) {
		game.joinRemoteServer(host, port);
	}
	if (skipMenu) {
		// skipMenu requires a server target; if --port wasn't given, host one.
		if (port <= 0) {
			solarium::AgentManager::Config cfg;
			cfg.seed = 42;
			cfg.templateIndex = templateIndex;
			cfg.execDir = execDir;
			cfg.debugMobCount = debugMobCount;
			cfg.simSpeed = simSpeed;
			// Honour settings.lan_visible — fast-path users with LAN
			// hosting on get broadcast on UDP 7778 without the wizard.
			game.setNextHostLanVisible(game.settings().lan_visible);
			if (!game.hostLocalServer(cfg)) {
				fprintf(stderr, "[vk] failed to launch solarium-server\n");
				return 1;
			}
		}
		game.skipMenu();
	}
	if (logOnly) game.setLogOnly(true);

	// `--template N` means "boot straight into that world template" (this is
	// what `make test-dog`, etc. do). There's no main menu to
	// host in that flow, so suppress --cef-menu — otherwise CEF composites an
	// empty Chromium surface over the loading screen and the player sees what
	// looks like a stray browser pop-up. `make game` (no --template) still
	// gets the CEF main menu.
	if (templateExplicit && cefMenu) {
		std::printf("[cef] --template %d in effect; --cef-menu suppressed\n", templateIndex);
		cefMenu = false;
	}

	// Bring up the demo CEF browser if requested. We embed a tiny menu page
	// directly in a data: URL so the smoke path needs no server, no asset
	// staging, and no civ:// scheme handler — those come in phase 2.
	if (cefMenu && cefInitOk) {
		// Inline HTML for each menu page. CEF stays active across pages —
		// loadUrl() navigates between them rather than dismissing. Only when
		// the player picks a character (action "play:<id>") do we dismiss
		// the overlay so the loading screen / world becomes visible.
		// Common CSS now lives in client/ui/components.h. Reuses the same
		// All page builders + their action handlers live in
		// client/ui/pages.cpp. Build the static-page cache once at boot
		// (each call allocates ~50 KB of HTML); pages with live state
		// (saveSlots / multiplayer / settings / mods) are rebuilt per
		// click via solarium::ui::xxxPage(game) directly.
		static solarium::ui::PageCache pageCache(game);

		// Test hook: SOLARIUM_BOOT_PAGE=handbook|chars|settings|main lets a
		// dev land directly on a non-main page for screenshot iteration.
		// Useful when synthetic clicks are flaky (real cursor on the visible
		// window can race the click).
		if (cefUrl.empty()) {
			const char* boot = std::getenv("SOLARIUM_BOOT_PAGE");
			using MS = solarium::vk::MenuScreen;
			std::string b = boot ? boot : "";
			if (b == "handbook") {
				cefUrl = pageCache.handbook;
				game.setMenuScreen(MS::Handbook);
			} else if (b == "chars") {
				cefUrl = pageCache.chars;
				game.setMenuScreen(MS::CharacterSelect);
				game.setPreviewClip("wave");
				// Mirror what the "singleplayer" action does so beginConnectAs
				// has a previewId to commit when the user clicks Begin Game.
				for (auto* e : game.artifactRegistry().byCategory("living")) {
					if (e->flag("playable")) { game.setPreviewId(e->id); break; }
				}
				// Lazy spawn: char-select is reached, server needs to exist
				// for Begin Game to work. Skip if --port (joining remote).
				if (port <= 0) {
					solarium::AgentManager::Config cfg;
					cfg.seed = 42;
					cfg.templateIndex = templateIndex;
					cfg.execDir = execDir;
					cfg.debugMobCount = debugMobCount;
					cfg.simSpeed = simSpeed;
					if (!game.hostLocalServer(cfg))
						std::fprintf(stderr, "[boot] hostLocalServer failed\n");
				}
			} else if (b == "settings") cefUrl = solarium::ui::settingsPage(game);
			else if (b == "worlds")    cefUrl = pageCache.worldPicker;
			else if (b == "pause")     cefUrl = pageCache.pause;
			else if (b == "mp")        cefUrl = solarium::ui::multiplayerPage(game);
			else if (b == "saves")     cefUrl = solarium::ui::saveSlotsPage(game);
			else if (b == "mods")      cefUrl = solarium::ui::modManagerPage(game);
			else if (b == "lobby")     cefUrl = pageCache.lobby;
			else if (b == "death")     cefUrl = pageCache.death;
			else if (b == "editor") {
				// SOLARIUM_BOOT_EDIT=cat:id (default living:guy).
				const char* tgt = std::getenv("SOLARIUM_BOOT_EDIT");
				std::string spec = tgt ? tgt : "living:guy";
				auto col = spec.find(':');
				std::string url = (col == std::string::npos) ? "" :
					solarium::ui::editorUrlFor(game,
						spec.substr(0, col), spec.substr(col + 1));
				cefUrl = url.empty() ? pageCache.main : url;
			}
			else cefUrl = pageCache.main;
		}

		cefHost = std::make_unique<solarium::vk::CefHost>(fbw, fbh);
		// Don't force CEF on if --skip-menu already moved us into Connecting
		// — we want the user to see the loading-screen animation instead of
		// the title page composited over it.
		if (!skipMenu) game.setCefMenuActive(true);
		auto* hostRaw = cefHost.get();
		// Hand the CefHost + handbook URL to Game so the in-game H key (and
		// future ESC pause Settings) can re-show CEF over the running game.
		game.setCefHost(hostRaw);
		game.setCefHandbookUrl(pageCache.handbook);
		game.setCefPauseUrl(pageCache.pause);
		game.setCefDeathUrl(pageCache.death);
		game.setCefMainUrl(pageCache.main);
		// ── Action routing ──────────────────────────────────────────────
		// All 24 page-action handlers live in client/ui/pages.cpp so each
		// handler co-locates with the page it belongs to. main() just
		// builds the page cache, computes the boot-time defaults, and
		// hands them to ui::registerActions.
		const std::string firstPlayableId = [&]() -> std::string {
			for (auto* e : game.artifactRegistry().byCategory("living"))
				if (e->flag("playable")) return e->id;
			return {};
		}();
		static solarium::ui::ActionRouter router;
		solarium::ui::registerActions(router, game, hostRaw, win,
		                              pageCache, firstPlayableId);
		cefHost->setActionCallback([](const std::string& action) {
			std::printf("[cef] action: %s\n", action.c_str());
			if (!router.dispatch(action))
				std::fprintf(stderr, "[cef] unhandled action: %s\n", action.c_str());
		});
		if (!cefHost->start(cefUrl)) {
			fprintf(stderr, "[cef] host start failed — disabling --cef-menu\n");
			cefHost.reset();
		}
	}

	Shell shell{ rhi.get(), &game, cefHost.get() };
	glfwSetWindowUserPointer(win, &shell);
	glfwSetFramebufferSizeCallback(win, resizeCb);
	glfwSetScrollCallback(win, scrollCb);
	glfwSetCharCallback(win, charCb);
	glfwSetKeyCallback(win, keyCb);
	if (cefHost) {
		glfwSetCursorPosCallback(win, cursorPosCb);
		glfwSetMouseButtonCallback(win, mouseBtnCb);
	}

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

		// CEF: stage latest pixels while the overlay is active; otherwise
		// disable RHI overlay drawing. Single source of truth is
		// game.cefMenuActive() (atomic) — flipped by CEF action callback
		// (dismiss) and by native ESC handlers (re-show on return to Main).
		if (cefHost) {
			static int kick = 0;
			if (game.cefMenuActive()) {
				if (++kick % 30 == 0) cefHost->invalidate();
				static std::vector<uint8_t> cefBuf;
				int cw = 0, ch = 0;
				uint64_t f = cefHost->snapshotPixels(cefBuf, cw, ch);
				if (f > 0 && cw > 0 && ch > 0)
					rhi->blitCefImage(cefBuf.data(), cw, ch);

				// Synthetic input for headless verification.
				static bool didHover = false, didClick = false;
				if (!didHover && cefMouseX >= 0 && wallTime > 4.0f) {
					cefHost->sendMouseMove(cefMouseX, cefMouseY);
					cefHost->invalidate();
					didHover = true;
					std::printf("[cef] synth hover at (%d,%d)\n", cefMouseX, cefMouseY);
				}
				if (!didClick && cefClickX >= 0 && wallTime > 6.0f) {
					cefHost->sendMouseMove(cefClickX, cefClickY);
					cefHost->sendMouseClick(cefClickX, cefClickY, 0, true);
					cefHost->sendMouseClick(cefClickX, cefClickY, 0, false);
					didClick = true;
					std::printf("[cef] synth click at (%d,%d)\n", cefClickX, cefClickY);
				}
				if (didClick && kick % 5 == 0) cefHost->invalidate();
			} else {
				rhi->blitCefImage(nullptr, 0, 0);
			}
		}

		game.runOneFrame(dt, wallTime);

		// --auto-screenshot: queue a screenshot path on Game once enough
		// frames have rendered (CEF compositing if --cef-menu, plain
		// game frames otherwise). The in-
		// frame screenshot block inside game_vk.cpp consumes it (RHI
		// screenshot only works mid-frame). Once Game reports the shot
		// was taken, break the loop — game.shutdown() runs normally, no
		// leftover process. Used by src/tests/ui_smoke.sh.
		if (!autoScreenshotPath.empty()) {
			static int qualifyingFrames = 0;
			static bool requested = false;
			bool ready;
			if (cefHost && game.cefMenuActive()) {
				// CEF mode: wait for the overlay to actually paint.
				ready = (cefHost->frameCounter() > 0);
			} else if (game.state() == solarium::vk::GameState::Playing) {
				// Best case: in-world, post-loading.
				ready = true;
			} else {
				// Loading / Menu states still draw real frames; allow the
				// screenshot once the wall clock has had time to settle so
				// we capture the loading screen if the world's still
				// streaming.
				ready = (wallTime > 5.0f);
			}
			if (ready) ++qualifyingFrames;
			if (!requested && qualifyingFrames >= autoScreenshotMinFrames) {
				game.setPendingScreenshotPath(autoScreenshotPath);
				requested = true;
			}
			if (requested && game.consumeScreenshotTaken()) {
				std::fprintf(stderr,
					"[main] --auto-screenshot %s (after %d qualifying frames, state=%d)\n",
					autoScreenshotPath.c_str(), qualifyingFrames,
					(int)game.state());
				break;
			}
		}

		auto tAfter = std::chrono::steady_clock::now();
		double elapsed = std::chrono::duration<double>(tAfter - tNow).count();
		if (elapsed < kFrameBudgetSec) {
			std::this_thread::sleep_for(
				std::chrono::duration<double>(kFrameBudgetSec - elapsed));
		}
	}

	game.shutdown();
	if (net) net->disconnect();

	// Tear down the CEF browser, drain its message queue, then call CefShutdown.
	// Order matters: hosts must be gone (their CefBrowser refs released)
	// before CefShutdown or it will deadlock waiting for cleanup callbacks.
	if (cefHost) cefHost->shutdown();
	cefHost.reset();
	if (cefInitOk) {
		// Multi-threaded loop manages its own draining; just call shutdown.
		CefShutdown();
	}
	rhi->shutdown();
	glfwDestroyWindow(win);
	glfwTerminate();

	if (debugBehaviorMode) {
		// Flush any buffered "[first..last xN] msg" summaries to disk, then tell
		// the caller where to grep. See .claude/skills/testing-plan/SKILL.md.
		solarium::entityLogFlushAll();
		auto files = solarium::entityLogProducedFiles();
		std::fprintf(stderr, "\n[debug-behavior] produced per-entity logs (%zu):\n",
		             files.size());
		for (const auto& p : files) std::fprintf(stderr, "  %s\n", p.c_str());
	}

#ifdef SOLARIUM_PERF
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
			std::string summary = solarium::perf::formatSummary(
				"SOLARIUM CLIENT PERF", elapsed);

			auto [topName, topV] = solarium::perf::topByP99({
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
				"/tmp/solarium_perf_client_%s.txt", ts);
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
