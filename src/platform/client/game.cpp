#include "client/game.h"
#include "client/game_logger.h"
#include "server/entity_manager.h"
#include "shared/constants.h"
#include "client/model_loader.h"
#include "shared/physics.h"
#include "client/network_server.h"
#include "imgui.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace civcraft {

// ============================================================
// Screenshot utility
// ============================================================
static void writeScreenshot(int w, int h, const char* path) {
	std::vector<uint8_t> px(w * h * 3);
	glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
	std::vector<uint8_t> fl(w * h * 3);
	for (int y = 0; y < h; y++)
		memcpy(&fl[y * w * 3], &px[(h - 1 - y) * w * 3], w * 3);
	std::ofstream f(path, std::ios::binary);
	f << "P6\n" << w << " " << h << "\n255\n";
	f.write((char*)fl.data(), fl.size());
	printf("Screenshot: %s\n", path);
}

// ============================================================
// Init / Shutdown
// ============================================================
bool Game::init(int argc, char** argv) {
	printf("=== CivCraft v0.9.0 ===\n");

	// Determine executable directory (for launching server/bot processes)
	if (argc > 0) {
		std::string exe = argv[0];
		auto pos = exe.rfind('/');
		m_execDir = (pos != std::string::npos) ? exe.substr(0, pos) : ".";
	}

	// Early scan for --log-only so the window is created hidden AND we can
	// force --skip-menu (no GUI to click through). Other flags handled below.
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--log-only") == 0) {
			m_logOnly = true;
			m_skipMenu = true;
		}
	}

	if (!m_window.init(1600, 900, "CivCraft", m_logOnly)) return false;
	if (!m_renderer.init("shaders")) return false;
	if (!m_text.init("shaders")) return false;
	if (!m_particles.init("shaders")) return false;

	m_entityDrawer    = std::make_unique<EntityDrawer>(m_renderer.modelRenderer());
	m_lightbulbDrawer = std::make_unique<LightbulbDrawer>(m_renderer.modelRenderer(), m_text);

	m_controls.load("config/controls.yaml");
	m_ui.init(m_window.handle());

	// Generate random player name
	{
		static const char* adj[] = {"Swift","Brave","Sneaky","Lucky","Mighty",
			"Clever","Bold","Calm","Fierce","Noble","Gentle","Wild"};
		static const char* noun[] = {"Fox","Bear","Eagle","Wolf","Owl",
			"Hawk","Lynx","Stag","Crow","Hare","Pike","Wren"};
		srand((unsigned)time(nullptr));
		m_playerName = std::string(adj[rand()%12]) + noun[rand()%12];
	}
	m_imguiMenu.setPlayerInfo(&m_playerName, &m_selectedCreature);

	// Audio system
	if (m_audio.init()) {
		m_audio.loadSoundsFrom("resources/sounds");
	}

	m_hud.init(m_renderer.highlightShader());
	m_behaviorStore.init("artifacts/behaviors");

	// Characters + faces

	// World templates
	m_templates = {
		std::make_shared<ConfigurableWorldTemplate>("artifacts/worlds/base/flat.py"),
		std::make_shared<ConfigurableWorldTemplate>("artifacts/worlds/base/village.py"),
	};
	m_imguiMenu.init(m_templates);
	m_imguiMenu.setControls(&m_controls);
	m_imguiMenu.setAudio(&m_audio);

	// Load all artifact definitions (Python files from artifacts/)
	m_artifacts.setPlayerNamespace(ArtifactRegistry::generatePlayerNamespace());
	m_artifacts.loadAll("artifacts");

	// Parse args
	{
		development::DebugCapture::Config dbgCfg;
		for (int i = 1; i < argc; i++) {
			if (strcmp(argv[i], "--skip-menu") == 0) m_skipMenu = true;
			else if (strcmp(argv[i], "--profiler") == 0) m_showProfiler = true;
			else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) m_connectHost = argv[++i];
			else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
				m_connectPort = atoi(argv[++i]);
				m_serverPort = m_connectPort;
			}
			else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) {
				m_skipMenuTemplate = atoi(argv[++i]);
			}
			else if (strcmp(argv[i], "--debug-scenario") == 0 && i + 1 < argc) {
				dbgCfg.scenario = argv[++i];
				dbgCfg.active = true;
				m_skipMenu = true;  // scenario implies --skip-menu
			}
			else if (strcmp(argv[i], "--debug-item") == 0 && i + 1 < argc) {
				dbgCfg.targetItem = argv[++i];
			}
			else if (strcmp(argv[i], "--debug-character") == 0 && i + 1 < argc) {
				dbgCfg.targetCharacter = argv[++i];
			}
			else if (strcmp(argv[i], "--debug-clip") == 0 && i + 1 < argc) {
				dbgCfg.targetClip = argv[++i];
			}
			else if (strcmp(argv[i], "--debug-hand-item") == 0 && i + 1 < argc) {
				dbgCfg.handItem = argv[++i];
			}
			else if (strcmp(argv[i], "--log-only") == 0) {
				/* handled above */
			}
		}
		if (dbgCfg.active) {
			m_debugCapture.configure(dbgCfg);
			printf("[Debug] Scenario '%s' item='%s' character='%s'\n",
			       dbgCfg.scenario.c_str(),
			       dbgCfg.targetItem.c_str(),
			       dbgCfg.targetCharacter.c_str());
		}
	}

	// Models — ALL loaded from Python (artifacts/models/base/ and models/player/)
	m_models = model_loader::loadAllModels("artifacts");
	printf("[Game] Loaded %zu models from Python\n", m_models.size());

	// Validate: every artifact and block type should have a model.
	// Missing models get the magenta "?" placeholder and a console warning.
	{
		int missing = 0;
		// Check artifacts (living entities, items).
		// Prefer the artifact's explicit "model" field when present (lets
		// multiple living entries share one model — e.g. brave_chicken → chicken).
		// Fall back to file stem so names with spaces still resolve.
		for (auto* cat : {"living", "item"}) {
			for (auto* entry : m_artifacts.byCategory(cat)) {
				std::string key;
				auto mit = entry->fields.find("model");
				if (mit != entry->fields.end() && !mit->second.empty()) {
					key = mit->second;
				} else {
					key = std::filesystem::path(entry->filePath).stem().string();
				}
				if (!m_models.count(key)) {
					printf("[MISSING MODEL] %s '%s' → expected artifacts/models/base/%s.py\n",
						cat, entry->name.c_str(), key.c_str());
					missing++;
				}
			}
		}
		// Check block types (blocks appear in inventory when broken)
		for (auto& entry : m_artifacts.entries()) {
			if (entry.category == "block") {
				std::string key = entry.name;
				for (auto& c : key) c = (char)std::tolower((unsigned char)c);
				if (!m_models.count(key) && key != "air") {
					printf("[MISSING MODEL] block '%s' → expected artifacts/models/base/%s.py\n",
						entry.name.c_str(), key.c_str());
					missing++;
				}
			}
		}
		if (missing > 0)
			printf("[WARNING] %d items/blocks have no 3D model — they will show as '?' in UI\n", missing);
		else
			printf("[Game] All items and blocks have 3D models ✓\n");
	}
	m_modelPreview.init(&m_renderer.highlightShader(), 256, 256);
	m_iconCache.init(&m_renderer.highlightShader(), &m_renderer.modelRenderer());

	// Character selection preview (uses same models + preview as Handbook)
	m_imguiMenu.setCharacterPreview(&m_artifacts, &m_modelPreview,
		&m_renderer.modelRenderer(), &m_models);

	// Auto-select first living entity if none set.
	// Prefer humanoids (subcategory == "humanoid") for the default character;
	// animals are still playable but humanoids feel like the canonical "player".
	if (m_selectedCreature.empty()) {
		auto living = m_artifacts.byCategory("living");
		const ArtifactEntry* pick = nullptr;
		for (auto* e : living) {
			if (e->subcategory == "humanoid") { pick = e; break; }
		}
		if (!pick && !living.empty()) pick = living[0];
		m_selectedCreature = pick ? pick->id : "player";
	}

	// Register ALL models for Handbook 3D preview
	auto& hb = m_imguiMenu.handbook();
	hb.setPreview(&m_modelPreview, &m_renderer.modelRenderer());
	hb.setRegistry(&m_artifacts);
	hb.setAudio(&m_audio);

	// Register ALL Python models for Handbook 3D preview
	for (auto& [key, mdl] : m_models)
		hb.registerModel(key, mdl);

	// Register source-tree paths so the in-game editor can save back to
	// the .py the build was staged from. Walk up from CWD (build/) looking
	// for a sibling src/<game>/artifacts/models/{base,player}/<name>.py;
	// this keeps edits from being clobbered by the next CMake POST_BUILD.
	// Game-agnostic — we just look at every src/* subdir.
	{
		namespace fs = std::filesystem;
		std::vector<fs::path> srcRoots;
		fs::path probe = fs::current_path();
		for (int i = 0; i < 4; i++) {
			if (fs::is_directory(probe / "src")) {
				for (auto& e : fs::directory_iterator(probe / "src"))
					if (e.is_directory()) srcRoots.push_back(e.path());
				break;
			}
			probe = probe.parent_path();
		}
		// Strip variant suffix ("#0") since multiple bakes share one .py.
		auto baseName = [](std::string n) {
			auto h = n.find('#'); if (h != std::string::npos) n.resize(h); return n;
		};
		for (auto& [key, _] : m_models) {
			std::string name = baseName(key);
			std::string found;
			for (auto& root : srcRoots) {
				for (auto* sub : {"player", "base"}) {
					auto p = root / "artifacts" / "models" / sub / (name + ".py");
					if (fs::exists(p)) { found = p.string(); break; }
				}
				if (!found.empty()) break;
			}
			if (!found.empty()) hb.registerModelPath(key, found);
		}
	}

	// Scroll callback — reads selected slot from player entity
	struct ScrollData { Game* game; Camera* cam; };
	static ScrollData sd = {this, &m_camera};
	glfwSetWindowUserPointer(m_window.handle(), &sd);
	glfwSetScrollCallback(m_window.handle(), [](GLFWwindow* w, double xoff, double y) {
		// Always forward to ImGui first
		ImGuiIO& io = ImGui::GetIO();
		io.AddMouseWheelEvent((float)xoff, (float)y);

		// If ImGui wants the mouse (hovering a window), don't handle in gameplay
		if (io.WantCaptureMouse) return;

		auto* d = (ScrollData*)glfwGetWindowUserPointer(w);
		if (d->cam->mode == CameraMode::FirstPerson) {
			Entity* pe = d->game->playerEntity();
			if (pe) {
				int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
				slot = ((slot - (int)y) % HOTBAR_SIZE + HOTBAR_SIZE) % HOTBAR_SIZE;
				pe->setProp(Prop::SelectedSlot, slot);
			}
		} else if (d->cam->mode == CameraMode::ThirdPerson) {
			d->cam->orbitDistanceTarget = std::clamp(d->cam->orbitDistanceTarget - (float)y, 2.0f, 20.0f);
		} else if (d->cam->mode == CameraMode::RPG) {
			d->cam->godDistanceTarget = std::clamp(d->cam->godDistanceTarget - (float)y * 2, 3.0f, 50.0f);
		} else if (d->cam->mode == CameraMode::RTS) {
			// Multiplicative zoom (WoW-style): each wheel tick scales the
			// camera height by ~15%, so close zooms feel fine-grained and
			// far zooms move fast. Height drives both altitude and horizontal
			// distance in updateRTS, giving a true dolly along the view ray.
			float factor = std::pow(0.85f, (float)y);
			d->cam->rtsHeightTarget = std::clamp(
				d->cam->rtsHeightTarget * factor, 5.0f, 120.0f);
		}
	});

	// Character input callback (for code editor)
	glfwSetCharCallback(m_window.handle(), [](GLFWwindow* w, unsigned int c) {
		auto* d = (ScrollData*)glfwGetWindowUserPointer(w);
		d->game->m_codeEditor.onChar(c);
	});

	// Key callback (for code editor special keys)
	glfwSetKeyCallback(m_window.handle(), [](GLFWwindow* w, int key, int, int action, int mods) {
		auto* d = (ScrollData*)glfwGetWindowUserPointer(w);
		d->game->m_codeEditor.onKey(key, action, mods);
	});

	m_lastTime = std::chrono::steady_clock::now();

	// --skip-menu + --host: auto-join the server directly, no menu
	if (m_skipMenu && !m_connectHost.empty()) {
		printf("[Game] --skip-menu: auto-joining %s:%d\n",
		       m_connectHost.c_str(), m_connectPort);
		joinServer(m_connectHost, m_connectPort, GameState::LOADING);
	// --skip-menu alone: always start a fresh world (no save loading for debug)
	} else if (m_skipMenu) {
		printf("[Game] --skip-menu: starting new world directly\n");
		m_currentWorldPath = "";   // force new world, never resume a save
		m_currentSeed = (int)std::random_device{}();
		enterGame(m_skipMenuTemplate, GameState::LOADING);
	// --host only: pre-populate the server list and show menu
	} else if (!m_connectHost.empty()) {
		printf("[Game] Server hint: %s:%d (join from menu)\n",
		       m_connectHost.c_str(), m_connectPort);
		m_imguiMenu.addServerHint(m_connectHost, m_connectPort);
	}

	return true;
}

