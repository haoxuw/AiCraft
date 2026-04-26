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
#include "client/process_manager.h"
#include "client/game_vk.h"
#include "client/cef_browser_host.h"
#include "client/cef_app.h"
#include "agent/agent_client.h"
#include "debug/entity_log.h"
#include "debug/perf_registry.h"

#include "include/cef_app.h"

#include <ctime>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

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

	// Spawn a solarium-server (if --port absent) and connect over TCP, handing
	// the NetworkServer to Game BEFORE init() so chunks stream from the server
	// rather than being generated client-side.
	solarium::AgentManager agentMgr;
	solarium::LocalWorld localWorld;
	{
		solarium::ArtifactRegistry artifacts;
		artifacts.loadAll("artifacts");
		localWorld.entityDefs().mergeArtifactTags(artifacts.livingTags());
		localWorld.entityDefs().applyLivingStats(artifacts.livingStats());
	}
	std::unique_ptr<solarium::NetworkServer> net;
	{
		int connectPort = port;
		if (connectPort <= 0) {
			solarium::AgentManager::Config cfg;
			cfg.seed = 42;
			cfg.templateIndex = templateIndex;
			cfg.execDir = execDir;
			cfg.villagersOverride = villagersOverride;
			cfg.simSpeed = simSpeed;
			connectPort = agentMgr.launchServer(cfg);
			if (connectPort < 0) {
				fprintf(stderr, "[vk] failed to launch solarium-server\n");
				return 1;
			}
		}
		net = std::make_unique<solarium::NetworkServer>(host, connectPort, localWorld);
		// Handshake (C_HELLO) is deferred until the menu's character-select
		// completes — we need the chosen creatureType before sending HELLO.
		// --skip-menu calls Game::skipMenu() which connects immediately as
		// the server-default playable.
		printf("[vk] solarium-server ready at %s:%d (awaiting character pick)\n",
		       host.c_str(), connectPort);
	}

	solarium::vk::Game game;
	game.setServer(net.get());  // must precede init()
	game.setPendingConnect(42, templateIndex);  // seed+template used on character-select confirm
	game.setAgentConfig(agentCfg);              // DecidePacer knobs — must precede init()
	if (!game.init(rhi.get(), win)) {
		fprintf(stderr, "game.init failed\n");
		return 1;
	}
	if (skipMenu) game.skipMenu();

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

		auto mainPage = [&]() -> std::string {
			return "data:text/html,<html><head><style>" + kCss +
				"</style></head><body>"
				"<h1>Solarium</h1>"
				"<div class='tag'>A voxel sandbox civilization</div>"
				"<button class='btn' onclick=\"send('singleplayer')\">Singleplayer</button>"
				"<button class='btn' onclick=\"send('multiplayer')\">Multiplayer</button>"
				"<button class='btn' onclick=\"send('handbook')\">Handbook</button>"
				"<button class='btn' onclick=\"send('settings')\">Settings</button>"
				"<button class='btn' onclick=\"send('quit')\">Quit</button>"
				"<div class='version'>v0.2.0 / CEF 146</div>" + kJs +
				"</body></html>";
		};

		auto charSelectPage = [&]() -> std::string {
			std::string html = "data:text/html,<html><head><style>" + kCss +
				".btn{width:340px}.btn small{display:block;font-size:12px;"
				"opacity:0.65;margin-top:4px;letter-spacing:1px}"
				"</style></head><body>"
				"<h1>Choose Character</h1>"
				"<div class='tag'>Pick your role in the realm</div>";
			struct PlayableItem { std::string id, name, desc; };
			std::vector<PlayableItem> playables;
			for (auto* e : game.artifactRegistry().byCategory("living")) {
				auto it = e->fields.find("playable");
				if (it == e->fields.end()) continue;
				if (it->second != "True" && it->second != "true") continue;
				std::string desc;
				auto dit = e->fields.find("description");
				if (dit != e->fields.end()) desc = dit->second;
				playables.push_back({e->id, e->name.empty() ? e->id : e->name, desc});
			}
			std::sort(playables.begin(), playables.end(),
				[](const PlayableItem& a, const PlayableItem& b){return a.name < b.name;});
			for (auto& p : playables) {
				html += "<button class='btn' onclick=\"send('play:" + p.id + "')\">"
				      + p.name;
				if (!p.desc.empty()) html += "<small>" + p.desc + "</small>";
				html += "</button>";
			}
			html += "<button class='btn back' onclick=\"send('back')\">Back</button>";
			html += "<div class='version'>v0.2.0 / CEF 146</div>" + kJs;
			html += "</body></html>";
			return html;
		};

		auto placeholderPage = [&](const std::string& title,
		                            const std::string& body) -> std::string {
			return "data:text/html,<html><head><style>" + kCss +
				"</style></head><body>"
				"<h1>" + title + "</h1>"
				"<div class='tag'>" + body + "</div>"
				"<button class='btn back' onclick=\"send('back')\">Back</button>"
				"<div class='version'>v0.2.0 / CEF 146</div>" + kJs +
				"</body></html>";
		};

		// Captured by ref in the action callback below.
		static std::string sMain   = mainPage();
		static std::string sChar   = charSelectPage();
		static std::string sMulti  = placeholderPage("Multiplayer", "LAN browser coming soon");
		static std::string sHand   = placeholderPage("Handbook", "Handbook moving to HTML soon");
		static std::string sSett   = placeholderPage("Settings", "Controls + tuning coming soon");

		if (cefUrl.empty()) cefUrl = sMain;

		cefHost = std::make_unique<solarium::vk::CefHost>(fbw, fbh);
		game.setCefMenuActive(true);
		auto* hostRaw = cefHost.get();
		cefHost->setActionCallback([win, &game, hostRaw](const std::string& action) {
			std::printf("[cef] action: %s\n", action.c_str());
			if (action == "quit") {
				glfwSetWindowShouldClose(win, GLFW_TRUE);
			} else if (action == "back") {
				hostRaw->loadUrl(sMain);
			} else if (action == "singleplayer") {
				hostRaw->loadUrl(sChar);
			} else if (action == "multiplayer") {
				hostRaw->loadUrl(sMulti);
			} else if (action == "handbook") {
				hostRaw->loadUrl(sHand);
			} else if (action == "settings") {
				hostRaw->loadUrl(sSett);
			} else if (action.rfind("play:", 0) == 0) {
				// "play:guy" — pick character, kick into Connecting flow,
				// dismiss CEF so the world becomes visible.
				std::string id = action.substr(5);
				if (game.beginConnectAs(id)) {
					game.setMenuScreen(solarium::vk::MenuScreen::Connecting);
					game.setCefMenuActive(false);
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
