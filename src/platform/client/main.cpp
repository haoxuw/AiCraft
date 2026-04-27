// solarium-ui-vk entry point. Owns GLFW window + RHI; pumps one frame per loop.
// Game logic + input live in game_vk.cpp. If --port is omitted we spawn a local
// solarium-server and connect to it over TCP (singleplayer uses the same code
// path as multiplayer — Rule: no in-process shortcut).

#include "client/rhi/rhi.h"
#include "client/rhi/rhi_vk.h"
#include "client/game_logger.h"
#include "client/local_world.h"
#include "client/network_server.h"
#include "logic/artifact_registry.h"
#include "logic/world_templates.h"
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
#include <functional>
#include <memory>
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

// Route GLFW pointer input to CEF only while the overlay is active.
void cursorPosCb(GLFWwindow* w, double x, double y) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (s && s->cef && s->game && s->game->cefMenuActive())
		s->cef->sendMouseMove((int)x, (int)y);
}
void mouseBtnCb(GLFWwindow* w, int button, int action, int /*mods*/) {
	auto* s = (Shell*)glfwGetWindowUserPointer(w);
	if (!s || !s->cef || !s->game || !s->game->cefMenuActive()) return;
	int b = (button == GLFW_MOUSE_BUTTON_RIGHT) ? 2
	       : (button == GLFW_MOUSE_BUTTON_MIDDLE) ? 1 : 0;
	double x, y; glfwGetCursorPos(w, &x, &y);
	s->cef->sendMouseClick((int)x, (int)y, b, action == GLFW_PRESS);
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
	int  villagersOverride = 0;     // --villagers: override villager count (0 = leave template as-is)
	float simSpeed = 1.0f;          // --sim-speed: server sim multiplier (1=real, 4=4× faster)
	// --terminate-after: seconds; ≤0 = run forever. After the deadline the
	// main loop exits naturally — same path as the menu's Quit button — so
	// game.shutdown(), client perf dump, and server save all fire as usual.
	float terminateAfterSec = 0.0f;
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
		else if (strcmp(argv[i], "--villagers") == 0 && i + 1 < argc) villagersOverride = std::atoi(argv[++i]);
		else if (strcmp(argv[i], "--sim-speed") == 0 && i + 1 < argc) simSpeed = (float)std::atof(argv[++i]);
		else if (strcmp(argv[i], "--terminate-after") == 0 && i + 1 < argc) terminateAfterSec = (float)std::atof(argv[++i]);
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
			if (villagersOverride <= 0) villagersOverride = 1;
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
	GLFWwindow* win = glfwCreateWindow(1280, 800, "Solarium (Vulkan)", nullptr, nullptr);
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
	solarium::LocalWorld localWorld;
	{
		solarium::ArtifactRegistry artifacts;
		artifacts.loadAll("artifacts");
		localWorld.entityDefs().mergeArtifactTags(artifacts.livingTags());
		localWorld.entityDefs().applyLivingStats(artifacts.livingStats());
	}
	auto net = std::make_unique<solarium::NetworkServer>(host, /*port=*/0, localWorld);

	solarium::vk::Game game;
	game.setServer(net.get());  // must precede init()
	game.setPendingConnect(42, templateIndex);  // overridden by hostLocalServer
	game.setAgentConfig(agentCfg);              // DecidePacer knobs — must precede init()
	game.setExecDir(execDir);                   // hostLocalServer needs it for the server-binary path
	if (!game.init(rhi.get(), win)) {
		fprintf(stderr, "game.init failed\n");
		return 1;
	}

	// Boot routing — three paths:
	//   1) --port N  → join a remote server at host:N, no local subprocess.
	//   2) --skip-menu → host locally with the CLI-supplied template
	//      (matches `make game`, `make toronto`, `make test-*`).
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
			cfg.villagersOverride = villagersOverride;
			cfg.simSpeed = simSpeed;
			if (!game.hostLocalServer(cfg)) {
				fprintf(stderr, "[vk] failed to launch solarium-server\n");
				return 1;
			}
		}
		game.skipMenu();
	}

	// `--template N` means "boot straight into that world template" (this is
	// what `make toronto`, `make test-dog`, etc. do). There's no main menu to
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
		const std::string kCss =
			"html,body{margin:0;height:100vh;background:transparent;"
			"color:%23f0e0c0;font-family:Georgia,serif;"
			"display:flex;flex-direction:column;align-items:center;justify-content:center}"
			"body{background:radial-gradient(ellipse at center,"
			"rgba(16,8,10,0.55) 0%%,rgba(16,8,10,0) 60%%)}"
			"h1{color:%23f3c44c;font-size:72px;letter-spacing:8px;margin:0 0 8px;"
			"text-shadow:0 4px 24px rgba(0,0,0,0.85),0 0 18px rgba(243,196,76,0.35)}"
			".tag{font-size:16px;letter-spacing:4px;opacity:0.85;margin:0 0 40px;"
			"text-transform:uppercase;text-shadow:0 2px 6px rgba(0,0,0,0.85)}"
			".btn{display:block;width:280px;margin:6px 0;padding:12px 0;font-size:18px;"
			"background:rgba(26,18,11,0.78);color:%23f3c44c;"
			"border:1px solid %23b88838;font-family:inherit;cursor:pointer;"
			"letter-spacing:2px;backdrop-filter:blur(2px);"
			"transition:background 0.15s,transform 0.15s}"
			".btn:hover{background:rgba(94,67,30,0.92);transform:scale(1.02)}"
			".back{margin-top:32px;width:160px;font-size:14px;opacity:0.85}"
			".version{position:fixed;bottom:20px;right:30px;font-size:13px;opacity:0.6;"
			"text-shadow:0 1px 3px rgba(0,0,0,0.85)}";
		const std::string kJs =
			"<script>function send(a){"
			"window.cefQuery({request:'action:'+a,onSuccess:()=>{},onFailure:()=>{}});"
			"}</script>";

		// Bottom-right version label, identical across every page.
		const std::string kVersion =
			"<div class='version'>v0.2.0 / CEF 146</div>";

		// HTML-escape the bare minimum: ", <, >, &, %, ', #. Pages are passed
		// as `data:text/html,<…>` so any '%' that isn't part of a percent-
		// escape will look malformed — URL-encode it explicitly. Use `&apos;`
		// (not `&#39;`) for the apostrophe so the HTML entity itself contains
		// no `#` — a bare `#` in the URL terminates the data: payload as a
		// fragment marker, truncating the script.
		auto enc = [](const std::string& s) -> std::string {
			std::string o; o.reserve(s.size() + 16);
			for (char c : s) {
				switch (c) {
					case '"': o += "&quot;"; break;
					case '<': o += "&lt;";   break;
					case '>': o += "&gt;";   break;
					case '&': o += "&amp;";  break;
					case '\'':o += "&apos;"; break;
					case '%': o += "%25";    break;  // data: URL escape
					case '#': o += "%23";    break;  // data: URL fragment guard
					default:  o += c;
				}
			}
			return o;
		};

		auto mainPage = [&]() -> std::string {
			return "data:text/html,<html><head><style>" + kCss +
				"</style></head><body>"
				"<h1>Solarium</h1>"
				"<div class='tag'>A voxel sandbox civilization</div>"
				"<button class='btn' onclick=\"send('singleplayer')\">Singleplayer</button>"
				"<button class='btn' onclick=\"send('multiplayer')\">Multiplayer</button>"
				"<button class='btn' onclick=\"send('handbook')\">Handbook</button>"
				"<button class='btn' onclick=\"send('settings')\">Settings</button>"
				"<button class='btn' onclick=\"send('quit')\">Quit</button>" +
				kVersion + kJs +
				"</body></html>";
		};

		// Pokédex / Civ6 Civilopedia chrome — shared by Character Select and
		// Handbook. Sidebar pinned to the left edge holds the list; the right
		// area is mostly transparent so the live plaza preview shows through,
		// with a translucent detail panel pinned to the bottom-right for the
		// selected entry's name + description + attribute table.
		// Hover or click on a row fires `pick:<id>` so the 3D preview swaps
		// in real time without a click commit.
		const std::string kDexCss =
			"body{display:block;align-items:initial;justify-content:initial;"
			"background:transparent;padding:0;margin:0;min-height:100vh;"
			"font-family:Georgia,serif;color:%23f0e0c0}"
			".sidebar{position:fixed;left:0;top:0;bottom:0;width:300px;"
			"background:rgba(16,8,10,0.88);border-right:1px solid %23b88838;"
			"display:flex;flex-direction:column;box-sizing:border-box}"
			".sb-head{padding:22px 22px 12px;border-bottom:1px solid rgba(184,136,56,0.4)}"
			".sb-head h1{font-size:24px;letter-spacing:4px;margin:0;color:%23f3c44c;"
			"font-weight:400;text-shadow:0 2px 8px rgba(0,0,0,0.8)}"
			".sb-head .sub{font-size:11px;letter-spacing:2px;color:%23b88838;"
			"text-transform:uppercase;margin-top:4px}"
			".sb-search{margin:14px 22px 8px;padding:8px 12px;width:auto;"
			"background:rgba(26,18,11,0.78);color:%23f0e0c0;border:1px solid %23b88838;"
			"font-family:inherit;font-size:13px;letter-spacing:1px;box-sizing:border-box}"
			".sb-list{flex:1;overflow-y:auto;padding:6px 0}"
			".sb-grp{padding:14px 22px 4px;font-size:11px;letter-spacing:3px;"
			"color:%23b88838;text-transform:uppercase}"
			".sb-row{display:block;padding:9px 22px;cursor:pointer;"
			"font-size:14px;letter-spacing:1px;color:%23f0e0c0;"
			"border-left:3px solid transparent;background:transparent;border-top:0;"
			"border-right:0;border-bottom:0;text-align:left;width:100%%;"
			"font-family:inherit}"
			".sb-row:hover{background:rgba(94,67,30,0.45);"
			"border-left-color:%23b88838;color:%23f3c44c}"
			".sb-row.on{background:rgba(94,67,30,0.92);color:%23f3c44c;"
			"border-left-color:%23f3c44c}"
			".sb-row .id{display:block;font-size:10px;color:%23b88838;"
			"font-family:monospace;letter-spacing:1px;margin-top:2px;opacity:0.7}"
			".sb-foot{padding:14px 22px;border-top:1px solid rgba(184,136,56,0.4);"
			"display:flex;flex-direction:column;gap:8px}"
			".sb-foot button{padding:10px 0;background:rgba(26,18,11,0.78);"
			"color:%23f3c44c;border:1px solid %23b88838;font-family:inherit;"
			"font-size:14px;letter-spacing:2px;cursor:pointer;"
			"transition:background 0.15s}"
			".sb-foot button:hover{background:rgba(94,67,30,0.95)}"
			".sb-foot .primary{color:%23f3c44c;font-size:16px;padding:14px 0;"
			"box-shadow:0 0 0 1px %23f3c44c inset}"
			".detail{margin-left:300px;min-height:100vh;position:relative}"
			".detail-card{position:fixed;bottom:0;left:300px;right:0;"
			"padding:32px 60px 36px;"
			"background:linear-gradient(to top,"
			"rgba(10,5,5,0.92) 0%%,rgba(10,5,5,0.78) 60%%,rgba(10,5,5,0) 100%%);"
			"max-height:60vh;overflow-y:auto}"
			".detail-card h2{font-size:56px;letter-spacing:6px;color:%23f3c44c;"
			"margin:0 0 4px;font-weight:400;"
			"text-shadow:0 4px 18px rgba(0,0,0,0.85)}"
			".detail-card .badge{display:inline-block;font-size:11px;"
			"letter-spacing:3px;color:%23b88838;text-transform:uppercase;"
			"font-family:monospace;margin-right:16px}"
			".detail-card .desc{font-size:15px;line-height:1.55;max-width:780px;"
			"color:%23f0e0c0;margin:14px 0 0;opacity:0.92}"
			".detail-card .attrs{display:grid;"
			"grid-template-columns:auto 1fr;column-gap:20px;row-gap:4px;"
			"margin-top:18px;max-width:780px;font-size:12px;font-family:monospace;"
			"max-height:200px;overflow-y:auto;padding-right:12px}"
			".detail-card .k{color:%23b88838;text-transform:uppercase;"
			"letter-spacing:1px;white-space:nowrap}"
			".detail-card .v{color:%23f0e0c0;word-break:break-word;"
			"overflow-wrap:anywhere}"
			".version{position:fixed;bottom:8px;right:14px;font-size:11px;"
			"color:%23b88838;opacity:0.5;letter-spacing:1px}";

		// Compact a multi-line / oversized field value for handbook attrs.
		// Strips control chars (newlines, CRs, tabs) and non-ASCII bytes,
		// because the result is embedded in JS string literals inside the
		// data: URL — a raw newline is a parse error, and CEF's data: URL
		// decoder has been observed to mangle UTF-8 multi-byte sequences
		// in ways that fail to parse cleanly. Backslashes are escaped.
		auto compact = [](const std::string& s, size_t kMax = 220) -> std::string {
			std::string out;
			out.reserve(std::min(s.size(), kMax + 1));
			for (unsigned char c : s) {
				if (c == '\n' || c == '\r') {
					if (!out.empty() && out.back() != ' ') out += " / ";
				} else if (c == '\t') {
					out += ' ';
				} else if (c == '\\') {
					out += "\\\\";
				} else if (c < 0x20 || c >= 0x7F) {
					// drop control + non-ASCII; common cases (em-dash,
					// ellipsis) end up as nothing rather than mojibake.
					continue;
				} else {
					out += (char)c;
				}
				if (out.size() >= kMax) { out += "..."; break; }
			}
			return out;
		};

		auto charSelectPage = [&]() -> std::string {
			struct PlayableItem {
				std::string id, name, desc;
				int str=0, sta=0, agi=0, intl=0;     // 0–5 stat bars
				float walk=0, run=0;                  // m/s
				std::string features;                 // " · "-joined tag list
			};
			auto readF = [](const solarium::ArtifactEntry& e,
			                const char* k) -> float {
				auto it = e.fields.find(k);
				if (it == e.fields.end()) return 0.0f;
				return (float)std::atof(it->second.c_str());
			};
			auto readI = [](const solarium::ArtifactEntry& e,
			                const char* k) -> int {
				auto it = e.fields.find(k);
				if (it == e.fields.end()) return 0;
				return std::atoi(it->second.c_str());
			};
			std::vector<PlayableItem> playables;
			for (auto* e : game.artifactRegistry().byCategory("living")) {
				auto it = e->fields.find("playable");
				if (it == e->fields.end()) continue;
				if (it->second != "True" && it->second != "true") continue;
				std::string desc;
				auto dit = e->fields.find("description");
				if (dit != e->fields.end()) desc = dit->second;
				PlayableItem p;
				p.id   = e->id;
				p.name = e->name.empty() ? e->id : e->name;
				p.desc = desc;
				p.str  = readI(*e, "stats_strength");
				p.sta  = readI(*e, "stats_stamina");
				p.agi  = readI(*e, "stats_agility");
				p.intl = readI(*e, "stats_intelligence");
				p.walk = readF(*e, "walk_speed");
				p.run  = readF(*e, "run_speed");
				// "features" is a string-list in the artifact loader output;
				// it lands as a comma-joined string. Show as-is.
				auto fit = e->fields.find("features");
				if (fit != e->fields.end()) p.features = fit->second;
				playables.push_back(std::move(p));
			}
			std::sort(playables.begin(), playables.end(),
				[](const PlayableItem& a, const PlayableItem& b){return a.name < b.name;});

			std::string html = "data:text/html,<html><head><style>" + kDexCss +
				".stats{display:grid;grid-template-columns:auto 1fr auto;"
				"column-gap:14px;row-gap:6px;margin-top:18px;max-width:520px;"
				"font-size:12px;align-items:center}"
				".stats .lbl{color:%23b88838;text-transform:uppercase;"
				"letter-spacing:2px;font-family:monospace}"
				".stats .bar{height:10px;background:rgba(40,30,20,0.85);"
				"border:1px solid rgba(184,136,56,0.5);position:relative;"
				"box-sizing:border-box}"
				".stats .bar .fill{position:absolute;top:0;left:0;bottom:0;"
				"background:linear-gradient(90deg,%23b88838,%23f3c44c)}"
				".stats .num{color:%23f3c44c;font-family:monospace;font-size:13px;"
				"min-width:24px;text-align:right}"
				".speed{display:flex;gap:24px;margin-top:14px;font-size:12px;"
				"color:%23b88838;font-family:monospace;letter-spacing:1px;"
				"text-transform:uppercase}"
				".speed b{color:%23f3c44c;font-weight:400;font-size:14px;"
				"margin-left:6px}"
				".features{margin-top:14px;display:flex;flex-wrap:wrap;gap:6px}"
				".features span{padding:4px 10px;font-size:11px;"
				"background:rgba(94,67,30,0.5);border:1px solid %23b88838;"
				"color:%23f3c44c;letter-spacing:1px}"
				"</style></head><body>"
				"<aside class='sidebar'>"
				"<div class='sb-head'><h1>Choose Character</h1>"
				"<div class='sub'>" + std::to_string(playables.size()) +
				" playable" + (playables.size() == 1 ? "" : "s") + "</div></div>"
				"<div class='sb-list'>";
			for (size_t i = 0; i < playables.size(); ++i) {
				const auto& p = playables[i];
				html += "<button class='sb-row" + std::string(i == 0 ? " on" : "") +
				        "' data-id='" + p.id + "' "
				        "onclick=\"pick('" + p.id + "',this)\" "
				        "onmouseenter=\"hover('" + p.id + "',this)\">" +
				        p.name + "<span class='id'>" + p.id + "</span></button>";
			}
			html += "</div>"
				"<div class='sb-foot'>"
				"<button class='primary' onclick=\"send('play')\">Begin Game</button>"
				"<button onclick=\"send('back')\">Back</button>"
				"</div></aside>"
				"<main class='detail'>"
				"<div class='detail-card' id='card'></div></main>" +
				kVersion +
				"<script>"
				"const ENTRIES={";
			for (size_t i = 0; i < playables.size(); ++i) {
				const auto& p = playables[i];
				if (i) html += ",";
				char buf[64];
				std::snprintf(buf, sizeof(buf), "%.1f", p.walk);
				std::string walkS = buf;
				std::snprintf(buf, sizeof(buf), "%.1f", p.run);
				std::string runS  = buf;
				html += "'" + p.id + "':{name:'" + enc(compact(p.name, 80)) +
				        "',desc:'" + enc(compact(p.desc, 400)) +
				        "',str:" + std::to_string(p.str) +
				        ",sta:" + std::to_string(p.sta) +
				        ",agi:" + std::to_string(p.agi) +
				        ",intl:" + std::to_string(p.intl) +
				        ",walk:'" + walkS + "',run:'" + runS +
				        "',feat:'" + enc(compact(p.features, 200)) + "'}";
			}
			html += "};"
				"function bar(label,n){"
				// 5 is the per-stat ceiling shown in artifacts; clamp anything
				// silly to 100% so the bar never overflows visually.
				"const pct=Math.min(100,Math.max(0,n*20));"
				"return \"<span class='lbl'>\"+label+\"</span>\"+"
				"\"<span class='bar'><span class='fill' style='width:\"+pct+\"%25'></span></span>\"+"
				"\"<span class='num'>\"+n+\"</span>\";}"
				"function render(id){"
				"const e=ENTRIES[id];if(!e)return;"
				"let h=\"<span class='badge'>Living</span>\"+"
				"\"<span class='badge'>\"+id+\"</span>\"+"
				"\"<h2>\"+e.name+\"</h2>\";"
				"if(e.desc)h+=\"<div class='desc'>\"+e.desc+\"</div>\";"
				"if(e.str||e.sta||e.agi||e.intl){"
				"h+=\"<div class='stats'>\"+bar('STR',e.str)+bar('STA',e.sta)+"
				"bar('AGI',e.agi)+bar('INT',e.intl)+\"</div>\";}"
				"if(e.walk||e.run){"
				"h+=\"<div class='speed'>walk<b>\"+e.walk+\"</b>m/s\";"
				"if(e.run!=='0.0')h+=\"  run<b>\"+e.run+\"</b>m/s\";"
				"h+=\"</div>\";}"
				"if(e.feat){const tags=e.feat.split(',').map(s=>s.trim()).filter(Boolean);"
				"if(tags.length){h+=\"<div class='features'>\"+"
				"tags.map(t=>'<span>'+t+'</span>').join('')+\"</div>\";}}"
				"document.getElementById('card').innerHTML=h;}"
				"function send(a){window.cefQuery({request:'action:'+a,"
				"onSuccess:()=>{},onFailure:()=>{}});}"
				"function setActive(el){document.querySelectorAll('.sb-row')"
				".forEach(b=>b.classList.remove('on'));el.classList.add('on');}"
				"let lastHover='';"
				"function hover(id,el){if(id===lastHover)return;lastHover=id;"
				"setActive(el);render(id);send('pick:'+id);}"
				"function pick(id,el){lastHover=id;setActive(el);render(id);"
				"send('pick:'+id);}"
				"render('" + (playables.empty() ? "" : playables[0].id) + "');"
				"</script>"
				"</body></html>";
			return html;
		};

		// Singleplayer flow: pick a world before going to character select.
		// Tile grid driven by kWorldTemplates (logic/world_templates.h) — the
		// canonical id ↔ templateIndex source the server also uses. Clicking
		// a tile fires action `world:<id>`, which the C++ callback resolves
		// to a templateIndex and hands to Game::hostLocalServer.
		// Multiplayer hub — split between hosting your own LAN game and
		// joining someone else's. Host routes through the world picker
		// (same UI as Singleplayer) but with the next-host-visible flag
		// set, so the spawned solarium-server announces on UDP 7778.
		auto multiplayerHubPage = [&]() -> std::string {
			return "data:text/html,<html><head><style>" + kCss +
				"h1{font-size:48px;letter-spacing:6px;margin:0 0 4px}"
				".tag{margin:0 0 28px}"
				".btn{width:340px}"
				".btn small{display:block;font-size:12px;opacity:0.65;"
				"margin-top:6px;letter-spacing:1px}"
				"</style></head><body>"
				"<h1>Multiplayer</h1>"
				"<div class='tag'>Host a session, or join one on your network</div>"
				"<button class='btn' onclick=\"send('mp_host')\">"
				"Host New Game<small>visible to LAN players</small></button>"
				"<button class='btn' onclick=\"send('mp_join')\">"
				"Join LAN Game<small>browse servers on UDP 7778</small></button>"
				"<button class='btn back' onclick=\"send('back')\">Back</button>" +
				kVersion + kJs +
				"</body></html>";
		};

		// In-game ESC pause page. Renders over the running world (the world
		// keeps rendering behind because we don't change m_state). Static
		// buttons → simple 4-line action wiring.
		auto pausePage = [&]() -> std::string {
			return "data:text/html,<html><head><style>" + kCss +
				"body{justify-content:center;align-items:center;"
				"background:radial-gradient(ellipse at center,"
				"rgba(10,5,5,0.65) 0%%,rgba(10,5,5,0.25) 60%%)}"
				"h1{font-size:64px;letter-spacing:8px;margin:0 0 24px}"
				".btn{width:280px}"
				"</style></head><body>"
				"<h1>Paused</h1>"
				"<button class='btn' onclick=\"send('resume')\">Resume</button>"
				"<button class='btn' onclick=\"send('settings')\">Settings</button>"
				"<button class='btn' onclick=\"send('main_menu')\">Main Menu</button>"
				"<button class='btn' onclick=\"send('quit')\">Quit</button>" +
				kVersion + kJs +
				"</body></html>";
		};

		auto worldPickerPage = [&]() -> std::string {
			std::string html = "data:text/html,<html><head><style>" + kCss +
				"body{justify-content:flex-start;padding:60px 60px 60px;height:auto;"
				"min-height:100vh;box-sizing:border-box;align-items:center}"
				"h1{font-size:48px;letter-spacing:6px;margin:0 0 8px}"
				".tag{margin:0 0 32px}"
				".tiles{display:grid;grid-template-columns:repeat(auto-fill,"
				"minmax(320px,1fr));gap:16px;width:100%%;max-width:1100px}"
				".tile{background:rgba(26,18,11,0.85);border:1px solid %23b88838;"
				"padding:22px 24px;cursor:pointer;font-family:inherit;color:inherit;"
				"text-align:left;transition:background 0.15s,transform 0.15s,"
				"box-shadow 0.15s}"
				".tile:hover{background:rgba(94,67,30,0.95);transform:translateY(-2px);"
				"box-shadow:0 6px 20px rgba(0,0,0,0.5)}"
				".tile h3{margin:0 0 6px;color:%23f3c44c;font-size:22px;"
				"letter-spacing:2px;font-weight:400}"
				".tile .id{font-size:11px;color:%23b88838;font-family:monospace;"
				"letter-spacing:1px;margin-bottom:8px;display:block;opacity:0.7}"
				".tile p{margin:0;font-size:13px;line-height:1.45;opacity:0.88}"
				".opts{display:flex;gap:18px;align-items:center;margin-top:28px;"
				"padding:14px 22px;background:rgba(26,18,11,0.6);"
				"border:1px solid rgba(184,136,56,0.5)}"
				".opts label{font-size:12px;letter-spacing:2px;color:%23b88838;"
				"text-transform:uppercase;display:flex;align-items:center;gap:10px}"
				".opts input{background:rgba(40,30,20,0.95);color:%23f3c44c;"
				"border:1px solid %23b88838;font-family:monospace;font-size:13px;"
				"padding:6px 8px;width:90px}"
				".opts input[type='range']{width:160px;accent-color:%23f3c44c}"
				".opts .val{font-family:monospace;color:%23f3c44c;min-width:32px;"
				"text-align:right}"
				".opts button{padding:6px 12px;background:rgba(94,67,30,0.85);"
				"color:%23f3c44c;border:1px solid %23b88838;font-family:inherit;"
				"font-size:11px;cursor:pointer;letter-spacing:1px}"
				".back{margin-top:24px;width:160px}"
				"</style></head><body>"
				"<h1>Choose World</h1>"
				"<div class='tag'>Pick where your story begins</div>"
				"<div class='tiles'>";
			for (size_t i = 0; i < solarium::kWorldTemplateCount; ++i) {
				const auto& t = solarium::kWorldTemplates[i];
				// Pull display name + description from the artifact registry
				// when present (lets the .py be the source of truth); fall
				// back to the constexpr table.
				std::string name = t.fallbackName;
				std::string desc;
				if (auto* e = game.artifactRegistry().findById(t.id)) {
					if (!e->name.empty())        name = e->name;
					if (!e->description.empty()) desc = e->description;
				}
				html += "<button class='tile' onclick=\"send('world:" +
				        std::string(t.id) + "')\">"
				        "<h3>" + enc(compact(name, 80)) + "</h3>"
				        "<span class='id'>" + std::string(t.id) + "</span>"
				        "<p>" + enc(desc.empty() ? "(no description)"
				                                 : compact(desc, 220)) + "</p>"
				        "</button>";
			}
			html += "</div>"
				"<div class='opts'>"
				"<label>Seed<input type='number' id='wseed' value='42' min='0' max='99999'></label>"
				"<button onclick=\"document.getElementById('wseed').value="
				"Math.floor(Math.random()*99999)\">Randomise</button>"
				"<label>Villagers<input type='range' id='wvill' min='0' max='100' value='0'>"
				"<span class='val' id='wvillv'>auto</span></label>"
				"</div>"
				"<button class='btn back' onclick=\"send('back')\">Back</button>"
				"<script>"
				"function send(a){window.cefQuery({request:'action:'+a,"
				"onSuccess:()=>{},onFailure:()=>{}});}"
				"document.getElementById('wvill').addEventListener('input',e=>{"
				"document.getElementById('wvillv').textContent="
				"e.target.value==='0'?'auto':e.target.value;});"
				// Hijack tile clicks: append seed + villagers to the action.
				"document.querySelectorAll('.tile').forEach(t=>{"
				"const id=t.getAttribute('onclick').match(/world:([\\w_]+)/)[1];"
				"t.onclick=()=>{const s=document.getElementById('wseed').value||'42';"
				"const v=document.getElementById('wvill').value;"
				"send('world:'+id+':'+s+':'+v);};});"
				"</script>" +
				kVersion +
				"</body></html>";
			return html;
		};

		// Interactive settings — sliders/toggles persist via the `set:<k>:<v>`
		// action which mutates Game::settings() and writes settings.json.
		// Tabs: Audio (live-applied to AudioManager), Network (host-broadcast
		// toggle, takes effect next host), Controls (cheat sheet, rebinding
		// is a later milestone). Captures live values from the current
		// Settings struct each time the page is built so reopening Settings
		// shows what's actually persisted.
		//
		// Value-capture of kCss/kJs/kVersion: this lambda is invoked from the
		// action callback long after the surrounding block returns; a [&]
		// capture would dangle (same trap as multiplayerPage hit earlier).
		auto settingsPage = [kCss, kJs, kVersion, &game]() -> std::string {
			const auto& s = game.settings();
			auto boolStr = [](bool b) { return b ? "true" : "false"; };
			char buf[64];
			std::string html = "data:text/html,<html><head><style>" + kCss +
				"body{justify-content:flex-start;padding:48px 0 60px;align-items:center;"
				"height:auto;min-height:100vh;box-sizing:border-box}"
				"h1{font-size:48px;letter-spacing:6px;margin:0 0 8px}"
				".tag{margin:0 0 24px}"
				".tabs{display:flex;gap:6px;margin-bottom:24px}"
				".tabs button{padding:8px 18px;font-size:13px;letter-spacing:2px;"
				"background:rgba(26,18,11,0.8);color:%23b88838;border:1px solid %23b88838;"
				"font-family:inherit;cursor:pointer;text-transform:uppercase}"
				".tabs button.on{background:rgba(94,67,30,0.95);color:%23f3c44c}"
				".pane{display:none;width:680px;max-width:90%%}"
				".pane.on{display:block}"
				".row{display:flex;align-items:center;justify-content:space-between;"
				"padding:14px 20px;background:rgba(26,18,11,0.6);"
				"border:1px solid rgba(184,136,56,0.4);margin-bottom:8px}"
				".row .lbl{font-size:14px;letter-spacing:2px;color:%23f0e0c0}"
				".row .ctl{display:flex;align-items:center;gap:12px;min-width:220px;"
				"justify-content:flex-end}"
				".row input[type='range']{width:180px;accent-color:%23f3c44c}"
				".row .val{font-family:monospace;color:%23f3c44c;font-size:13px;"
				"min-width:42px;text-align:right}"
				".tog{position:relative;width:44px;height:22px;background:rgba(40,30,20,0.9);"
				"border:1px solid %23b88838;border-radius:11px;cursor:pointer;"
				"transition:background 0.15s}"
				".tog::after{content:'';position:absolute;left:3px;top:2px;"
				"width:14px;height:14px;border-radius:50%%;background:%23b88838;"
				"transition:left 0.15s,background 0.15s}"
				".tog.on{background:rgba(94,67,30,0.95)}"
				".tog.on::after{left:25px;background:%23f3c44c}"
				".kvtbl{border-collapse:collapse;margin:0 auto}"
				".kvtbl td{padding:6px 18px;font-size:14px;letter-spacing:1px;"
				"border-bottom:1px solid rgba(184,136,56,0.25)}"
				".kvtbl td:first-child{color:%23b88838;text-align:right;width:220px}"
				".kvtbl td:last-child{color:%23f0e0c0;font-family:monospace}"
				".back{margin-top:24px;width:160px}"
				"</style></head><body>"
				"<h1>Settings</h1>"
				"<div class='tag'>Audio | Network | Controls</div>"
				"<div class='tabs'>"
				"<button class='on' onclick=\"tab(0)\">Audio</button>"
				"<button onclick=\"tab(1)\">Network</button>"
				"<button onclick=\"tab(2)\">Controls</button>"
				"</div>"
				// ── Audio pane ──
				"<div class='pane on' id='p0'>"
				"<div class='row'><span class='lbl'>Master Volume</span>"
				"<span class='ctl'><input type='range' min='0' max='1' step='0.05' "
				"value='";
			std::snprintf(buf, sizeof(buf), "%.2f", s.master_volume);
			html += buf;
			html += "' oninput=\"slider(this,'master_volume')\">"
				"<span class='val' id='v_master_volume'>";
			std::snprintf(buf, sizeof(buf), "%.0f%%", s.master_volume * 100);
			html += buf;
			html += "</span></span></div>"
				"<div class='row'><span class='lbl'>Music Volume</span>"
				"<span class='ctl'><input type='range' min='0' max='1' step='0.05' "
				"value='";
			std::snprintf(buf, sizeof(buf), "%.2f", s.music_volume);
			html += buf;
			html += "' oninput=\"slider(this,'music_volume')\">"
				"<span class='val' id='v_music_volume'>";
			std::snprintf(buf, sizeof(buf), "%.0f%%", s.music_volume * 100);
			html += buf;
			html += "</span></span></div>"
				"<div class='row'><span class='lbl'>Music Enabled</span>"
				"<span class='ctl'><div class='tog";
			html += s.music_enabled ? " on" : "";
			html += "' onclick=\"toggle(this,'music_enabled')\"></div></span></div>"
				"<div class='row'><span class='lbl'>Footsteps</span>"
				"<span class='ctl'><div class='tog";
			html += s.footsteps_muted ? "" : " on";  // inverted: "on" = audible
			html += "' onclick=\"toggleInv(this,'footsteps_muted')\"></div></span></div>"
				"<div class='row'><span class='lbl'>Effects (combat / dig / pickup)</span>"
				"<span class='ctl'><div class='tog";
			html += s.effects_muted ? "" : " on";
			html += "' onclick=\"toggleInv(this,'effects_muted')\"></div></span></div>"
				"</div>"
				// ── Network pane ──
				"<div class='pane' id='p1'>"
				"<div class='row'><span class='lbl'>Visible to LAN</span>"
				"<span class='ctl'><div class='tog";
			html += s.lan_visible ? " on" : "";
			html += "' onclick=\"toggle(this,'lan_visible')\"></div></span></div>"
				"<div class='row' style='opacity:0.6'><span class='lbl'>"
				"Sim speed cap</span><span class='ctl'><span class='val'>";
			std::snprintf(buf, sizeof(buf), "%.1fx", s.sim_speed_cap);
			html += buf;
			html += "</span></span></div>"
				"</div>"
				// ── Controls pane (read-only cheat sheet) ──
				"<div class='pane' id='p2'>"
				"<table class='kvtbl'>"
				"<tr><td>Move</td><td>WASD</td></tr>"
				"<tr><td>Jump</td><td>Space</td></tr>"
				"<tr><td>Sprint</td><td>Shift</td></tr>"
				"<tr><td>Look</td><td>Mouse</td></tr>"
				"<tr><td>Attack / Place</td><td>LMB / RMB</td></tr>"
				"<tr><td>Drop</td><td>Q</td></tr>"
				"<tr><td>Inventory</td><td>Tab</td></tr>"
				"<tr><td>Handbook</td><td>H</td></tr>"
				"<tr><td>Camera</td><td>V</td></tr>"
				"<tr><td>Pause</td><td>Esc</td></tr>"
				"<tr><td>Debug / Tuning</td><td>F3 / F6</td></tr>"
				"<tr><td>Screenshot</td><td>F2</td></tr>"
				"</table></div>"
				"<button class='btn back' onclick=\"send('back')\">Back</button>" +
				kVersion +
				"<script>"
				"function send(a){window.cefQuery({request:'action:'+a,"
				"onSuccess:()=>{},onFailure:()=>{}});}"
				"function tab(i){"
				"document.querySelectorAll('.tabs button').forEach((b,j)=>"
				"b.classList.toggle('on',j==i));"
				"document.querySelectorAll('.pane').forEach((p,j)=>"
				"p.classList.toggle('on',j==i));}"
				"function slider(el,key){"
				"const v=parseFloat(el.value);"
				"document.getElementById('v_'+key).textContent="
				"Math.round(v*100)+'%25';"
				"send('set:'+key+':'+v);}"
				"function toggle(el,key){"
				"const on=!el.classList.contains('on');"
				"el.classList.toggle('on',on);"
				"send('set:'+key+':'+(on?'true':'false'));}"
				"function toggleInv(el,key){"  // UI-on means feature ENABLED → key (muted) is false
				"const on=!el.classList.contains('on');"
				"el.classList.toggle('on',on);"
				"send('set:'+key+':'+(on?'false':'true'));}"
				"</script></body></html>";
			(void)boolStr;
			return html;
		};

		// Civ6 Civilopedia-style handbook. Top-level groups expand/collapse;
		// each group splits its entries by the artifact's `subcategory` (or
		// a derived key for living/playable). Right detail panel swaps on
		// hover/click; preview area is transparent so the live plaza model
		// (set via shell.previewId) shows through for renderable types.
		auto handbookPage = [&]() -> std::string {
			using Entry = const solarium::ArtifactEntry*;

			// Helper: pick the section bucket within a top-level group based
			// on artifact fields. Falls back to subcategory or "Other".
			auto livingBucket = [](Entry e) -> const char* {
				auto pit = e->fields.find("playable");
				if (pit != e->fields.end() &&
				    (pit->second == "True" || pit->second == "true"))
					return "Heroes";
				const std::string& sc = e->subcategory;
				if (sc == "humanoid")  return "Villagers";
				if (sc == "hostile" || sc == "predator")  return "Hostile";
				if (sc == "animal" || sc == "livestock")  return "Animals";
				return "Wildlife";
			};
			auto resourceBucket = [](Entry e) -> const char* {
				const std::string& id = e->id;
				if (id.find("footstep") != std::string::npos) return "Footsteps";
				if (id.find("combat") != std::string::npos)   return "Combat";
				if (id.find("music") != std::string::npos)    return "Music";
				if (id.find("ambient") != std::string::npos)  return "Ambient";
				if (id.find("ui") != std::string::npos)       return "UI";
				if (id.find("creature") != std::string::npos) return "Creatures";
				if (id.find("block") != std::string::npos ||
				    id.find("door") != std::string::npos)     return "Blocks";
				if (id.find("explosion") != std::string::npos ||
				    id.find("spell") != std::string::npos)    return "Spells";
				return "Misc";
			};
			auto subOrOther = [](Entry e) -> const char* {
				return e->subcategory.empty() ? "Other" : e->subcategory.c_str();
			};

			using Bucketer = std::function<const char*(Entry)>;
			struct Group {
				const char*              label;
				std::vector<const char*> cats;
				Bucketer                 bucketer;
			};
			std::vector<Group> groups = {
				{"Heroes & Creatures", {"living"},                livingBucket},
				{"Items & Equipment",  {"item"},                  subOrOther},
				{"Spells & Effects",   {"effect"},                subOrOther},
				{"Blocks",             {"block"},                 subOrOther},
				{"Structures",         {"structure"},             subOrOther},
				{"Worlds & Maps",      {"world", "annotation"},   subOrOther},
				{"AI Behaviors",       {"behavior"},              subOrOther},
				{"Audio Library",      {"resource"},              resourceBucket},
				{"Modding",            {"model"},                 subOrOther},
			};

			// Per-group items, sorted by (bucket, displayName) so buckets render
			// as contiguous blocks with alpha order inside.
			struct Item { int groupIdx; std::string bucket; Entry e; const char* cat; };
			std::vector<Item> items;
			size_t totalCount = 0;
			for (size_t gi = 0; gi < groups.size(); ++gi) {
				std::vector<Item> groupItems;
				const char* primaryCat = groups[gi].cats.empty()
				    ? "" : groups[gi].cats[0];
				for (const char* cat : groups[gi].cats) {
					for (auto* e : game.artifactRegistry().byCategory(cat)) {
						groupItems.push_back({(int)gi, groups[gi].bucketer(e), e, primaryCat});
					}
				}
				std::sort(groupItems.begin(), groupItems.end(),
					[](const Item& a, const Item& b){
						if (a.bucket != b.bucket) return a.bucket < b.bucket;
						std::string an = a.e->name.empty() ? a.e->id : a.e->name;
						std::string bn = b.e->name.empty() ? b.e->id : b.e->name;
						return an < bn;
					});
				totalCount += groupItems.size();
				for (auto& it : groupItems) items.push_back(std::move(it));
			}

			std::string html = "data:text/html,<html><head><style>" + kDexCss +
				".sb-grp-h{padding:12px 22px 6px;font-size:12px;letter-spacing:3px;"
				"color:%23f3c44c;text-transform:uppercase;cursor:pointer;"
				"border-top:1px solid rgba(184,136,56,0.25);user-select:none;"
				"display:flex;justify-content:space-between;align-items:center}"
				".sb-grp-h:hover{color:%23ffe28a}"
				".sb-grp-h .arr{font-size:10px;opacity:0.6;transition:transform 0.15s}"
				".sb-grp.collapsed .sb-grp-h .arr{transform:rotate(-90deg)}"
				".sb-grp.collapsed .sb-grp-body{display:none}"
				".sb-sub{padding:8px 22px 2px;font-size:10px;letter-spacing:2px;"
				"color:%23b88838;text-transform:uppercase;opacity:0.78}"
				".sb-grp .sb-row{padding-left:30px}"
				"</style></head><body>"
				"<aside class='sidebar'>"
				"<div class='sb-head'><h1>Handbook</h1>"
				"<div class='sub'>" + std::to_string(totalCount) +
				" entries</div></div>"
				"<input class='sb-search' id='dexq' placeholder='filter...' "
				"oninput=\"dexfilter(this.value)\">"
				"<div class='sb-list' id='dexlist'>";

			// Render groups in order, splitting each into buckets.
			for (size_t gi = 0; gi < groups.size(); ++gi) {
				int groupCount = 0;
				for (const auto& it : items) if (it.groupIdx == (int)gi) ++groupCount;
				if (groupCount == 0) continue;
				const bool isFirst = (gi == 0);
				html += "<div class='sb-grp" +
				        std::string(isFirst ? "" : " collapsed") +
				        "' data-grp='" + std::to_string(gi) + "'>"
				        "<div class='sb-grp-h' onclick=\"togGrp(this)\">"
				        "<span>" + enc(groups[gi].label) +
				        " <span style='opacity:0.6'>(" +
				        std::to_string(groupCount) + ")</span></span>"
				        "<span class='arr'>v</span></div>"
				        "<div class='sb-grp-body'>";
				std::string lastBucket;
				for (const auto& it : items) {
					if (it.groupIdx != (int)gi) continue;
					if (it.bucket != lastBucket) {
						html += "<div class='sb-sub'>" + enc(it.bucket) + "</div>";
						lastBucket = it.bucket;
					}
					std::string display = it.e->name.empty() ? it.e->id : it.e->name;
					std::string key = std::string(it.cat) + ":" + it.e->id;
					std::string q = display + " " + it.e->id + " " + it.bucket;
					std::transform(q.begin(), q.end(), q.begin(),
						[](unsigned char c){ return std::tolower(c); });
					// data-key carries the composite ENTRIES lookup key; the
					// bare id goes into pick:<id> so the C++ preview lookup
					// hits the artifact registry's findById.
					html += "<button class='sb-row' data-key='" + enc(key) +
					        "' data-id='" + enc(it.e->id) +
					        "' data-q='" + enc(q) + "' "
					        "onclick=\"pick(this)\" onmouseenter=\"hover(this)\">" +
					        enc(display) + "<span class='id'>" +
					        enc(it.e->id) + "</span></button>";
				}
				html += "</div></div>";
			}
			html += "</div>"
				"<div class='sb-foot'>"
				"<button onclick=\"send('back')\">Back</button>"
				"</div></aside>"
				"<main class='detail'>"
				"<div class='detail-card' id='card'></div></main>" +
				kVersion +
				"<script>const ENTRIES={";
			for (size_t i = 0; i < items.size(); ++i) {
				const auto& it = items[i];
				if (i) html += ",";
				std::string display = it.e->name.empty() ? it.e->id : it.e->name;
				html += "'" + std::string(it.cat) + ":" + it.e->id +
				        "':{name:'" + enc(compact(display, 80)) +
				        "',cat:'" + enc(it.cat) +
				        "',sub:'" + enc(it.bucket) +
				        "',desc:'" + enc(compact(it.e->description, 400)) +
				        "',attrs:[";
				std::vector<std::pair<std::string, std::string>> attrs;
				for (auto& kv : it.e->fields) {
					if (kv.first == "description") continue;
					if (kv.second.empty()) continue;
					attrs.emplace_back(kv.first, kv.second);
				}
				std::sort(attrs.begin(), attrs.end());
				if (!it.e->subcategory.empty() && it.e->subcategory != it.bucket)
					attrs.insert(attrs.begin(), {"subcategory", it.e->subcategory});
				for (size_t j = 0; j < attrs.size(); ++j) {
					if (j) html += ",";
					html += "['" + enc(attrs[j].first) + "','" +
					        enc(compact(attrs[j].second)) + "']";
				}
				html += "]}";
			}
			html += "};"
				"function render(key){"
				"const e=ENTRIES[key];if(!e)return;"
				"let h=\"<span class='badge'>\"+e.cat+\"</span>\"+"
				"\"<span class='badge'>\"+e.sub+\"</span>\"+"
				"\"<span class='badge'>\"+key.split(':')[1]+\"</span>\"+"
				"\"<h2>\"+e.name+\"</h2>\";"
				"if(e.desc)h+=\"<div class='desc'>\"+e.desc+\"</div>\";"
				"if(e.attrs.length){h+=\"<div class='attrs'>\";"
				"for(const[k,v] of e.attrs)"
				"h+=\"<span class='k'>\"+k+\"</span>"
				"<span class='v'>\"+v+\"</span>\";"
				"h+=\"</div>\";}"
				"document.getElementById('card').innerHTML=h;}"
				"function send(a){window.cefQuery({request:'action:'+a,"
				"onSuccess:()=>{},onFailure:()=>{}});}"
				"function setActive(el){document.querySelectorAll('.sb-row')"
				".forEach(b=>b.classList.remove('on'));el.classList.add('on');}"
				"function togGrp(h){h.parentElement.classList.toggle('collapsed');}"
				"let lastKey='';"
				"function hover(el){const k=el.dataset.key;if(k===lastKey)return;"
				"lastKey=k;setActive(el);render(k);send('pick:'+el.dataset.id);}"
				"function pick(el){const k=el.dataset.key;lastKey=k;"
				"setActive(el);render(k);send('pick:'+el.dataset.id);}"
				"function dexfilter(q){q=q.toLowerCase();"
				"const grps=document.querySelectorAll('.sb-grp');"
				"if(q)grps.forEach(g=>g.classList.remove('collapsed'));"
				// '%23' decodes to '#' after the URL parser runs — written
				// literally as '#' here would terminate the data: URL early
				// (treated as fragment start) and truncate the script.
				"document.querySelectorAll('%23dexlist .sb-row').forEach(r=>{"
				"r.style.display=(!q||r.dataset.q.indexOf(q)>=0)?'':'none';});"
				"document.querySelectorAll('.sb-sub').forEach(h=>{"
				"let n=h.nextElementSibling,any=false;"
				"while(n&&n.classList.contains('sb-row')){"
				"if(n.style.display!=='none')any=true;n=n.nextElementSibling;}"
				"h.style.display=any?'':'none';});}"
				"const _f=document.querySelector('.sb-row');"
				"if(_f){_f.classList.add('on');lastKey=_f.dataset.key;"
				"render(_f.dataset.key);send('pick:'+_f.dataset.id);}"
				"</script>"
				"</body></html>";
			return html;
		};

		// kCss captured by value because this lambda is invoked from the
		// action callback long after this scope returns — a `[&]` capture
		// would dangle and produce a corrupted data: URL (we hit this).
		auto multiplayerPage = [kCss, kJs, kVersion, &game]() -> std::string {
			constexpr const char* kClientVer = "0.2.0";
			std::string html = "data:text/html,<html><head><style>" + kCss +
				".srv{display:grid;grid-template-columns:1fr auto auto auto;"
				"column-gap:18px;align-items:center;width:680px;padding:12px 22px;"
				"margin:5px 0;background:rgba(26,18,11,0.78);"
				"border:1px solid %23b88838;color:%23f3c44c;font-size:14px;"
				"letter-spacing:1px;font-family:monospace;cursor:pointer;"
				"transition:background 0.15s,transform 0.15s}"
				".srv.mismatch{opacity:0.55;border-color:rgba(184,136,56,0.4)}"
				".srv:hover{background:rgba(94,67,30,0.92);transform:scale(1.01)}"
				".srv .ip{color:%23f3c44c}"
				".srv .world{color:%23f0e0c0;font-size:12px;text-transform:uppercase;"
				"letter-spacing:2px}"
				".srv .ver{color:%23b88838;font-size:11px;letter-spacing:1px}"
				".srv .pl{color:%23b88838;font-size:13px;text-align:right;min-width:90px}"
				".filters{display:flex;gap:18px;align-items:center;margin-bottom:12px;"
				"font-size:12px;letter-spacing:2px;color:%23b88838;text-transform:uppercase}"
				".filters label{display:flex;align-items:center;gap:8px;cursor:pointer}"
				".filters input[type='checkbox']{accent-color:%23f3c44c}"
				".empty{opacity:0.5;font-style:italic;margin:24px 0}"
				"</style></head><body>"
				"<h1>Multiplayer</h1>"
				"<div class='tag'>";
			if (!game.lanBrowser().listening()) {
				html += "Could not bind UDP 7778 - try --host HOST --port PORT";
			} else {
				const auto& srvs = game.lanBrowser().servers();
				if (srvs.empty()) {
					html += "Scanning UDP 7778...";
				} else {
					char hdr[64];
					std::snprintf(hdr, sizeof(hdr), "%zu LAN server%s",
						srvs.size(), srvs.size() == 1 ? "" : "s");
					html += hdr;
				}
			}
			html += "</div>"
				"<div class='filters'>"
				"<label><input type='checkbox' id='fver' checked>"
				"Hide version mismatches</label>"
				"</div>";
			for (const auto& s : game.lanBrowser().servers()) {
				bool match = (s.version == kClientVer);
				char row[400];
				std::snprintf(row, sizeof(row),
					"<div class='srv%s' data-match='%d' "
					"onclick=\"send('join:%s:%d')\">"
					"<span class='ip'>%s:%d</span>"
					"<span class='world'>%s</span>"
					"<span class='ver'>v%s</span>"
					"<span class='pl'>%d player%s</span></div>",
					match ? "" : " mismatch", match ? 1 : 0,
					s.ip.c_str(), s.port,
					s.ip.c_str(), s.port,
					s.world.empty()   ? "?" : s.world.c_str(),
					s.version.empty() ? "?" : s.version.c_str(),
					s.humans, s.humans == 1 ? "" : "s");
				html += row;
			}
			html += std::string(
				"<button class='btn' onclick=\"send('multiplayer')\">Refresh</button>"
				"<button class='btn back' onclick=\"send('back')\">Back</button>"
				"<script>"
				"document.getElementById('fver').addEventListener('change',e=>{"
				"const hide=e.target.checked;"
				"document.querySelectorAll('.srv').forEach(r=>{"
				"r.style.display=(hide && r.dataset.match==='0')?'none':'';});});"
				"document.getElementById('fver').dispatchEvent(new Event('change'));"
				"</script>") +
				kVersion + kJs +
				"</body></html>";
			return html;
		};

		// Captured by ref in the action callback. Static so they live across
		// lambda invocations. Built once at startup; multiplayer is rebuilt
		// per click below to pick up new LAN servers.
		static std::string sMain  = mainPage();
		static std::string sChar  = charSelectPage();
		static std::string sHand  = handbookPage();
		static std::string sWorld = worldPickerPage();
		static std::string sPause = pausePage();
		static std::string sMpHub = multiplayerHubPage();
		// settingsPage rebuilt fresh each click — captures live values
		// (master_volume, footsteps_muted, …) so reopening shows current state.

		// Test hook: SOLARIUM_BOOT_PAGE=handbook|chars|settings|main lets a
		// dev land directly on a non-main page for screenshot iteration.
		// Useful when synthetic clicks are flaky (real cursor on the visible
		// window can race the click).
		if (cefUrl.empty()) {
			const char* boot = std::getenv("SOLARIUM_BOOT_PAGE");
			using MS = solarium::vk::MenuScreen;
			if (boot && std::string(boot) == "handbook") {
				cefUrl = sHand; game.setMenuScreen(MS::Handbook);
			} else if (boot && std::string(boot) == "chars") {
				cefUrl = sChar; game.setMenuScreen(MS::CharacterSelect);
				game.setPreviewClip("wave");
				// Mirror what the "singleplayer" action does so beginConnectAs
				// has a previewId to commit when the user clicks Begin Game.
				for (auto* e : game.artifactRegistry().byCategory("living")) {
					auto it = e->fields.find("playable");
					if (it == e->fields.end()) continue;
					if (it->second == "True" || it->second == "true") {
						game.setPreviewId(e->id);
						break;
					}
				}
				// Lazy spawn: char-select is reached, server needs to exist
				// for Begin Game to work. Skip if --port (joining remote).
				if (port <= 0) {
					solarium::AgentManager::Config cfg;
					cfg.seed = 42;
					cfg.templateIndex = templateIndex;
					cfg.execDir = execDir;
					cfg.villagersOverride = villagersOverride;
					cfg.simSpeed = simSpeed;
					if (!game.hostLocalServer(cfg))
						std::fprintf(stderr, "[boot] hostLocalServer failed\n");
				}
			} else if (boot && std::string(boot) == "settings") {
				cefUrl = settingsPage();
			} else if (boot && std::string(boot) == "worlds") {
				cefUrl = sWorld;
			} else if (boot && std::string(boot) == "pause") {
				cefUrl = sPause;
			} else if (boot && std::string(boot) == "mp") {
				cefUrl = multiplayerPage();
			} else {
				cefUrl = sMain;
			}
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
		game.setCefHandbookUrl(sHand);
		game.setCefPauseUrl(sPause);
		game.setCefMainUrl(sMain);
		// multiplayerPage is captured by value so the lambda owns its own
		// copy — local 'multiplayerPage' in this block goes out of scope
		// once init returns. Same for the enc helper inside it.
		cefHost->setActionCallback(
			[win, &game, hostRaw, multiplayerPage, settingsPage](const std::string& action) {
			std::printf("[cef] action: %s\n", action.c_str());
			using MS = solarium::vk::MenuScreen;
			// First playable id, used as default char-select pick. Cached
			// on first lookup; the artifact registry is immutable post-init.
			static std::string firstPlayableId;
			if (firstPlayableId.empty()) {
				for (auto* e : game.artifactRegistry().byCategory("living")) {
					auto it = e->fields.find("playable");
					if (it == e->fields.end()) continue;
					if (it->second == "True" || it->second == "true") {
						firstPlayableId = e->id;
						break;
					}
				}
			}
			if (action == "quit") {
				glfwSetWindowShouldClose(win, GLFW_TRUE);
			} else if (action == "resume") {
				// Pause-menu Resume — same as ESC-while-CEF-up: just drop
				// the overlay and hand control back to the world.
				game.setCefMenuActive(false);
			} else if (action == "main_menu") {
				// Pause-menu "Main Menu" — disconnect, kill any hosted
				// subprocess, rewind to the title.
				game.returnToMainMenu();
			} else if (action == "back") {
				// Back from a CEF sub-screen has two meanings:
				//   * Boot flow (Menu state) — return to the main title.
				//   * In-game (Playing state, H opened the handbook over
				//     the world) — just dismiss CEF; don't drop the player
				//     back to the title.
				if (game.state() == solarium::vk::GameState::Playing) {
					game.setCefMenuActive(false);
					game.setPreviewId("");
					game.setPreviewClip("");
				} else {
					game.setMenuScreen(MS::Main);
					game.setPreviewId("");
					game.setPreviewClip("");
					hostRaw->loadUrl(sMain);
				}
			} else if (action == "singleplayer") {
				// Singleplayer = host locally, but first the user picks
				// which world. The actual server-spawn fires on `world:<id>`
				// once we know what to host. Picker → world: → host →
				// char-select; world picker isn't a "preview-style" screen
				// so clear shell.previewId to drop the camera pin.
				game.setMenuScreen(MS::Main);
				game.setPreviewId("");
				game.setPreviewClip("");
				game.setNextHostLanVisible(false);  // private session
				hostRaw->loadUrl(sWorld);
			}
			else if (action.rfind("world:", 0) == 0) {
				// "world:<id>" or "world:<id>:<seed>:<villagers>" — the picker
				// hijacks tile clicks to append slider values, so today's
				// picker always sends the long form. Plain form kept for
				// SOLARIUM_BOOT_PAGE=worlds and any future caller that just
				// wants defaults.
				const std::string body = action.substr(6);
				auto parts = std::vector<std::string>{};
				size_t p = 0;
				while (p <= body.size()) {
					auto q = body.find(':', p);
					parts.push_back(body.substr(p, q == std::string::npos ? std::string::npos : q - p));
					if (q == std::string::npos) break;
					p = q + 1;
				}
				const std::string& id = parts[0];
				int idx = solarium::worldTemplateIndexOf(id);
				if (idx < 0) {
					std::fprintf(stderr,
						"[cef] world:%s — unknown template\n", id.c_str());
					return;
				}
				solarium::AgentManager::Config cfg;
				cfg.templateIndex = idx;
				cfg.execDir = game.execDir();
				cfg.seed              = parts.size() > 1 ? std::atoi(parts[1].c_str()) : 42;
				cfg.villagersOverride = parts.size() > 2 ? std::atoi(parts[2].c_str()) : 0;
				if (cfg.seed <= 0) cfg.seed = 42;
				if (!game.hostLocalServer(cfg)) {
					std::fprintf(stderr,
						"[cef] world:%s — hostLocalServer failed\n", id.c_str());
					return;
				}
				game.setMenuScreen(MS::CharacterSelect);
				game.setPreviewClip("wave");
				if (!firstPlayableId.empty())
					game.setPreviewId(firstPlayableId);
				hostRaw->loadUrl(sChar);
			} else if (action == "multiplayer") {
				// Multiplayer hub: Host vs Join split.
				game.setMenuScreen(MS::Main);
				game.setPreviewId("");
				game.setPreviewClip("");
				game.setNextHostLanVisible(false);
				hostRaw->loadUrl(sMpHub);
			} else if (action == "mp_host") {
				// Host flow: world picker, then hostLocalServer with broadcast on.
				game.setNextHostLanVisible(true);
				hostRaw->loadUrl(sWorld);
			} else if (action == "mp_join") {
				// Join flow: existing LAN browser. Rebuild fresh each click —
				// picks up newly-discovered LAN servers.
				game.setNextHostLanVisible(false);
				hostRaw->loadUrl(multiplayerPage());
			} else if (action == "handbook") {
				// Hand the camera pin a screen we treat as preview-style; the
				// JS-side render() fires a pick: for the first entry on load
				// to seed shell.previewId. Clear previewClip so the static
				// "mine" pose is used (reads more like an encyclopedia entry
				// than the lively wave on char-select).
				game.setMenuScreen(MS::Handbook);
				game.setPreviewClip("");
				hostRaw->loadUrl(sHand);
			} else if (action == "settings") {
				hostRaw->loadUrl(settingsPage());
			} else if (action.rfind("set:", 0) == 0) {
				// "set:master_volume:0.5" / "set:footsteps_muted:true" — mutate
				// Settings, persist to disk, and live-apply to AudioManager so
				// the slider effect is audible while the page is still open.
				const std::string body = action.substr(4);
				auto colon = body.find(':');
				if (colon == std::string::npos) return;
				std::string key = body.substr(0, colon);
				std::string val = body.substr(colon + 1);
				bool b = (val == "true" || val == "1");
				float f = (float)std::atof(val.c_str());
				auto& s = game.settings();
				if      (key == "master_volume")   { s.master_volume   = f; game.audio().setMasterVolume(f); }
				else if (key == "music_volume")    { s.music_volume    = f; game.audio().setMusicVolume(f); }
				else if (key == "music_enabled")   { s.music_enabled   = b; if (!b) game.audio().stopMusic(); }
				else if (key == "footsteps_muted") { s.footsteps_muted = b; game.audio().setFootstepsMuted(b); }
				else if (key == "effects_muted")   { s.effects_muted   = b; game.audio().setEffectsMuted(b); }
				else if (key == "lan_visible")     { s.lan_visible     = b; }
				else { std::fprintf(stderr, "[set] unknown key: %s\n", key.c_str()); return; }
				s.save();
			} else if (action.rfind("pick:", 0) == 0) {
				// Preview-only: swap the plaza-injected model. Camera pin in
				// game_vk.cpp keeps framing it. No connect yet.
				game.setPreviewId(action.substr(5));
			} else if (action == "play") {
				// Commit the current preview. previewId was set by an earlier
				// "pick:" or by the "singleplayer" default.
				const std::string id = game.previewId();
				if (!id.empty() && game.beginConnectAs(id)) {
					game.setMenuScreen(MS::Connecting);
					game.setCefMenuActive(false);
				}
			} else if (action.rfind("join:", 0) == 0) {
				// "join:192.168.1.5:7777" — point the network transport at
				// the chosen LAN host (no local server spawn) and go to
				// character select.
				const std::string rest = action.substr(5);
				auto colon = rest.rfind(':');
				if (colon != std::string::npos) {
					std::string ip = rest.substr(0, colon);
					int port = std::atoi(rest.substr(colon + 1).c_str());
					game.joinRemoteServer(ip, port);
					game.setMenuScreen(MS::CharacterSelect);
					game.setPreviewClip("wave");
					if (!firstPlayableId.empty())
						game.setPreviewId(firstPlayableId);
					hostRaw->loadUrl(sChar);
				}
			}
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