void Game::shutdown() {
	// Save world on quit
	saveCurrentWorld();

	// Stop server + bot processes if we spawned them
	m_agentMgr.stopAll();

m_audio.shutdown();
	m_ui.shutdown();
	m_hud.shutdown();
	m_particles.shutdown();
	m_text.shutdown();
	m_renderer.shutdown();
	m_window.shutdown();
}


void Game::appendLog(const std::string& msg) {
	// Game-time timestamp: map worldTime [0..1] to HH:MM (for the ImGui viewer)
	int hours = (int)(m_worldTime * 24.0f) % 24;
	int mins  = (int)(m_worldTime * 24.0f * 60.0f) % 60;
	char entry[280];
	snprintf(entry, sizeof(entry), "[%02d:%02d] %s", hours, mins, msg.c_str());
	m_gameLog.push_back(entry);
	if (m_gameLog.size() > 200) m_gameLog.pop_front();

	// Tee to the persistent logger. Pick a coarse category from the text so
	// Claude / external readers can grep. The event derivation sites
	// (game_render.cpp, network_server hooks) already prefix "<entity>: <goal>"
	// for decisions and "<name> took N damage|died" for combat.
	const char* cat = "EVENT";
	if      (msg.find(" died")            != std::string::npos) cat = "DEATH";
	else if (msg.find(" damage")          != std::string::npos) cat = "COMBAT";
	else if (msg.find("Picked up")        != std::string::npos) cat = "INV";
	else if (msg.find("Dropped")          != std::string::npos) cat = "INV";
	else if (msg.find("Deposited")        != std::string::npos) cat = "INV";
	else if (msg.find("broke ")           != std::string::npos) cat = "ACTION";
	else if (msg.find("placed ")          != std::string::npos) cat = "ACTION";
	else if (msg.find(": ")               != std::string::npos) cat = "DECIDE";
	GameLogger::instance().emit(cat, "%s", msg.c_str());
}

// ============================================================
// Main loop
// ============================================================
void Game::runOneFrame() {
	auto frameStart = std::chrono::steady_clock::now();

	float dt = beginFrame();
	auto afterBegin = std::chrono::steady_clock::now();

	float aspect = m_window.aspectRatio();
	handleGlobalInput();
	auto afterInput = std::chrono::steady_clock::now();

	updateAndRender(dt, aspect);
	auto afterUpdate = std::chrono::steady_clock::now();

	endFrame();
	auto frameEnd = std::chrono::steady_clock::now();

	if (m_state == GameState::PLAYING || m_state == GameState::ADMIN) {
		auto ms = [](auto a, auto b) {
			return std::chrono::duration<float, std::milli>(b - a).count();
		};
		float frameMs  = ms(frameStart, frameEnd);
		float beginMs  = ms(frameStart, afterBegin);
		float inputMs  = ms(afterBegin, afterInput);
		float updMs    = ms(afterInput, afterUpdate);
		float swapMs   = ms(afterUpdate, frameEnd);

		m_perfTimer += dt;
		if (frameMs > 33.0f) {
			m_slowFrameCount++;
			if (frameMs > m_worstFrameMs) m_worstFrameMs = frameMs;
			// Per-frame SLOW log is noisy; the 5-second summary below is enough.
		}
		if (m_perfTimer >= 5.0f) {
			if (m_slowFrameCount > 0)
				fprintf(stderr, "[Perf] Last %.0fs: %d slow frames, worst=%.1fms\n",
					m_perfTimer, m_slowFrameCount, m_worstFrameMs);
			m_perfTimer = 0;
			m_slowFrameCount = 0;
			m_worstFrameMs = 0;
		}
	}
}

void Game::run() {
#ifdef __EMSCRIPTEN__
	// Browser: yield to event loop each frame
	emscripten_set_main_loop_arg([](void* arg) {
		static_cast<Game*>(arg)->runOneFrame();
	}, this, 0, true);
#else
	// Native: blocking loop
	while (!m_window.shouldClose()) {
		runOneFrame();
	}
#endif
}

float Game::beginFrame() {
	auto now = std::chrono::steady_clock::now();
	float dt = std::min(std::chrono::duration<float>(now - m_lastTime).count(), 0.1f);
	m_lastTime = now;
	m_window.pollEvents();
	m_controls.update(m_window.handle());

	m_fpsTimer += dt;
	m_frameCount++;
	if (m_fpsTimer >= 0.5f) {
		m_currentFPS = m_frameCount / m_fpsTimer;
		m_fpsTimer = 0;
		m_frameCount = 0;
	}
	return dt;
}

void Game::handleGlobalInput() {
	if (m_controls.pressed(Action::Screenshot))
		saveScreenshot();
	if (std::filesystem::exists("/tmp/civcraft_screenshot_request")) {
		std::filesystem::remove("/tmp/civcraft_screenshot_request");
		saveScreenshot();
	}
	if (m_controls.pressed(Action::ToggleDebug))
		m_showDebug = !m_showDebug;

	// F5: toggle frame profiler
	static bool prevF5 = false;
	bool f5 = glfwGetKey(m_window.handle(), GLFW_KEY_F5) == GLFW_PRESS;
	if (f5 && !prevF5) {
		m_showProfiler = !m_showProfiler;
		printf("[Game] Frame profiler %s\n", m_showProfiler ? "ON" : "OFF");
	}
	prevF5 = f5;

	// F12: toggle admin mode (unlimited blocks, control any entity via possession)
	static bool prevF12 = false;
	bool f12 = glfwGetKey(m_window.handle(), GLFW_KEY_F12) == GLFW_PRESS;
	if (f12 && !prevF12 && (m_state == GameState::ADMIN || m_state == GameState::PLAYING)) {
		if (m_state == GameState::ADMIN) {
			m_state = GameState::PLAYING;
			m_adminFly = false;
			// Admin/fly lives on the local player body (drives canClientControl),
			// not on whatever is currently being Control-driven.
			Entity* pe = localPlayerEntity();
			if (pe) pe->setProp("fly_mode", false);
			printf("[Game] Admin mode OFF\n");
		} else {
			m_state = GameState::ADMIN;
			printf("[Game] Admin mode ON (F11=fly)\n");
		}
	}
	prevF12 = f12;

	// F11: toggle fly (admin mode only)
	static bool prevF11 = false;
	bool f11 = glfwGetKey(m_window.handle(), GLFW_KEY_F11) == GLFW_PRESS;
	if (f11 && !prevF11 && m_state == GameState::ADMIN) {
		m_adminFly = !m_adminFly;
		Entity* pe = localPlayerEntity();
		if (pe) pe->setProp("fly_mode", m_adminFly);
		printf("[Game] Fly mode %s\n", m_adminFly ? "ON" : "OFF");
	}
	prevF11 = f11;

	// Ctrl+M: toggle background music
	static bool prevM = false;
	bool mKey = glfwGetKey(m_window.handle(), GLFW_KEY_M) == GLFW_PRESS;
	bool ctrl = glfwGetKey(m_window.handle(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
	            glfwGetKey(m_window.handle(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
	if (mKey && !prevM && ctrl) {
		if (m_audio.musicPlaying()) {
			m_audio.stopMusic();
			printf("[Audio] Music OFF (Ctrl+M)\n");
		} else {
			m_audio.startMusic();
			printf("[Audio] Music ON (Ctrl+M)\n");
		}
	}
	prevM = mKey;

	// Ctrl+N: toggle effect sounds (creature, footstep, dig, etc.)
	static bool prevN = false;
	bool nKey = glfwGetKey(m_window.handle(), GLFW_KEY_N) == GLFW_PRESS;
	if (nKey && !prevN && ctrl) {
		m_audio.setEffectsMuted(!m_audio.effectsMuted());
		printf("[Audio] Effects %s (Ctrl+N)\n", m_audio.effectsMuted() ? "OFF" : "ON");
	}
	prevN = nKey;

	// Ctrl+> (Ctrl+Shift+.): skip track and disable it
	static bool prevDot = false;
	bool dotKey = glfwGetKey(m_window.handle(), GLFW_KEY_PERIOD) == GLFW_PRESS;
	bool shift = glfwGetKey(m_window.handle(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
	             glfwGetKey(m_window.handle(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
	if (dotKey && !prevDot && ctrl && shift && m_audio.musicPlaying()) {
		m_audio.skipAndDisable();
	}
	prevDot = dotKey;

	// Ctrl+< (Ctrl+Shift+,): previous track
	static bool prevComma = false;
	bool commaKey = glfwGetKey(m_window.handle(), GLFW_KEY_COMMA) == GLFW_PRESS;
	if (commaKey && !prevComma && ctrl && shift && m_audio.musicPlaying()) {
		m_audio.prevTrack();
	}
	prevComma = commaKey;

	if (m_controls.pressed(Action::ToggleInventory)) {
		
		m_equipUI.toggle();
		if (m_equipUI.isOpen())
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

void Game::endFrame() {
	m_window.swapBuffers();
}

void Game::saveScreenshot() {
	// Use std::filesystem for cross-platform temp directory
	namespace fs = std::filesystem;
	fs::path tmp;
	try { tmp = fs::temp_directory_path(); } catch (...) { tmp = "/tmp"; }
	char name[64];
	snprintf(name, sizeof(name), "civcraft_screenshot_%d.ppm", m_screenshotCounter++);
	std::string path = (tmp / name).string();

	writeScreenshot(m_window.width(), m_window.height(), path.c_str());
	printf("Screenshot saved: %s\n", path.c_str());

	// Best-effort clipboard copy (platform-specific)
#if defined(_WIN32)
	// Windows: no easy PPM clipboard; skip
#elif defined(__APPLE__)
	{
		// macOS: convert PPM to TIFF via sips and set clipboard with osascript
		char cmd[512];
		std::string tiff = (tmp / "civcraft_ss_tmp.tiff").string();
		snprintf(cmd, sizeof(cmd), "sips -s format tiff '%s' --out '%s' 2>/dev/null && "
		         "osascript -e 'set the clipboard to (read file \"%s\" as TIFF picture)' 2>/dev/null",
		         path.c_str(), tiff.c_str(), tiff.c_str());
		system(cmd);
	}
#elif defined(__EMSCRIPTEN__)
	// Web: skip clipboard; file is in browser virtual FS
#else
	// Linux: try xclip
	{
		char cmd[512];
		snprintf(cmd, sizeof(cmd), "xclip -selection clipboard -t image/x-portable-pixmap -i '%s' 2>/dev/null",
		         path.c_str());
		system(cmd);
	}
#endif
}

// ============================================================
// State dispatch
// ============================================================
void Game::updateAndRender(float dt, float aspect) {
	switch (m_state) {
	case GameState::MENU: {
		// Show cursor for menu interaction
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

		// ImGui-based menu
		m_ui.beginFrame();
		auto action = m_imguiMenu.render(m_artifacts,
			(float)m_window.width(), (float)m_window.height());
		m_ui.endFrame();
		handleMenuAction(action);
		// External screenshot trigger (same as in-game path) — useful for
		// menu-layout regression capture.
		if (std::filesystem::exists("/tmp/civcraft_screenshot_request")) {
			std::filesystem::remove("/tmp/civcraft_screenshot_request");
			saveScreenshot();
		}
		break;
	}
	// Legacy screens — redirect to ImGui menu
	case GameState::SERVER_BROWSER:
	case GameState::TEMPLATE_SELECT:
	case GameState::CONTROLS:
	case GameState::CHARACTER:
		m_state = GameState::MENU;
		break;
	case GameState::LOADING:
		updateLoading(dt, aspect);
		break;
	case GameState::DISCONNECTED:
		updateDisconnected(dt, aspect);
		break;
	case GameState::ADMIN:
	case GameState::PLAYING:
		updatePlaying(dt, aspect);
		break;
	case GameState::ENTITY_INSPECT:
		updateEntityInspect(dt, aspect);
		break;
	case GameState::CODE_EDITOR:
		updateCodeEditor(dt, aspect);
		break;
	case GameState::PAUSED:
		updatePaused(dt, aspect);
		break;
	case GameState::CONNECTING: {
		// Web: poll WebSocket each frame until S_WELCOME arrives
		m_connectTimer += dt;
		const float kConnectTimeout = 10.0f;

		glClearColor(0.12f, 0.13f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		m_ui.beginFrame();
		ImGui::SetNextWindowPos(ImVec2((float)m_window.width() * 0.5f,
		                               (float)m_window.height() * 0.5f),
		                        ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(340, 90), ImGuiCond_Always);
		ImGui::Begin("##connecting", nullptr,
		    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		    ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar);
		int dots = (int)(m_connectTimer * 2) % 4;
		char buf[64];
		snprintf(buf, sizeof(buf), "Connecting%s", std::string(dots, '.').c_str());
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(buf).x) * 0.5f);
		ImGui::Text("%s", buf);
		ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 80) * 0.5f);
		if (ImGui::Button("Cancel", ImVec2(80, 0))) {
			m_server.reset();
			m_state = GameState::MENU;
		}
		ImGui::End();
		m_ui.endFrame();

		if (m_connectTimer >= kConnectTimeout) {
			printf("[Game] Connection timed out\n");
			m_server.reset();
			m_state = GameState::MENU;
			break;
		}

		if (auto* net = dynamic_cast<NetworkServer*>(m_server.get())) {
			if (net->pollWelcome()) {
				setupAfterConnect(m_connectTargetState);
			} else if (!net->isConnected()) {
				m_server.reset();
				m_state = GameState::MENU;
			}
		}
		break;
	}
	}
}

void Game::handleMenuAction(const MenuAction& action) {
	switch (action.type) {
	case MenuAction::None:
		break;
	case MenuAction::Quit:
		glfwSetWindowShouldClose(m_window.handle(), true);
		break;
	// Legacy actions — no longer emitted by ImGui menu
	case MenuAction::StartGame:
	case MenuAction::ShowControls:
	case MenuAction::ShowCharacter:
		m_state = GameState::MENU;
		break;
	case MenuAction::BackToMenu:
		m_state = GameState::MENU;
		break;
	case MenuAction::EnterGame:
		m_currentWorldPath = action.worldPath;
		m_currentSeed = action.seed ? action.seed : (int)std::random_device{}();
		enterGame(action.templateIndex, action.targetState, action.worldGenConfig);
		break;
	case MenuAction::JoinServer:
		joinServer(action.serverHost, action.serverPort, action.targetState);
		break;
	case MenuAction::ResumeGame:
		if (m_server) {
			m_state = m_preMenuState;
			bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
			                    m_camera.mode == CameraMode::ThirdPerson);
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
				needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
			m_camera.resetMouseTracking();
		}
		break;
	case MenuAction::LoadWorld: {
		m_currentWorldPath = action.worldPath;
		m_currentSeed = 0;  // will be loaded from save
		printf("[Game] Loading world from %s\n", action.worldPath.c_str());
		enterGame(action.templateIndex, GameState::LOADING);
		break;
	}
	case MenuAction::DeleteWorld:
		m_imguiMenu.worldManager().deleteWorld(action.worldPath);
		m_imguiMenu.worldManager().refresh();
		break;
	}
}

void Game::joinServer(const std::string& host, int port, GameState targetState) {
	printf("[Game] Joining server at %s:%d\n", host.c_str(), port);
	m_reconnectHost = host;  // remember for auto-reconnect on mid-game disconnect
	m_reconnectPort = port;

	// Async connect for both native and web. The sync createGame() path used
	// to block for up to 60s during the server's async Preparing phase — no
	// UI could render. Now we beginConnect() (sends C_HELLO, returns
	// immediately), hop straight to LOADING, and updateLoading() polls for
	// S_WELCOME each frame while showing the S_PREPARING progress bar.
	//
	// Native keeps its retry-on-refused-connect behaviour since the server
	// may still be starting; beginConnect() itself is the retryable part.
#ifndef __EMSCRIPTEN__
	for (int attempt = 0; attempt < 5; attempt++) {
		if (attempt > 0) {
			printf("[Game] Connect attempt %d/5 failed, retrying in 200ms...\n", attempt);
			usleep(200000);
		}
#endif
		auto netServer = std::make_unique<NetworkServer>(host, port);
		netServer->setDisplayName(m_playerName);
		netServer->setCreatureType(m_selectedCreature);
		if (netServer->beginConnect()) {
			m_server = std::move(netServer);
			m_connectTargetState = targetState;
			m_state = targetState;     // LOADING — updateLoading() polls welcome
			m_connectTimer = 0;
			m_handshake = HandshakeProgress{}; // fresh milestone tracker per connect
			return;
		}
#ifndef __EMSCRIPTEN__
	}
	printf("[Game] Failed to join %s:%d after 5 attempts\n", host.c_str(), port);
#else
	printf("[Game] Failed to begin connect to %s:%d\n", host.c_str(), port);
#endif
	m_state = GameState::MENU;
}

// ============================================================
// World creation — always creates a local server
// ============================================================
void Game::enterGame(int templateIndex, GameState targetState, const WorldGenConfig& wgc) {
	printf("[Game] Starting game (server + bot processes)\n");

	// Stop any existing processes
	m_agentMgr.stopAll();

	// Launch server process
	AgentManager::Config cfg;
	cfg.seed = m_currentSeed;
	cfg.templateIndex = templateIndex;
	cfg.worldPath = m_currentWorldPath;
	cfg.execDir = m_execDir;
	cfg.port = m_serverPort;

	int port = m_agentMgr.launchServer(cfg);
	if (port < 0) {
		fprintf(stderr, "[Game] Failed to launch civcraft-server — binary missing or no port available\n");
		// TODO: surface error in UI rather than silently returning to menu
		return;
	}

	// Connect to localhost as a regular network client.
	// AI agent processes are spawned by the server automatically.
	joinServer("127.0.0.1", port, targetState);
}

void Game::setupAfterConnect(GameState targetState) {
	m_connectTimer = 0;        // always start entity-wait timer fresh
	m_reconnectAttempt = 0;    // successful connect resets the retry counter

	// Set callbacks for visual + audio effects
	m_server->setEffectCallbacks(
		// onChunkDirty: mark chunk for remesh when block data changes
		[this](ChunkPos cp) { m_renderer.markChunkDirty(cp); },
		// onBlockBreakText: block break confirmed by server — show HUD text
		[this](glm::vec3 pos, const std::string& blockName) {
			std::string name = blockName;
			if (!name.empty()) name[0] = (char)toupper((unsigned char)name[0]);
			for (auto& c : name) if (c == '_') c = ' ';
			FloatTextEvent ft;
			ft.source      = FloatSource::BlockBreak;
			ft.worldPos    = pos + glm::vec3(0.5f, 1.5f, 0.5f);
			ft.coalesceKey = blockName;
			ft.text        = name;
			ft.value       = 1.0f;
			m_floatText.add(ft);
			// Log: ACTION entry (category derived from "broke ")
			char buf[160];
			snprintf(buf, sizeof(buf), "broke %s @(%d,%d,%d)",
				blockName.c_str(), (int)pos.x, (int)pos.y, (int)pos.z);
			appendLog(buf);
		},
		// onBlockPlace: play placement sound (sound_place field from BlockDef)
		[this](glm::vec3 pos, const std::string& soundPlace) {
			std::string sound = soundPlace.empty() ? "place_stone" : soundPlace;
			if (sound.find("wood") != std::string::npos)       sound = "place_wood";
			else if (sound.find("dirt") != std::string::npos ||
			         sound.find("sand") != std::string::npos)  sound = "place_soft";
			else                                                sound = "place_stone";
			m_audio.play(sound, pos, 0.5f);
		}
	);

	// Repopulate the client-only hotbar whenever the server pushes a fresh
	// inventory snapshot for the local player. Also log inventory deltas
	// for every entity (so we can see "woodcutter picked up base:logs ×3"
	// and "woodcutter deposited base:logs ×5" in the headless log stream).
	m_server->setInventoryCallback([this](EntityId eid) {
		if (!m_server) return;
		Entity* ent = m_server->getEntity(eid);
		if (!ent) return;
		if (eid == m_server->localPlayerId() && ent->inventory)
			m_hotbar.repopulateFrom(*ent->inventory);

		// Inventory-delta log. Compare against last snapshot per entity.
		if (!ent->inventory) return;
		auto& prev = m_prevInv[eid];
		std::unordered_map<std::string,int> cur;
		for (auto& [iid, cnt] : ent->inventory->items()) cur[iid] = cnt;
		std::string typeName = ent->typeId();
		auto col = typeName.find(':');
		if (col != std::string::npos) typeName = typeName.substr(col + 1);
		if (!typeName.empty()) typeName[0] = (char)toupper((unsigned char)typeName[0]);
		auto diff = [&](const std::string& iid, int delta) {
			char buf[160];
			const char* verb = delta > 0 ? "Picked up" : "Dropped";
			int n = delta > 0 ? delta : -delta;
			snprintf(buf, sizeof(buf), "%s #%u %s %s x%d",
				typeName.c_str(), eid, verb, iid.c_str(), n);
			appendLog(buf);
		};
		for (auto& [iid, cnt] : cur) {
			int was = 0;
			auto it = prev.find(iid);
			if (it != prev.end()) was = it->second;
			if (cnt != was) diff(iid, cnt - was);
		}
		for (auto& [iid, was] : prev) {
			if (cur.find(iid) == cur.end() && was != 0) diff(iid, -was);
		}
		prev.swap(cur);
	});

	// Initial S_INVENTORY for the local player often arrives during
	// pollWelcome() — BEFORE this callback is registered. Catch up by
	// repopulating from whatever inventory the server has already sent.
	if (Entity* me = m_server->getEntity(m_server->localPlayerId()))
		if (me->inventory) m_hotbar.repopulateFrom(*me->inventory);

	m_state = targetState;
	glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	m_camera.mode = CameraMode::FirstPerson;

	// Background music is off by default; player can enable via Ctrl+M or Settings.

	glm::vec3 spawn = m_server->spawnPos();
	m_camera.player.feetPos = spawn;

	// Face +Z: toward portal stairs and village beyond
	float spawnYaw = 90.0f;
	m_camera.player.yaw = spawnYaw;
	m_camera.lookYaw = spawnYaw;
	m_camera.lookPitch = -5;
	m_camera.resetSmoothing();
	// Pre-initialize RTS/RPG camera centers to player spawn so switching
	// to those modes looks at the right place from the start.
	m_camera.rtsCenter = spawn;

	ChunkSource& chunks = m_server->chunks();
	int sx = (int)spawn.x, sh = (int)spawn.y, sz = (int)spawn.z;
	chunks.ensureChunksAround(worldToChunk(sx, sh, sz), 8);
	m_renderer.meshAllPending(chunks, m_camera, m_renderDistance);

	m_worldTime = 0.25f; // dawn — server will sync via S_TIME
	m_playerWalkDist = 0;
	m_globalTime = 0;

	// Create AgentClient for controlling owned NPCs
	m_agentClient = std::make_unique<AgentClient>(*m_server, m_behaviorStore);
	m_gameplay.setAgentClient(m_agentClient.get());

	// Wire server-broadcast interrupts (S_NPC_INTERRUPT, S_WORLD_EVENT) to
	// the agent client. Only NetworkServer delivers these — TestServer is
	// headless and does not broadcast them.
	if (auto* net = dynamic_cast<NetworkServer*>(m_server.get())) {
		AgentClient* ac = m_agentClient.get();
		net->setInterruptHandlers(
			[ac](EntityId eid, const std::string& reason) {
				ac->onInterrupt(eid, reason);
			},
			[ac](const std::string& kind, const std::string& payload) {
				ac->onWorldEvent(kind, payload);
			});
	}
}

void Game::saveCurrentWorld() {
	// The server process (civcraft-server) saves world data on shutdown.
	// stopAll() sends SIGTERM which triggers main_server.cpp's save handler.
	// Nothing to do here on the client side.
	if (!m_currentWorldPath.empty())
		m_imguiMenu.worldManager().refresh();
}

// NOTE: updatePlaying, renderPlaying → game_playing.cpp / game_render.cpp
// NOTE: updateEntityInspect, updateCodeEditor, updatePaused → game_ui.cpp

} // namespace civcraft
