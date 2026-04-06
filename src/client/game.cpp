#include "client/game.h"
#include "server/entity_manager.h"
#include "shared/constants.h"
#include "shared/model_loader.h"
#include "server/world_save.h"
#include "shared/physics.h"
#include "client/network_server.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <fstream>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace agentica {

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
	printf("=== Agentica v0.9.0 ===\n");

	// Determine executable directory (for launching server/bot processes)
	if (argc > 0) {
		std::string exe = argv[0];
		auto pos = exe.rfind('/');
		m_execDir = (pos != std::string::npos) ? exe.substr(0, pos) : ".";
	}

	if (!m_window.init(1600, 900, "Agentica")) return false;
	if (!m_renderer.init("shaders")) return false;
	if (!m_text.init("shaders")) return false;
	if (!m_particles.init("shaders")) return false;

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
		std::make_shared<FlatWorldTemplate>(),
		std::make_shared<VillageWorldTemplate>(),
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
			else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) m_connectHost = argv[++i];
			else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
				m_connectPort = atoi(argv[++i]);
				m_serverPort = m_connectPort;
			}
			else if (strcmp(argv[i], "--debug-scenario") == 0 && i + 1 < argc) {
				dbgCfg.scenario = argv[++i];
				dbgCfg.active = true;
				m_skipMenu = true;  // scenario implies --skip-menu
			}
			else if (strcmp(argv[i], "--debug-item") == 0 && i + 1 < argc) {
				dbgCfg.targetItem = argv[++i];
			}
		}
		if (dbgCfg.active) {
			m_debugCapture.configure(dbgCfg);
			printf("[Debug] Scenario '%s' for item '%s'\n",
			       dbgCfg.scenario.c_str(), dbgCfg.targetItem.c_str());
		}
	}

	// Models — ALL loaded from Python (artifacts/models/base/ and models/player/)
	m_models = model_loader::loadAllModels("artifacts");
	printf("[Game] Loaded %zu models from Python\n", m_models.size());

	// Validate: every artifact and block type should have a model.
	// Missing models get the magenta "?" placeholder and a console warning.
	{
		int missing = 0;
		// Check artifacts (creatures, characters, items)
		for (auto* cat : {"creature", "character", "item"}) {
			for (auto* entry : m_artifacts.byCategory(cat)) {
				std::string key = entry->name;
				for (auto& c : key) c = (char)std::tolower((unsigned char)c);
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

	// Auto-select first character if none set
	if (m_selectedCreature.empty()) {
		auto chars = m_artifacts.byCategory("character");
		if (!chars.empty()) m_selectedCreature = chars[0]->id;
		else m_selectedCreature = "base:player"; // fallback
	}

	// Register ALL models for Handbook 3D preview
	auto& hb = m_imguiMenu.handbook();
	hb.setPreview(&m_modelPreview, &m_renderer.modelRenderer());
	hb.setRegistry(&m_artifacts);
	hb.setAudio(&m_audio);

	// Register ALL Python models for Handbook 3D preview
	for (auto& [key, mdl] : m_models)
		hb.registerModel(key, mdl);

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
			d->cam->rtsHeightTarget = std::clamp(d->cam->rtsHeightTarget - (float)y * 3, 15.0f, 80.0f);
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
		joinServer(m_connectHost, m_connectPort, GameState::PLAYING);
	// --skip-menu alone: always start a fresh world (no save loading for debug)
	} else if (m_skipMenu) {
		printf("[Game] --skip-menu: starting new world directly\n");
		m_currentWorldPath = "";   // force new world, never resume a save
		m_currentSeed = (int)std::random_device{}();
		enterGame(1, GameState::PLAYING);
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

	if (m_serverLog) { fclose(m_serverLog); m_serverLog = nullptr; }
	m_audio.shutdown();
	m_ui.shutdown();
	m_hud.shutdown();
	m_particles.shutdown();
	m_text.shutdown();
	m_renderer.shutdown();
	m_window.shutdown();
}


void Game::appendLog(const std::string& msg) {
	// Game-time timestamp: map worldTime [0..1] to HH:MM
	int hours = (int)(m_worldTime * 24.0f) % 24;
	int mins  = (int)(m_worldTime * 24.0f * 60.0f) % 60;
	char entry[280];
	snprintf(entry, sizeof(entry), "[%02d:%02d] %s", hours, mins, msg.c_str());
	m_gameLog.push_back(entry);
	if (m_gameLog.size() > 200) m_gameLog.pop_front();
}

// ============================================================
// Main loop
// ============================================================
void Game::runOneFrame() {
	float dt = beginFrame();
	float aspect = m_window.aspectRatio();
	handleGlobalInput();
	updateAndRender(dt, aspect);
	endFrame();
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
	if (m_controls.pressed(Action::ToggleDebug))
		m_showDebug = !m_showDebug;

	// F12: toggle admin mode (fly, unlimited blocks, debug)
	static bool prevF12 = false;
	bool f12 = glfwGetKey(m_window.handle(), GLFW_KEY_F12) == GLFW_PRESS;
	if (f12 && !prevF12 && (m_state == GameState::ADMIN || m_state == GameState::PLAYING)) {
		if (m_state == GameState::ADMIN) {
			m_state = GameState::PLAYING;
			printf("[Game] Admin mode OFF\n");
		} else {
			m_state = GameState::ADMIN;
			printf("[Game] Admin mode ON (fly, unlimited blocks)\n");
		}
	}
	prevF12 = f12;

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
	char p[256];
	snprintf(p, 256, "/tmp/agentica_screenshot_%d.ppm", m_screenshotCounter++);
	writeScreenshot(m_window.width(), m_window.height(), p);
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
		break;
	}
	// Legacy screens — redirect to ImGui menu
	case GameState::SERVER_BROWSER:
	case GameState::TEMPLATE_SELECT:
	case GameState::CONTROLS:
	case GameState::CHARACTER:
		m_state = GameState::MENU;
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
		enterGame(action.templateIndex, GameState::PLAYING);
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

#ifdef __EMSCRIPTEN__
	// Web: WebSocket connect is async — initiate and wait across frames
	auto netServer = std::make_unique<NetworkServer>(host, port);
	netServer->setDisplayName(m_playerName);
	netServer->setCreatureType(m_selectedCreature);
	if (netServer->beginConnect()) {
		m_server = std::move(netServer);
		m_connectTargetState = targetState;
		m_state = GameState::CONNECTING;
		m_connectTimer = 0;
	} else {
		printf("[Game] Failed to begin connect to %s:%d\n", host.c_str(), port);
		m_state = GameState::MENU;
	}
#else
	// Native: retry up to 5 times with 200ms delay (server may just be starting)
	for (int attempt = 0; attempt < 5; attempt++) {
		if (attempt > 0) {
			printf("[Game] Connect attempt %d/5 failed, retrying in 200ms...\n", attempt);
			usleep(200000);
		}
		auto netServer = std::make_unique<NetworkServer>(host, port);
		netServer->setDisplayName(m_playerName);
		netServer->setCreatureType(m_selectedCreature);
		if (netServer->createGame(42, 0)) {
			printf("[Game] Connected to %s:%d as %s (%s)\n",
			       host.c_str(), port, m_playerName.c_str(), m_selectedCreature.c_str());
			m_server = std::move(netServer);
			setupAfterConnect(targetState);
			return;
		}
	}
	printf("[Game] Failed to join %s:%d after 5 attempts\n", host.c_str(), port);
	// Stay in menu on failure
	m_state = GameState::MENU;
#endif
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
		printf("[Game] Failed to launch server, falling back to local server\n");
		// Fallback to LocalServer (e.g., if binaries not found)
		auto localServer = std::make_unique<LocalServer>(m_templates);
		localServer->setCreatureType(m_selectedCreature);
		localServer->createGame(m_currentSeed, templateIndex, wgc);
		m_server = std::move(localServer);
		setupAfterConnect(targetState);
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
		[this](ChunkPos cp) { m_renderer.markChunkDirty(cp); },
		[this](glm::vec3 pos, glm::vec3 color, int count) {
			m_particles.emitBlockBreak(pos, color, count);
			// Play block break sound based on block color heuristic
			// (the callback only gives us color, not block type)
			// Brown/green = dirt/grass, gray = stone, tan = sand, white = snow, brown = wood
			float r = color.r, g = color.g, b = color.b;
			if (r > 0.7f && g > 0.7f && b > 0.7f) {
				m_audio.play("dig_snow", pos, 0.6f);
			} else if (r > 0.6f && g > 0.5f && b < 0.4f) {
				m_audio.play("dig_sand", pos, 0.6f);
			} else if (r < 0.5f && g < 0.5f && b < 0.5f) {
				m_audio.play("dig_stone", pos, 0.7f);
			} else if (r > 0.3f && g < 0.35f && b < 0.25f) {
				m_audio.play("dig_wood", pos, 0.6f);
			} else if (g > r && g > b) {
				m_audio.play("dig_leaves", pos, 0.5f);
			} else {
				m_audio.play("dig_dirt", pos, 0.6f);
			}
		},
		[this](glm::vec3 pos, glm::vec3 color) {
			m_particles.emitItemPickup(pos, color);
			m_audio.play("item_pickup", pos, 0.4f);
		},
		[this](glm::vec3 pos, const std::string& soundPlace) {
			std::string sound = soundPlace.empty() ? "place_stone" : soundPlace;
			// Map sound_place names to our group names
			if (sound.find("wood") != std::string::npos)
				sound = "place_wood";
			else if (sound.find("dirt") != std::string::npos || sound.find("sand") != std::string::npos)
				sound = "place_soft";
			else
				sound = "place_stone";
			m_audio.play(sound, pos, 0.5f);
		}
	);

	// Floating text callbacks (item pickup + block break names)
	if (auto* local = dynamic_cast<LocalServer*>(m_server.get())) {
		if (local->server()) {
			auto& cb = local->server()->callbacks();
			cb.onPickupText = [this](glm::vec3 pos, const std::string& item, int count) {
				std::string name = item;
				if (name.size() > 5 && name.substr(0,5) == "base:") name = name.substr(5);
				if (!name.empty()) name[0] = (char)toupper((unsigned char)name[0]);
				for (auto& c : name) if (c == '_') c = ' ';
				if (m_serverLog) fprintf(m_serverLog, "[pickup] %s x%d at (%.0f,%.0f,%.0f)\n",
					item.c_str(), count, pos.x, pos.y, pos.z);

				// Floating pickup notification
				FloatTextEvent ft;
				ft.type        = FloatTextType::Pickup;
				ft.worldPos    = pos;
				ft.coalesceKey = item;  // same item type → coalesce
				ft.color       = {0.85f, 1.0f, 0.55f, 1.0f};
				ft.text        = "+" + std::to_string(count) + " " + name;
				m_floatText.add(ft);
			};
			cb.onBreakText = [this](glm::vec3 pos, const std::string& blockName) {
				if (m_serverLog) fprintf(m_serverLog, "[break] %s at (%.0f,%.0f,%.0f)\n",
					blockName.c_str(), pos.x, pos.y, pos.z);
			};
			cb.onPickupDenied = [this](glm::vec3, const std::string&) {
			};
		}
	}

	m_state = targetState;
	glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	m_camera.mode = CameraMode::FirstPerson;

	// Start background music
	if (!m_audio.musicPlaying())
		m_audio.startMusic();

	glm::vec3 spawn = m_server->spawnPos();
	m_camera.player.feetPos = spawn;

	// Default: face +Z (yaw=-90); override for village worlds to face the village
	float spawnYaw = -90.0f;
	if (auto* ls = dynamic_cast<LocalServer*>(m_server.get())) {
		if (ls->server()) {
			World& w = ls->server()->world();
			auto& tmpl = w.getTemplate();
			if (tmpl.pyConfig().hasVillage) {
				auto vc = tmpl.villageCenter(w.seed());
				float dx = (float)vc.x - spawn.x;
				float dz = (float)vc.y - spawn.z;
				float len = std::sqrt(dx*dx + dz*dz);
				if (len > 0.001f) {
					// Camera yaw convention: fwd = (-cos(yaw_rad), 0, -sin(yaw_rad))
					// Solve for yaw so that fwd = (dx/len, 0, dz/len):
					//   -cos(yaw) = dx/len, -sin(yaw) = dz/len
					//   yaw_rad = atan2(-dz/len, -dx/len)
					spawnYaw = std::atan2(-dz / len, -dx / len) * (180.0f / 3.14159265f);
				}
			}
		}
	}
	m_camera.player.yaw = spawnYaw;
	m_camera.lookYaw = spawnYaw;
	m_camera.lookPitch = -5;
	m_camera.resetSmoothing();
	m_camera.rtsCenter = spawn;

	ChunkSource& chunks = m_server->chunks();
	int sx = (int)spawn.x, sh = (int)spawn.y, sz = (int)spawn.z;
	chunks.ensureChunksAround(worldToChunk(sx, sh, sz), 8);
	m_renderer.meshAllPending(chunks, m_camera, m_renderDistance);

	m_worldTime = 0.30f;
	m_playerWalkDist = 0;
	m_globalTime = 0;
}

void Game::saveCurrentWorld() {
	if (m_currentWorldPath.empty() || !m_server) return;
	auto* ls = dynamic_cast<LocalServer*>(m_server.get());
	if (!ls || !ls->server()) return;

	WorldMetadata meta;
	meta.name = m_currentWorldPath.substr(m_currentWorldPath.rfind('/') + 1);
	meta.seed = ls->server()->world().seed();
	meta.templateIndex = ls->server()->world().templateIndex();
	meta.gameMode = "playing";
	meta.version = 1;

	// Get template name from index
	if (meta.templateIndex < (int)m_templates.size())
		meta.templateName = m_templates[meta.templateIndex]->name();

	saveWorld(*ls->server(), m_currentWorldPath, meta);
	m_imguiMenu.worldManager().refresh();
}

// ============================================================
// Playing state
// ============================================================
void Game::updatePlaying(float dt, float aspect) {
	// Helper: handle a lost connection — attempt reconnect or fall back to menu.
	// Only reconnects for NetworkServer (remote host stored in m_reconnectHost).
	auto handleDisconnect = [&]() {
		m_server.reset(); // always close socket cleanly via RAII destructor
		bool isNetworkGame = !m_reconnectHost.empty();
		if (isNetworkGame && m_reconnectAttempt < kMaxReconnectAttempts) {
			m_reconnectAttempt++;
			printf("[Game] Connection lost — reconnecting (%d/%d) to %s:%d...\n",
			       m_reconnectAttempt, kMaxReconnectAttempts,
			       m_reconnectHost.c_str(), m_reconnectPort);
			joinServer(m_reconnectHost, m_reconnectPort, GameState::PLAYING);
		} else {
			if (isNetworkGame)
				printf("[Game] Reconnect attempts exhausted, returning to menu\n");
			m_reconnectAttempt = 0;
			m_state = GameState::MENU;
		}
	};

	if (!m_server || !m_server->isConnected()) {
		handleDisconnect();
		return;
	}

	// Tick server (polls for entity updates from network)
	m_server->tick(dt);

	// Connection may have dropped during tick — handle immediately
	if (!m_server->isConnected()) {
		handleDisconnect();
		return;
	}

	Entity* pe = playerEntity();
	if (!pe) {
		// Player entity not received yet — wait for server to broadcast it.
		// This happens on network clients: S_WELCOME arrives before S_ENTITY.
		m_connectTimer += dt;
		if (m_connectTimer > 10.0f) {
			printf("[Game] Timeout waiting for player entity\n");
			handleDisconnect(); // resets m_server, may reconnect
			return;
		}
		// Show loading message
		m_ui.beginFrame();
		ImGui::SetNextWindowPos(ImVec2(m_window.width() * 0.5f - 100, m_window.height() * 0.5f - 20));
		ImGui::Begin("##connecting", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::Text("Connecting to server...");
		ImGui::End();
		m_ui.endFrame();
		return;
	}
	m_connectTimer = 0;

	// Floating text update
	m_floatText.update(dt, m_camera.mode);

	// Debug capture tick — runs scenario and auto-exits when done
	if (m_debugCapture.active()) {
		development::ScenarioCallbacks cb;

		// save: receives a full /tmp/debug_N_<suffix>.ppm path from DebugCapture
		cb.save = [this](const std::string& path) {
			writeScreenshot(m_window.width(), m_window.height(), path.c_str());
		};
		cb.cycleCamera = [this]() {
			m_camera.cycleMode();
			m_camera.resetMouseTracking();
			bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
			                    m_camera.mode == CameraMode::ThirdPerson);
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
				needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		};
		cb.setCamera = [this](CameraMode mode) {
			while (m_camera.mode != mode) {
				m_camera.cycleMode();
				m_camera.resetMouseTracking();
			}
			bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
			                    m_camera.mode == CameraMode::ThirdPerson);
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
				needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		};
		cb.selectSlot = [pe](int slot) {
			pe->setProp(Prop::SelectedSlot, slot);
		};
		cb.dropItem = [this, pe]() {
			int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
			std::string itemId = pe->inventory ? pe->inventory->hotbar(slot) : "";
			if (!itemId.empty() && pe->inventory->has(itemId)) {
				ActionProposal drop;
				drop.type = ActionProposal::DropItem;
				drop.actorId = m_server->localPlayerId();
				drop.blockType = itemId;
				drop.itemCount = 1;
				// Debug: gentle forward drop so item lands ~1.5 blocks ahead (in RPG camera view)
				drop.desiredVel = m_debugCapture.active()
					? glm::vec3(std::cos(glm::radians(pe->yaw)) * 1.0f, 1.5f,
					            std::sin(glm::radians(pe->yaw)) * 1.0f)
					: m_camera.front() * 3.0f + glm::vec3(0, 2.0f, 0);
				m_server->sendAction(drop);
				m_dropCooldown = 0.8f;
			}
		};
		cb.triggerSwing = [this]() {
			m_fpSwingActive = true;
			m_fpSwingTimer = 0;
		};

		m_debugCapture.tick(dt, pe, m_camera, cb);
		if (m_debugCapture.done()) {
			printf("[Debug] Scenario complete — exiting.\n");
			glfwSetWindowShouldClose(m_window.handle(), true);
		}
	}

	if (m_controls.pressed(Action::MenuBack)) {
		// ESC closes overlays first, then shows pause menu
		if (m_equipUI.isOpen()) {
			m_equipUI.close();
			bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
			                    m_camera.mode == CameraMode::ThirdPerson);
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
				needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
			return;
		}
		m_preMenuState = m_state;
		m_state = GameState::PAUSED;
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		return;
	}

	// Tell gameplay if UI wants the cursor (inventory, ImGui, etc.)
	m_gameplay.setUIWantsCursor(m_equipUI.isOpen() || m_ui.wantsMouse());

	// Client-side: gather input → ActionProposals (works for local AND network)
	float jumpVel = 10.5f; // tuned for gravity=32: reaches ~1.7 blocks
	m_gameplay.update(dt, m_state, *m_server, *pe, m_camera, m_controls,
	                  m_renderer, m_particles, m_window, jumpVel);

	// First-person attack swing animation (block-break or entity attack)
	if (m_gameplay.swingTriggered()) {
		m_fpSwingDuration = 0.25f; // block-break: fast, fixed duration
		m_fpSwingActive = true;
		m_fpSwingTimer = 0;
		m_gameplay.clearSwing();
	}
	if (m_fpSwingActive) {
		m_fpSwingTimer += dt;
		if (m_fpSwingTimer >= m_fpSwingDuration) {
			m_fpSwingActive = false;
			m_fpSwingTimer = 0;
		}
	}

	// ── Entity attack: dispatch ActionProposal::Attack when player left-clicks entity ──
	m_attackCD -= dt;
	{
		EntityId attackId = m_gameplay.attackTarget();
		m_gameplay.clearAttack();
		if (attackId != ENTITY_NONE && pe->inventory && m_attackCD <= 0) {
			int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
			const std::string& itemId = pe->inventory->hotbar(slot);

			// Defaults: bare fist
			float damage  = 1.5f;
			float cooldown = 0.4f;
			float range   = 2.5f;
			bool canAttack = itemId.empty(); // empty hand = fist, always allows attack

			if (!itemId.empty()) {
				const ArtifactEntry* entry = m_artifacts.findById(itemId);
				if (entry) {
					auto it = entry->fields.find("on_interact");
					if (it != entry->fields.end() && it->second == "attack") {
						canAttack = true;
						auto dit = entry->fields.find("damage");
						if (dit != entry->fields.end()) damage = std::stof(dit->second);
						auto cit = entry->fields.find("cooldown");
						if (cit != entry->fields.end()) cooldown = std::stof(cit->second);
						auto rit = entry->fields.find("range");
						if (rit != entry->fields.end()) range = std::stof(rit->second);
					}
					// Item without on_interact=attack (e.g. potion): canAttack stays false
				}
			}

			if (canAttack) {
				Entity* target = m_server->getEntity(attackId);
				if (target) {
					float dist = glm::length(target->position - pe->position);
					if (dist <= range) {
						ActionProposal p;
						p.type       = ActionProposal::Attack;
						p.actorId    = pe->id();
						p.targetEntity = attackId;
						p.damage     = damage;
						m_server->sendAction(p);
						m_attackCD = cooldown;
						m_renderer.triggerHitmarker(false); // flash crosshair orange on attack
						// Swing duration: 60% of cooldown, capped at 0.45s (fast stab/slow slash)
						m_fpSwingDuration = std::min(cooldown * 0.6f, 0.45f);
						m_fpSwingActive = true;
						m_fpSwingTimer  = 0;
						// Swing sound: weapon whoosh or fist
						if (itemId.empty())
							m_audio.play("hit_punch", pe->position, 0.35f);
						else
							m_audio.play("sword_swing", pe->position, 0.55f);
					}
				}
			}
		}
	}

	// ── Item actions: Q=drop, E=equip, right-click=use ──
	if (pe->inventory && (m_state == GameState::PLAYING || m_state == GameState::ADMIN)) {
		int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
		std::string heldItem = pe->inventory->hotbar(slot);

		// Q = drop selected item
		m_dropCooldown -= dt;
		if (m_controls.pressed(Action::DropItem) && !heldItem.empty() && pe->inventory->has(heldItem)) {
			ActionProposal p;
			p.type = ActionProposal::DropItem;
			p.actorId = m_server->localPlayerId();
			p.blockType = heldItem;
			p.itemCount = 1;
			// Toss toward where the camera is looking
			p.desiredVel = m_camera.front() * 5.0f + glm::vec3(0, 3.0f, 0);
			m_server->sendAction(p);
			m_dropCooldown = 0.8f; // don't auto-pickup for 0.8s after dropping
		}

		// E = equip selected item
		if (m_controls.pressed(Action::EquipItem) && !heldItem.empty()) {
			const ArtifactEntry* art = m_artifacts.findById(heldItem);
			if (art) {
				auto slotIt = art->fields.find("equip_slot");
				if (slotIt != art->fields.end()) {
					printf("[Equip] '%s' → slot '%s'\n", heldItem.c_str(), slotIt->second.c_str());
					ActionProposal p;
					p.type = ActionProposal::EquipItem;
					p.actorId = m_server->localPlayerId();
					p.slotIndex = slot;
					p.blockType = slotIt->second;
					m_server->sendAction(p);
					// Metal items (sword, shield, helmet) use chain clink; others use cloth
					bool isMetal = heldItem.find("sword") != std::string::npos
					            || heldItem.find("shield") != std::string::npos
					            || heldItem.find("helmet") != std::string::npos
					            || heldItem.find("boots") != std::string::npos;
					m_audio.play(isMetal ? "item_equip_metal" : "item_equip", 0.55f);
				}
			}
		}

		// Right-click: use/eat/drink item (on_use = consume)
		// Only fires when not aiming at a block (block place takes priority).
		if (m_controls.pressed(Action::PlaceBlock) && !heldItem.empty()
		    && pe->inventory->has(heldItem) && !m_gameplay.currentHit()) {
			const ArtifactEntry* art = m_artifacts.findById(heldItem);
			if (art) {
				auto usIt = art->fields.find("on_use");
				if (usIt != art->fields.end() && usIt->second == "consume") {
					ActionProposal p;
					p.type = ActionProposal::UseItem;
					p.actorId = m_server->localPlayerId();
					p.slotIndex = slot;
					// Pass effect_amount in damage field so server uses correct heal value
					auto eit = art->fields.find("effect_amount");
					p.damage = (eit != art->fields.end()) ? std::stof(eit->second) : 4.0f;
					m_server->sendAction(p);
					m_audio.play("item_consume", pe->position, 0.7f);
				}
			}
		}
	}

	// Camera tracks entity position — same for all modes.
	// LocalServer: server tick already ran physics this frame.
	// NetworkServer: tick() interpolated ALL entities toward server
	//   positions. Player, animals, villagers — all same code path.
	m_camera.player.feetPos = pe->position;
	// Player model yaw: FPS = look direction, all others = server-set entity yaw (smooth)
	if (m_camera.mode != CameraMode::FirstPerson) {
		float diff = pe->yaw - m_camera.player.yaw;
		while (diff > 180.0f) diff -= 360.0f;
		while (diff < -180.0f) diff += 360.0f;
		m_camera.player.yaw += diff * std::min(dt * 10.0f, 1.0f);
	}
	m_worldTime = m_server->worldTime();
	m_renderer.setTimeOfDay(m_worldTime);
	m_renderer.tick(dt);

	// Update audio listener + background music
	m_audio.setListener(m_camera.position, m_camera.front());
	m_audio.updateMusic();

	// Client-initiated item pickup: scan nearby items, send PickupItem action.
	// Skip scan briefly after dropping an item to prevent instant re-pickup.
	if (m_dropCooldown <= 0)
	{
		float pickupRange = pe->def().pickup_range;
		auto& srv = *m_server;
		EntityId playerId = srv.localPlayerId();
		srv.forEachEntity([&](Entity& e) {
			if (e.typeId() != EntityType::ItemEntity) return;
			if (e.removed) return;
			if (m_pendingPickups.count(e.id())) return;
			float dist = glm::length(e.position - pe->position);
			if (dist < pickupRange) {
				ActionProposal p;
				p.type = ActionProposal::PickupItem;
				p.actorId = playerId;
				p.targetEntity = e.id();
				srv.sendAction(p);
				m_pendingPickups.insert(e.id());

				// Start fly-toward-player animation (optimistic, client-side)
				std::string itemType = e.getProp<std::string>(Prop::ItemType);
				const BlockDef* bdef = srv.blockRegistry().find(itemType);
				glm::vec3 color = bdef ? bdef->color_top : glm::vec3(0.8f, 0.5f, 0.2f);
				// Model key for rendering during fly animation
				std::string mk = itemType;
				auto mkColon = mk.find(':');
				if (mkColon != std::string::npos) mk = mk.substr(mkColon + 1);
				// Display name
				std::string name = mk;
				if (!name.empty()) name[0] = (char)toupper((unsigned char)name[0]);
				for (auto& c : name) if (c == '_') c = ' ';
				int count = e.getProp<int>(Prop::Count, 1);
				m_pickupAnims.push_back({e.id(), e.position, color, name, mk, count, 0, 0.35f});
			}
		});
		// Clean stale pending entries
		for (auto it = m_pendingPickups.begin(); it != m_pendingPickups.end(); ) {
			Entity* check = srv.getEntity(*it);
			if (!check || check->removed) it = m_pendingPickups.erase(it);
			else ++it;
		}
	}

	// Update pickup animations — animate item toward player, then fire effects
	for (auto it = m_pickupAnims.begin(); it != m_pickupAnims.end(); ) {
		it->t += dt / it->duration;
		if (it->t >= 1.0f) {
			// Arrived at player — puff + sound, accumulate for text
			m_particles.emitItemPickup(pe->position + glm::vec3(0, 0.8f, 0), it->color);
			m_audio.play("item_pickup", pe->position, 0.5f);

			it = m_pickupAnims.erase(it);
		} else {
			++it;
		}
	}


	// Creature ambient sounds: very rare, within 5 blocks only, whisper-quiet.
	m_creatureSoundTimer -= dt;
	if (m_creatureSoundTimer <= 0) {
		m_creatureSoundTimer = 30.0f + (float)(rand() % 200) / 10.0f; // every 30-50s

		struct SoundCandidate { std::string group; glm::vec3 pos; float dist; };
		std::vector<SoundCandidate> candidates;
		m_server->forEachEntity([&](Entity& e) {
			float dist = glm::length(e.position - pe->position);
			if (dist > 5.0f || dist < 0.3f) return;
			const auto& tid = e.typeId();
			if (tid == "base:pig")          candidates.push_back({"creature_pig", e.position, dist});
			else if (tid == "base:chicken") candidates.push_back({"creature_chicken", e.position, dist});
			else if (tid == "base:dog")     candidates.push_back({"creature_dog", e.position, dist});
			else if (tid == "base:cat")     candidates.push_back({"creature_cat", e.position, dist});
		});
		if (!candidates.empty()) {
			auto& c = candidates[rand() % candidates.size()];
			// Volume fades with distance: 0.04 at 0 blocks, ~0 at 5 blocks
			float vol = 0.04f * (1.0f - c.dist / 5.0f);
			m_audio.play(c.group, c.pos, vol);
		}
	}

	// Door toggle sound
	if (m_gameplay.doorToggled()) {
		m_audio.play("door_open", m_gameplay.doorTogglePos(), 0.6f);
		m_gameplay.clearDoorToggle();
	}

	// Block place feedback (immediate client-side sound)
	auto& placeEvt = m_gameplay.placeEvent();
	if (placeEvt.happened) {
		std::string snd = "place_stone";
		const std::string& bt = placeEvt.blockType;
		if (bt.find("wood") != std::string::npos || bt.find("log") != std::string::npos)
			snd = "place_wood";
		else if (bt.find("dirt") != std::string::npos || bt.find("sand") != std::string::npos)
			snd = "place_soft";
		m_audio.play(snd, placeEvt.pos, 0.5f);
	}

	// Per-hit mining feedback (particles + sound on each survival swing)
	auto& hitEvt = m_gameplay.hitEvent();
	if (hitEvt.happened) {
		m_particles.emitBlockBreak(hitEvt.pos, hitEvt.color, 5);
		// Play dig sound based on color
		float r = hitEvt.color.r, g = hitEvt.color.g, b = hitEvt.color.b;
		if (r > 0.7f && g > 0.7f && b > 0.7f)
			m_audio.play("dig_snow", hitEvt.pos, 0.4f);
		else if (r < 0.5f && g < 0.5f && b < 0.5f)
			m_audio.play("dig_stone", hitEvt.pos, 0.5f);
		else if (g > r && g > b)
			m_audio.play("dig_leaves", hitEvt.pos, 0.3f);
		else
			m_audio.play("dig_dirt", hitEvt.pos, 0.4f);
	}

	// Check if player right-clicked an entity → enter inspection
	if (m_gameplay.inspectedEntity() != ENTITY_NONE) {
		m_preInspectState = m_state;
		m_state = GameState::ENTITY_INSPECT;
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		m_camera.resetMouseTracking(); // prevent jump when returning to gameplay
	}

	renderPlaying(dt, aspect);
}

void Game::renderPlaying(float dt, float aspect, bool skipImGui) {
	if (!m_server) { printf("[Game] renderPlaying: no server\n"); return; }
	auto& srv = *m_server;
	Entity* pe = playerEntity();
	if (!pe) { printf("[Game] renderPlaying: no player entity\n"); return; }

	// Update chunks
	m_renderer.updateChunks(srv.chunks(), m_camera, m_renderDistance);

	// Render terrain + sky + crosshair
	auto& hit = m_gameplay.currentHit();
	glm::ivec3 hlPos;
	glm::ivec3* hlPtr = nullptr;
	if (hit) {
		hlPos = hit->blockPos;
		hlPtr = &hlPos;
	}
	int selectedSlot = pe->getProp<int>(Prop::SelectedSlot, 0);

	// Crosshair position depends on camera mode:
	// - FPS: center of screen
	// - ThirdPerson/RPG: project aim point in front of player to screen space
	// - RTS: no crosshair (mouse cursor visible)
	glm::vec2 crosshairOffset = {0, 0};
	bool showCrosshair = true;

	if (m_camera.mode != CameraMode::FirstPerson) {
		// TPS/RPG/RTS: no crosshair — targeted block is shown via highlight wireframe instead
		showCrosshair = false;
	}

	m_renderer.render(m_camera, aspect, hlPtr, selectedSlot, 7, crosshairOffset, showCrosshair);

	// Fog of war — render fog at unloaded chunk boundaries
	m_renderer.renderFogOfWar(m_camera, aspect, m_server->chunks(), m_renderDistance);

	// Move target highlight (RPG/RTS click-to-move destination)
	if (m_gameplay.hasMoveTarget()) {
		glm::ivec3 targetBlock = glm::ivec3(glm::floor(m_gameplay.moveTarget() - glm::vec3(0, 1, 0)));
		m_renderer.renderMoveTarget(m_camera, aspect, targetBlock);
	}

	// Block break progress overlay (survival multi-hit)
	if (m_gameplay.isBreaking()) {
		m_renderer.renderBreakProgress(m_camera, aspect,
			m_gameplay.breakTarget(), m_gameplay.breakProgress());
	}

	// 3D models
	glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
	auto& mr = m_renderer.modelRenderer();

	// Player walk animation (from entity velocity + walk distance)
	m_globalTime += dt;
	float playerSpeed = glm::length(glm::vec2(pe->velocity.x, pe->velocity.z));
	float prevWalkDist = m_playerWalkDist;
	m_playerWalkDist += playerSpeed * dt;
	float playerAttackPhase = m_fpSwingActive ? (m_fpSwingTimer / m_fpSwingDuration) : 0.0f;
	AnimState playerAnim = {m_playerWalkDist, playerSpeed, m_globalTime, playerAttackPhase};

	// Footstep sounds — play every ~2.5 blocks of movement
	if (pe->onGround && playerSpeed > 0.5f) {
		float stepInterval = 2.5f;
		if ((int)(m_playerWalkDist / stepInterval) != (int)(prevWalkDist / stepInterval)) {
			// Determine step sound from block under feet
			glm::ivec3 feetBlock = glm::ivec3(glm::floor(pe->position)) - glm::ivec3(0, 1, 0);
			BlockId underFeet = srv.chunks().getBlock(feetBlock.x, feetBlock.y, feetBlock.z);
			const auto& bdef = srv.blockRegistry().get(underFeet);
			std::string stepSound = bdef.sound_footstep;
			if (stepSound.empty()) stepSound = "step_dirt";
			m_audio.play(stepSound, pe->position, 0.35f);
		}
	}

	// Resolve model key: character_skin prop overrides EntityDef.model
	auto resolveModelKey = [](const Entity& e) -> std::string {
		std::string skin = e.getProp<std::string>("character_skin", "");
		if (!skin.empty()) {
			auto colon = skin.find(':');
			return (colon != std::string::npos) ? skin.substr(colon + 1) : skin;
		}
		std::string key = e.def().model;
		auto dot = key.rfind('.');
		if (dot != std::string::npos) key = key.substr(0, dot);
		return key;
	};

	// Build a model with equipped items attached at hand positions.
	// Items swing with the arm they're attached to.
	auto buildEquippedModel = [&](const Entity& e, const BoxModel& baseModel) -> BoxModel {
		BoxModel model = baseModel;
		if (!e.inventory) return model;
		const float PI = 3.14159265f;
		float s = model.modelScale;

		// Hand attachment points come from the character model (hand_r/l, pivot_r/l).
		// This lets each creature skin define correct proportions in Python.
		struct HandSlot {
			WearSlot slot;
			glm::vec3 handOffset;   // position of the hand (where item center goes)
			glm::vec3 pivot;        // arm rotation pivot
			float phase;            // arm swing phase
		};
		float armAmp = 50.0f; // match arm amplitude
		// Only the offhand (left) uses an equipment slot.
		// Right hand always shows the hotbar-selected item (handled below).
		HandSlot hands[] = {
			{WearSlot::Offhand, model.handL, model.pivotL, PI},
		};

		for (auto& h : hands) {
			const std::string& itemId = e.inventory->equipped(h.slot);
			if (itemId.empty()) continue;

			// Look up item model from artifact's model field
			std::string modelKey;
			const ArtifactEntry* art = m_artifacts.findById(itemId);
			if (art) {
				auto it = art->fields.find("model");
				if (it != art->fields.end()) modelKey = it->second;
			}
			// Fallback: strip "base:" prefix
			if (modelKey.empty()) {
				modelKey = itemId;
				auto colon = modelKey.find(':');
				if (colon != std::string::npos) modelKey = modelKey.substr(colon + 1);
			}

			auto mit = m_models.find(modelKey);
			if (mit == m_models.end()) continue;

			// Apply equip transform: rotate + offset + scale item parts
			auto& et = mit->second.equip;
			// If no explicit equip scale was set (default 1.0) and model is large,
			// auto-scale to fit in hand (~0.3 blocks)
			float es = et.scale;
			bool hasExplicitEquip = (et.rotation != glm::vec3(0) || et.offset != glm::vec3(0) || et.scale != 1.0f);
			if (!hasExplicitEquip) {
				float modelH = std::max(mit->second.totalHeight * mit->second.modelScale, 0.1f);
				es = std::min(0.35f / modelH, 0.5f); // fit to ~0.35 blocks, cap at 0.5
			}
			// Pre-compute rotation matrix from equip Euler angles (degrees)
			float rx = glm::radians(et.rotation.x);
			float ry = glm::radians(et.rotation.y);
			float rz = glm::radians(et.rotation.z);
			glm::mat3 rotX = {
				{1, 0, 0}, {0, std::cos(rx), std::sin(rx)}, {0, -std::sin(rx), std::cos(rx)}
			};
			glm::mat3 rotY = {
				{std::cos(ry), 0, -std::sin(ry)}, {0, 1, 0}, {std::sin(ry), 0, std::cos(ry)}
			};
			glm::mat3 rotZ = {
				{std::cos(rz), std::sin(rz), 0}, {-std::sin(rz), std::cos(rz), 0}, {0, 0, 1}
			};
			glm::mat3 equipRot = rotZ * rotY * rotX;

			for (auto& part : mit->second.parts) {
				BodyPart bp;
				// Rotate part offset around origin, then scale and translate to hand
				glm::vec3 rotatedOffset = equipRot * (part.offset * es);
				bp.offset = h.handOffset + et.offset + rotatedOffset;
				// Rotate halfSize axes (approximate: use rotated extents)
				glm::vec3 hs = part.halfSize * es;
				bp.halfSize = glm::abs(equipRot * hs);
				bp.color = part.color;
				bp.pivot = h.pivot;
				bp.swingAxis = {1, 0, 0};
				bp.swingAmplitude = armAmp;
				bp.swingPhase = h.phase;
				bp.swingSpeed = 1.0f;
				model.parts.push_back(bp);
			}
		}

		// Hotbar selected item → right hand (Minecraft-style: hotbar IS right hand)
		{
			int sel = e.getProp<int>(Prop::SelectedSlot, 0);
			std::string hotbarId = e.inventory->hotbar(sel);
			if (!hotbarId.empty()) {
				std::string modelKey;
				const ArtifactEntry* art = m_artifacts.findById(hotbarId);
				if (art) {
					auto it = art->fields.find("model");
					if (it != art->fields.end()) modelKey = it->second;
				}
				if (modelKey.empty()) {
					modelKey = hotbarId;
					auto colon = modelKey.find(':');
					if (colon != std::string::npos) modelKey = modelKey.substr(colon + 1);
				}
				auto mit = m_models.find(modelKey);
				if (mit != m_models.end()) {
					glm::vec3 rHandOff = model.handR;
					glm::vec3 rPivot = model.pivotR;

					auto& et = mit->second.equip;
					float es = et.scale;
					float rx = glm::radians(et.rotation.x);
					float ry = glm::radians(et.rotation.y);
					float rz = glm::radians(et.rotation.z);
					glm::mat3 rotXm = {{1,0,0},{0,std::cos(rx),std::sin(rx)},{0,-std::sin(rx),std::cos(rx)}};
					glm::mat3 rotYm = {{std::cos(ry),0,-std::sin(ry)},{0,1,0},{std::sin(ry),0,std::cos(ry)}};
					glm::mat3 rotZm = {{std::cos(rz),std::sin(rz),0},{-std::sin(rz),std::cos(rz),0},{0,0,1}};
					glm::mat3 equipRot = rotZm * rotYm * rotXm;

					for (auto& part : mit->second.parts) {
						BodyPart bp;
						bp.offset = rHandOff + et.offset + equipRot * (part.offset * es);
						bp.halfSize = glm::abs(equipRot * (part.halfSize * es));
						bp.color = part.color;
						bp.pivot = rPivot;
						bp.swingAxis = {1, 0, 0};
						bp.swingAmplitude = armAmp;
						bp.swingPhase = 0;
						bp.swingSpeed = 1.0f;
						model.parts.push_back(bp);
					}
				}
			}
		}

		// Back slot (jetpack, cape, etc.)
		{
			const std::string& backItem = e.inventory->equipped(WearSlot::Back);
			if (!backItem.empty()) {
				std::string modelKey;
				const ArtifactEntry* art = m_artifacts.findById(backItem);
				if (art) {
					auto it = art->fields.find("model");
					if (it != art->fields.end()) modelKey = it->second;
				}
				if (modelKey.empty()) {
					modelKey = backItem;
					auto colon = modelKey.find(':');
					if (colon != std::string::npos) modelKey = modelKey.substr(colon + 1);
				}
				auto mit = m_models.find(modelKey);
				if (mit != m_models.end()) {
					for (auto& part : mit->second.parts) {
						BodyPart bp;
						bp.offset = glm::vec3(0, 1.05f, 0.20f) + part.offset * 0.8f;
						bp.halfSize = part.halfSize * 0.8f;
						bp.color = part.color;
						model.parts.push_back(bp);
					}
				}
			}
		}

		// Head slot (helmet)
		{
			const std::string& headItem = e.inventory->equipped(WearSlot::Helmet);
			if (!headItem.empty()) {
				std::string modelKey;
				const ArtifactEntry* art = m_artifacts.findById(headItem);
				if (art) {
					auto it = art->fields.find("model");
					if (it != art->fields.end()) modelKey = it->second;
				}
				if (modelKey.empty()) {
					modelKey = headItem;
					auto colon = modelKey.find(':');
					if (colon != std::string::npos) modelKey = modelKey.substr(colon + 1);
				}
				auto mit = m_models.find(modelKey);
				if (mit != m_models.end()) {
					for (auto& part : mit->second.parts) {
						BodyPart bp;
						bp.offset = glm::vec3(0, 2.02f, 0) + part.offset * 0.7f;
						bp.halfSize = part.halfSize * 0.7f;
						bp.color = part.color;
						// Head bob: same as head part
						bp.pivot = {0, 1.5f, 0};
						bp.swingAxis = {1, 0, 0};
						bp.swingAmplitude = 5.0f;
						bp.swingPhase = 0;
						bp.swingSpeed = 2.0f;
						model.parts.push_back(bp);
					}
				}
			}
		}

		return model;
	};

	// Draw local player — skip in first-person (camera at eyes)
	if (m_camera.mode != CameraMode::FirstPerson) {
		auto pit = m_models.find(resolveModelKey(*pe));
		if (pit != m_models.end()) {
			BoxModel equipped = buildEquippedModel(*pe, pit->second);
			mr.draw(equipped, vp, m_camera.smoothedFeetPos(), m_camera.player.yaw, playerAnim);
		}
	}

	// Mob models — all entities except the locally-possessed one (drawn above)
	srv.forEachEntity([&](Entity& e) {
		if (e.id() == m_server->localPlayerId()) return; // drawn above with camera position

		float mobSpeed = glm::length(glm::vec2(e.velocity.x, e.velocity.z));
		float mobDist = e.getProp<float>(Prop::WalkDistance, 0.0f);
		AnimState mobAnim = {mobDist, mobSpeed, m_globalTime};

		std::string modelKey = resolveModelKey(e);
		auto mit = m_models.find(modelKey);
		if (mit != m_models.end()) {
			BoxModel mobModel = (e.inventory && e.def().isLiving())
				? buildEquippedModel(e, mit->second) : mit->second;

			// Damage flash: entity flashes red for a short time after being hit
			float flashT = 0.0f;
			auto flit = m_damageFlash.find(e.id());
			if (flit != m_damageFlash.end()) flashT = flit->second;
			float tintStr = std::max(0.0f, flashT / 0.25f); // 0.25s full duration
			// Attack phase: inject into AnimState for arm/limb lunge animation
			float atkPhase = 0.0f;
			auto apit = m_entityAttackPhase.find(e.id());
			if (apit != m_entityAttackPhase.end()) atkPhase = apit->second;
			mobAnim.attackPhase = atkPhase;
			mr.draw(mobModel, vp, e.position, e.yaw, mobAnim, tintStr);
		} else if (!modelKey.empty() && e.typeId() != EntityType::ItemEntity) {
			// Warn once per model key
			static std::unordered_set<std::string> warned;
			if (warned.insert(modelKey).second)
				printf("[Render] WARNING: no model for key '%s' (entity %s)\n",
					modelKey.c_str(), e.typeId().c_str());
		} else if (e.typeId() == EntityType::ItemEntity) {
			// If pickup is pending, don't render the server entity at all.
			// The pickup animation (below) renders the flying version.
			if (m_pendingPickups.count(e.id())) return;

			// Floating in place — bob + bounce + spin + XZ scatter (client-side)
			unsigned int h = e.id() * 2654435761u; // hash for deterministic scatter
			float bob = std::sin(m_globalTime * 2.5f + e.id() * 1.7f) * 0.06f;
			float bounce = std::abs(std::sin(m_globalTime * 4.0f + e.id() * 2.3f)) * 0.04f;
			float bobY = bob + bounce;
			float spinYaw = m_globalTime * 90.0f + e.id() * 47.0f;
			// Scatter: small XZ offset so stacked items don't overlap
			float ox = ((h & 0xFF) / 255.0f - 0.5f) * 0.3f;
			float oz = (((h >> 8) & 0xFF) / 255.0f - 0.5f) * 0.3f;
			std::string itemType = e.getProp<std::string>(Prop::ItemType);
			// Look up the actual 3D model for this item type
			std::string modelKey = itemType;
			auto colon = modelKey.find(':');
			if (colon != std::string::npos) modelKey = modelKey.substr(colon + 1);
			BoxModel itemModel;
			auto mit = m_models.find(modelKey);
			if (mit != m_models.end()) {
				// Use the real model, height-normalized so all items are ~0.35 blocks tall
				itemModel = mit->second;
				float targetH = 0.35f;
				float modelH = std::max(itemModel.totalHeight * itemModel.modelScale, 0.1f);
				float worldScale = targetH / modelH;
				for (auto& part : itemModel.parts) {
					part.offset *= worldScale;
					part.halfSize *= worldScale;
				}
			} else {
				// Fallback: colored cube
				const BlockDef* idef = srv.blockRegistry().find(itemType);
				glm::vec3 itemColor = idef ? idef->color_top : glm::vec3(0.8f, 0.5f, 0.2f);
				itemModel.parts.push_back({{0, 0.15f, 0}, {0.12f, 0.12f, 0.12f},
					{itemColor.r, itemColor.g, itemColor.b, 1.0f}});
			}
			mr.draw(itemModel, vp, e.position + glm::vec3(ox, bobY + 0.3f, oz), spinYaw, {});
		}
	});

	// Render pickup animations (items flying toward player)
	for (auto& pa : m_pickupAnims) {
		float ease = pa.t * pa.t * (3.0f - 2.0f * pa.t);
		glm::vec3 target = pe->position + glm::vec3(0, 0.8f, 0);
		glm::vec3 drawPos = glm::mix(pa.startPos, target, ease);
		float scale = 1.0f - ease * 0.5f;
		float spinYaw = m_globalTime * 720.0f;
		// Use actual 3D model for the flying item
		BoxModel flyModel;
		auto fmit = m_models.find(pa.modelKey);
		if (fmit != m_models.end()) {
			flyModel = fmit->second;
			float modelH = std::max(flyModel.totalHeight * flyModel.modelScale, 0.1f);
			float flyScale = (0.35f / modelH) * scale;
			for (auto& part : flyModel.parts) {
				part.offset *= flyScale;
				part.halfSize *= flyScale;
			}
		} else {
			float hs = 0.12f * scale;
			flyModel.parts.push_back({{0, 0.15f, 0}, {hs, hs, hs},
				{pa.color.r, pa.color.g, pa.color.b, 1.0f}});
		}
		mr.draw(flyModel, vp, drawPos, spinYaw, {});
	}

	// Selection circles under selected entities (RTS mode)
	{
		auto& sel = m_gameplay.selectedEntities();
		for (EntityId eid : sel) {
			Entity* se = srv.getEntity(eid);
			if (!se) continue;
			glm::ivec3 selBlock = glm::ivec3(glm::floor(se->position));
			m_renderer.renderMoveTarget(m_camera, aspect, selBlock);
		}
	}

	// Lightbulbs above living entities (behavior indicator)
	// Lightbulb icon (UI indicator above AI entities, not game content)
	static BoxModel lightbulb = []() {
		BoxModel m; m.totalHeight = 0.4f;
		m.parts.push_back({{0,0.15f,0},{0.08f,0.10f,0.08f},{1.0f,0.92f,0.3f,0.9f}});
		m.parts.push_back({{0,0.27f,0},{0.05f,0.04f,0.05f},{1.0f,1.0f,0.7f,0.95f}});
		m.parts.push_back({{0,0.04f,0},{0.06f,0.05f,0.06f},{0.5f,0.5f,0.5f,0.9f}});
		return m;
	}();
	srv.forEachEntity([&](Entity& e) {
		if (e.id() == m_server->localPlayerId()) return;
		if (!e.def().isLiving()) return; // only living entities

		float entityTop = e.def().collision_box_max.y;
		float bobY = std::sin(m_globalTime * 2.0f + e.id() * 0.7f) * 0.05f;
		glm::vec3 bulbPos = e.position + glm::vec3(0, entityTop + 0.3f + bobY, 0);

		// Red tint if behavior has error
		BoxModel bulb = lightbulb;
		if (e.hasError) {
			for (auto& p : bulb.parts)
				p.color = {1.0f, 0.2f, 0.2f, 0.9f};
		}

		mr.draw(bulb, vp, bulbPos, m_camera.lookYaw, {}); // billboard: face camera

		// Goal bubble: floating text + log when goal changes
		if (!e.goalText.empty()) {
			static std::unordered_map<EntityId, std::string> lastGoals;
			auto& prev = lastGoals[e.id()];
			if (prev != e.goalText) {
				prev = e.goalText;
				// Log the decision
				std::string eName = e.def().display_name.empty() ? e.typeId() : e.def().display_name;
				appendLog(eName + ": " + e.goalText);
			}
		}

		// HP tracking: detect damage/death for flash, log, sound, puff
		{
			int curHP = e.getProp<int>(Prop::HP, e.def().max_hp);
			auto hpIt = m_prevEntityHP.find(e.id());
			if (hpIt != m_prevEntityHP.end() && curHP < hpIt->second) {
				int dmg = hpIt->second - curHP;
				bool dying = (curHP <= 0);
				std::string eName = e.def().display_name.empty() ? e.typeId() : e.def().display_name;
				appendLog(dying ? eName + " died" : eName + " took " + std::to_string(dmg) + " damage");

				// Red flash: set/reset timer on any damage
				m_damageFlash[e.id()] = 0.25f;

				// Floating damage number
				{
					bool isPlayer = (e.id() == m_server->localPlayerId());
					FloatTextEvent ft;
					if (isPlayer) {
						ft.type  = FloatTextType::DamageTaken;
						ft.color = {1.0f, 0.30f, 0.20f, 1.0f};
						ft.text  = "-" + std::to_string(dmg) + " HP";
					} else {
						ft.type     = FloatTextType::DamageDealt;
						ft.targetId = e.id();
						ft.worldPos = e.position + glm::vec3(0.0f, entityTop * 0.5f, 0.0f);
						ft.color = dying ? glm::vec4(1.0f, 0.20f, 0.10f, 1.0f)
						                 : glm::vec4(1.0f, 0.85f, 0.10f, 1.0f);
						ft.text  = "-" + std::to_string(dmg);
					}
					m_floatText.add(ft);
				}

				// Hitmarker crosshair feedback
				m_renderer.triggerHitmarker(dying);

				// Impact sound: punch for fist/generic, sword slice for larger hits
				if (dying) {
					m_audio.play("hit_punch", e.position, 1.0f);
					// Death puff: particle burst at entity center using its body color
					glm::vec3 bodyColor = {0.7f, 0.55f, 0.35f};
					auto fmit = m_models.find(resolveModelKey(e));
					if (fmit != m_models.end() && !fmit->second.parts.empty())
						bodyColor = glm::vec3(fmit->second.parts[0].color);
					m_particles.emitDeathPuff(e.position, bodyColor, entityTop);
				} else {
					m_audio.play(dmg >= 4 ? "hit_sword" : "hit_punch", e.position, 0.6f);
				}
			}
			m_prevEntityHP[e.id()] = curHP;
		}
		// Decay damage flash timer
		{
			auto flit = m_damageFlash.find(e.id());
			if (flit != m_damageFlash.end()) {
				flit->second -= dt;
				if (flit->second <= 0) m_damageFlash.erase(flit);
			}
		}
	});

	// Remove HP / flash entries for entities no longer in the world
	{
		std::unordered_set<EntityId> seen;
		m_server->forEachEntity([&](Entity& e) { if (e.def().isLiving()) seen.insert(e.id()); });
		for (auto it = m_prevEntityHP.begin(); it != m_prevEntityHP.end(); )
			it = seen.count(it->first) ? std::next(it) : m_prevEntityHP.erase(it);
		for (auto it = m_damageFlash.begin(); it != m_damageFlash.end(); )
			it = seen.count(it->first) ? std::next(it) : m_damageFlash.erase(it);
		for (auto it = m_entityAttackPhase.begin(); it != m_entityAttackPhase.end(); )
			it = seen.count(it->first) ? std::next(it) : m_entityAttackPhase.erase(it);
	}

	// Particles
	m_particles.render(vp);

	// ── First-person held item (Minecraft-style, bottom-right) ──
	if (m_camera.mode == CameraMode::FirstPerson && pe->inventory) {
		int slot = pe->getProp<int>(Prop::SelectedSlot, 0);
		std::string heldId = pe->inventory->hotbar(slot);
		if (!heldId.empty() && pe->inventory->hotbarCount(slot) > 0) {
			std::string fpKey = heldId;
			auto fpColon = fpKey.find(':');
			if (fpColon != std::string::npos) fpKey = fpKey.substr(fpColon + 1);
			auto fpMit = m_models.find(fpKey);
			if (fpMit != m_models.end()) {
				// Clear depth so held item renders on top of world
				glClear(GL_DEPTH_BUFFER_BIT);

				glm::mat4 fpProj = glm::perspective(glm::radians(70.0f), aspect, 0.01f, 10.0f);
				glm::mat4 fpView = glm::lookAt(
					glm::vec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
				glm::mat4 fpVP = fpProj * fpView;

				// Position: bottom-right of view (Minecraft-style)
				// Walk bob: gentle up-down + side sway when moving on ground
				float bobAmt = pe->onGround
					? glm::length(glm::vec2(pe->velocity.x, pe->velocity.z)) : 0.0f;
				float bobScale = std::min(bobAmt / 4.0f, 1.0f);
				float bobY = std::sin(m_playerWalkDist * 6.0f) * 0.018f * bobScale;
				float bobX = std::sin(m_playerWalkDist * 3.0f) * 0.010f * bobScale;
				glm::vec3 itemPos(0.55f + bobX, -0.38f + bobY, -0.70f);

				glm::mat4 fpRoot = glm::translate(glm::mat4(1.0f), itemPos);

				// Apply equip rotation from model
				auto& eqt = fpMit->second.equip;
				fpRoot = glm::rotate(fpRoot, glm::radians(eqt.rotation.y), glm::vec3(0, 1, 0));
				fpRoot = glm::rotate(fpRoot, glm::radians(eqt.rotation.x), glm::vec3(1, 0, 0));
				fpRoot = glm::rotate(fpRoot, glm::radians(eqt.rotation.z), glm::vec3(0, 0, 1));

				// Swing animation on left-click.
				// Axis: blend of pitch (X) and forward-roll (Z) for a diagonal slash
				// that sweeps the blade edge through the air rather than pure up/down.
				if (m_fpSwingActive) {
					float t = m_fpSwingTimer / m_fpSwingDuration;
					float swing = std::sin(t * 3.14159f) * (-65.0f);
					glm::vec3 slashAxis = glm::normalize(glm::vec3(0.8f, 0.0f, 0.5f));
					fpRoot = glm::rotate(fpRoot, glm::radians(swing), slashAxis);
				}

				// Auto-scale items without explicit equip transform
				float fpEs = eqt.scale;
				bool fpHasEquip = (eqt.rotation != glm::vec3(0) || eqt.offset != glm::vec3(0) || eqt.scale != 1.0f);
				if (!fpHasEquip) {
					float mh = std::max(fpMit->second.totalHeight * fpMit->second.modelScale, 0.1f);
					fpEs = std::min(0.35f / mh, 0.5f);
				}
				// 0.72 keeps items occupying ~40-45% of FOV height at z=-0.70 with 70° FOV
				float fpScale = fpEs * 0.72f;
				fpRoot = glm::scale(fpRoot, glm::vec3(fpScale));

				mr.drawStatic(fpMit->second, fpVP, fpRoot);
			}
		}
	}

	// HUD — read all player state from entity
	int playerHP = pe->getProp<int>(Prop::HP, pe->def().max_hp);
	float playerHunger = pe->getProp<float>(Prop::Hunger, 20.0f);
	Inventory emptyInv;
	HUDContext ctx{
		aspect, m_state, selectedSlot,
		pe->inventory ? *pe->inventory : emptyInv,
		m_camera, srv.blockRegistry(), &srv.chunks(),
		m_worldTime, m_currentFPS, m_showDebug, m_equipUI.isOpen(),
		hit, m_gameplay.currentEntityHit(),
		m_renderer.sunStrength(),
		srv.entityCount(), m_particles.count(),
		playerHP, pe->def().max_hp, playerHunger
	};
	m_hud.render(ctx, m_text, m_renderer.highlightShader());


	// ImGui overlays (equipment, FPS) — skip when another ImGui overlay is active
	if (skipImGui) return;
	m_ui.beginFrame();
	m_iconCache.setTime(m_globalTime);

	// ── ImGui Hotbar (Roboto font + rotating 3D block preview) ──
	{
		ImDrawList* dl = ImGui::GetForegroundDrawList();
		float ww = (float)m_window.width(), wh = (float)m_window.height();
		int slots = Inventory::HOTBAR_SLOTS;
		float slotPx = 60.0f;          // slot size in pixels
		float gapPx = 4.0f;
		float totalW = slots * (slotPx + gapPx) - gapPx;
		float startX = (ww - totalW) * 0.5f;
		float startY = wh - slotPx - 12.0f;

		// Backdrop panel
		float pad = 8.0f;
		dl->AddRectFilled(
			{startX - pad, startY - pad},
			{startX + totalW + pad, startY + slotPx + pad},
			IM_COL32(16, 14, 10, 185), 8.0f);
		dl->AddRect(
			{startX - pad, startY - pad},
			{startX + totalW + pad, startY + slotPx + pad},
			IM_COL32(90, 72, 45, 170), 8.0f, 0, 1.5f);

		Inventory emptyInv;
		const Inventory& inv = pe->inventory ? *pe->inventory : emptyInv;
		auto& blocks = srv.blockRegistry();

		for (int i = 0; i < slots; i++) {
			float sx = startX + i * (slotPx + gapPx);
			float sy = startY;
			bool selected = (i == selectedSlot);

			// Slot bg
			ImU32 slotBg = selected ? IM_COL32(72, 58, 30, 230) : IM_COL32(28, 24, 18, 210);
			dl->AddRectFilled({sx, sy}, {sx + slotPx, sy + slotPx}, slotBg, 4.0f);

			// Selection glow
			if (selected) {
				dl->AddRect({sx - 2, sy - 2}, {sx + slotPx + 2, sy + slotPx + 2},
					IM_COL32(225, 175, 50, 230), 5.0f, 0, 2.5f);
			} else {
				dl->AddRect({sx, sy}, {sx + slotPx, sy + slotPx},
					IM_COL32(65, 52, 36, 140), 4.0f, 0, 1.0f);
			}

			// Item content: 3D model icon
			std::string itemId = inv.hotbar(i);
			int itemCount = inv.hotbarCount(i);
			if (!itemId.empty() && itemCount > 0) {
				// Look up model key (strip "base:" prefix)
				std::string modelKey = itemId;
				auto colon = modelKey.find(':');
				if (colon != std::string::npos) modelKey = modelKey.substr(colon + 1);

				// Try to get a cached 3D icon; fall back to isometric cube
				auto mit = m_models.find(modelKey);
				GLuint icon = (mit != m_models.end())
					? m_iconCache.getIcon(modelKey, mit->second) : 0;

				float pad = 4.0f;
				if (icon) {
					// Draw the 3D model icon texture (flipped UV for OpenGL)
					dl->AddImage((ImTextureID)(intptr_t)icon,
						{sx + pad, sy + pad}, {sx + slotPx - pad, sy + slotPx - pad},
						{0, 1}, {1, 0}); // flip Y
				} else {
					// Fallback: colored cube for blocks without a model file
					const BlockDef* bdef = blocks.find(itemId);
					glm::vec3 c = bdef ? bdef->color_top : glm::vec3(0.5f, 0.6f, 0.75f);
					float cx = sx + slotPx * 0.5f;
					float cy = sy + slotPx * 0.40f;
					float sz = slotPx * 0.34f;
					float angle = m_globalTime * 0.8f + i * 0.5f;
					float ca = std::cos(angle), sa = std::sin(angle);
					ImVec2 proj[8];
					float corners[8][3] = {
						{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1},
						{-1, 1,-1},{1, 1,-1},{1, 1,1},{-1, 1,1},
					};
					for (int v = 0; v < 8; v++) {
						float rx = corners[v][0]*ca - corners[v][2]*sa;
						float rz = corners[v][0]*sa + corners[v][2]*ca;
						float ry = corners[v][1];
						proj[v] = {cx + (rx - rz) * sz * 0.5f,
						           cy - (rx + rz) * sz * 0.25f - ry * sz * 0.5f};
					}
					auto drawFace = [&](int a, int b, int c2, int d, float shade) {
						ImVec2 pts[] = {proj[a], proj[b], proj[c2], proj[d]};
						ImU32 col = IM_COL32(
							(int)(c.r*shade*255), (int)(c.g*shade*255), (int)(c.b*shade*255), 230);
						dl->AddConvexPolyFilled(pts, 4, col);
						dl->AddPolyline(pts, 4, IM_COL32(0,0,0,80), true, 1.0f);
					};
					drawFace(7, 6, 5, 4, 1.0f);
					float nx_r = ca + sa, nx_f = -sa + ca;
					if (nx_r > 0) drawFace(1, 2, 6, 5, 0.72f);
					else          drawFace(3, 0, 4, 7, 0.72f);
					if (nx_f > 0) drawFace(2, 3, 7, 6, 0.85f);
					else          drawFace(0, 1, 5, 4, 0.85f);
				}

				// Stack count (large, Roboto, bottom-right with shadow)
				if (itemCount > 1) {
					char buf[8]; snprintf(buf, sizeof(buf), "%d", itemCount);
					ImFont* bigFont = ImGui::GetIO().Fonts->Fonts.Size > 1
						? ImGui::GetIO().Fonts->Fonts[1] : ImGui::GetFont();
					float tx = sx + slotPx - 8.0f;
					float ty = sy + slotPx - 6.0f;
					// Shadow
					dl->AddText(bigFont, 26.0f, {tx - strlen(buf)*13.0f + 1.5f, ty - 22.0f},
						IM_COL32(0,0,0,200), buf);
					// Main text
					dl->AddText(bigFont, 26.0f, {tx - strlen(buf)*13.0f, ty - 23.0f},
						IM_COL32(255,255,255,240), buf);
				}
			}

			// Key label (top-left, small)
			char key[4]; snprintf(key, sizeof(key), "%d", (i + 1) % 10);
			dl->AddText(ImGui::GetFont(), 14.0f, {sx + 4, sy + 2},
				IM_COL32(140, 130, 110, 130), key);
		}
	}

	// Equipment/Inventory UI ([I] to toggle)
	if (pe->inventory) {
		m_equipUI.setModels(&m_models, &m_iconCache);
		m_equipUI.render(*pe->inventory, m_server->blockRegistry(),
			(float)m_window.width(), (float)m_window.height());
	}

	// FPS counter
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.5f);
	if (ImGui::Begin("##fps", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
		ImGui::Text("FPS: %.0f", m_currentFPS);
	}
	ImGui::End();

	// RTS selection box overlay
	if (m_camera.mode == CameraMode::RTS && m_gameplay.isBoxDragging()) {
		auto s = m_gameplay.boxStart();
		auto e = m_gameplay.boxEnd();
		// Convert NDC to screen pixels
		float sw = m_window.width(), sh = m_window.height();
		float x0 = (std::min(s.x, e.x) + 1) * 0.5f * sw;
		float y0 = (1 - std::max(s.y, e.y)) * 0.5f * sh;
		float x1 = (std::max(s.x, e.x) + 1) * 0.5f * sw;
		float y1 = (1 - std::min(s.y, e.y)) * 0.5f * sh;

		ImDrawList* dl = ImGui::GetForegroundDrawList();
		dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1),
			IM_COL32(100, 200, 255, 200), 0, 0, 2.0f);
		dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
			IM_COL32(100, 200, 255, 40));
	}

	// RTS selected entity count
	if (m_camera.mode == CameraMode::RTS && !m_gameplay.selectedEntities().empty()) {
		ImGui::SetNextWindowPos(ImVec2(10, 40), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.6f);
		if (ImGui::Begin("##selection", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
			ImGui::Text("Selected: %zu units", m_gameplay.selectedEntities().size());
			ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1), "Right-click to move");
		}
		ImGui::End();
	}

	// ── Hotbar drag/drop interaction layer ──
	// Transparent ImGui window over the visual hotbar so ImGui can handle drag/drop.
	if (pe->inventory) {
		float ww = (float)m_window.width(), wh = (float)m_window.height();
		int hslots = Inventory::HOTBAR_SLOTS;
		float slotPx = 60.0f, gapPx = 4.0f, hpad = 8.0f;
		float totalW = hslots * (slotPx + gapPx) - gapPx;
		float startX = (ww - totalW) * 0.5f;
		float startY = wh - slotPx - 12.0f;

		ImGui::SetNextWindowPos({startX - hpad, startY - hpad});
		ImGui::SetNextWindowSize({totalW + 2*hpad, slotPx + 2*hpad});
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {hpad, hpad});
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {gapPx, 0.0f});
		if (ImGui::Begin("##hotbar_dd", nullptr,
			ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoBackground| ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {

			struct DragSlot { char itemId[64]; int slot; };
			Inventory& inv = *pe->inventory;

			for (int i = 0; i < hslots; i++) {
				if (i > 0) ImGui::SameLine(0.0f, gapPx);
				char bid[24]; snprintf(bid, sizeof(bid), "##hdd%d", i);
				ImGui::InvisibleButton(bid, {slotPx, slotPx});

				// Drag source — pick up item from this hotbar slot
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
					DragSlot ds{}; ds.slot = i;
					snprintf(ds.itemId, sizeof(ds.itemId), "%s", inv.hotbar(i).c_str());
					ImGui::SetDragDropPayload("INV_SLOT", &ds, sizeof(ds));
					if (ds.itemId[0]) ImGui::Text("%s", ds.itemId); else ImGui::Text("(empty)");
					ImGui::EndDragDropSource();
				}

				// Drop target — receive item into slot i
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("INV_SLOT")) {
						auto* ds = (const DragSlot*)pl->Data;
						std::string newId = ds->itemId;
						std::string oldId = inv.hotbar(i);
						if (ds->slot >= 0) {
							// Hotbar ↔ hotbar swap
							inv.setHotbar(ds->slot, oldId);
							m_server->sendHotbarSlot(ds->slot, oldId);
						}
						inv.setHotbar(i, newId);
						m_server->sendHotbarSlot(i, newId);
					}
					ImGui::EndDragDropTarget();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	// (Backpack panel removed — use [I] Equipment UI for all inventory management)

	// Floating text notifications (damage, pickups, heals)
	if (!skipImGui) {
		m_floatText.render(m_camera, aspect, m_camera.mode, m_text,
		                   m_gameplay.selectedEntities());
	}

	m_ui.endFrame();

	// Screenshot request: check for trigger file (external diagnostic tool)
	// Create /tmp/agentica_screenshot_request to trigger a screenshot.
	{
		static float screenshotCheckTimer = 0;
		screenshotCheckTimer += dt;
		if (screenshotCheckTimer > 0.5f) { // check every 0.5s
			screenshotCheckTimer = 0;
			if (std::filesystem::exists("/tmp/agentica_screenshot_request")) {
				std::filesystem::remove("/tmp/agentica_screenshot_request");
				saveScreenshot();
			}
		}
	}
}

// ============================================================
// Entity inspection overlay
// ============================================================
void Game::updateEntityInspect(float dt, float aspect) {
	if (!m_server) { m_state = GameState::MENU; return; }
	Entity* pe = playerEntity();
	if (!pe) { m_state = GameState::MENU; return; }

	// Keep server running
	m_server->tick(dt);
	if (!m_server->isConnected()) { m_state = GameState::MENU; return; }

	// Render 3D world in background (skip ImGui overlays — we draw our own)
	m_globalTime += dt;
	m_worldTime += m_daySpeed * dt;
	m_renderer.setTimeOfDay(m_worldTime);
	m_renderer.tick(dt);
	renderPlaying(dt, aspect, true);

	// Get inspected entity
	EntityId eid = m_gameplay.inspectedEntity();
	Entity* target = m_server->getEntity(eid);
	if (!target) { m_gameplay.clearInspection(); return; }

	// ── ImGui overlay panel ──────────────────────────────────
	m_ui.beginFrame();

	float ww = (float)m_window.width(), wh = (float)m_window.height();
	float panW = std::min(520.0f, ww * 0.85f);
	float panH = std::min(680.0f, wh * 0.88f);
	ImGui::SetNextWindowPos({(ww - panW) * 0.5f, (wh - panH) * 0.5f}, ImGuiCond_Always);
	ImGui::SetNextWindowSize({panW, panH}, ImGuiCond_Always);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 0.94f));
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.20f, 0.35f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14, 10});

	char title[128];
	snprintf(title, sizeof(title), "%s  (%s)###EntityInspect",
		target->def().display_name.c_str(), target->typeId().c_str());

	bool open = true;
	if (ImGui::Begin(title, &open,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {

		// ── Live stats ──
		int hp = target->getProp<int>(Prop::HP, target->def().max_hp);
		int maxHp = target->def().max_hp;
		ImGui::TextColored({0.4f, 1.0f, 0.4f, 1}, "HP: %d / %d", hp, maxHp);
		ImGui::SameLine(200);
		ImGui::TextColored({0.7f, 0.7f, 0.7f, 1}, "Pos: %.1f, %.1f, %.1f",
			target->position.x, target->position.y, target->position.z);

		if (!target->goalText.empty()) {
			ImGui::TextColored({0.5f, 1.0f, 0.8f, 1}, "Goal: %s", target->goalText.c_str());
		}
		if (target->hasError) {
			ImGui::TextColored({1.0f, 0.3f, 0.3f, 1}, "ERROR: %s", target->errorText.c_str());
		}

		ImGui::Separator();

		// ── Properties ──
		if (ImGui::CollapsingHeader("Properties")) {
			auto& def = target->def();
			if (ImGui::BeginTable("Props", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
				ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130);
				ImGui::TableSetupColumn("Value");
				auto row = [](const char* label, const char* fmt, auto... args) {
					ImGui::TableNextRow(); ImGui::TableNextColumn();
					ImGui::TextColored({0.5f, 0.52f, 0.56f, 1}, "%s", label);
					ImGui::TableNextColumn();
					char buf[64]; snprintf(buf, sizeof(buf), fmt, args...);
					ImGui::Text("%s", buf);
				};
				row("Walk Speed", "%.1f", def.walk_speed);
				row("Run Speed", "%.1f", def.run_speed);
				row("Max HP", "%d", def.max_hp);
				std::string bid = target->getProp<std::string>(Prop::BehaviorId, "");
				if (!bid.empty()) { row("Behavior", "%s", bid.c_str()); }
				ImGui::EndTable();
			}
		}

		// ── Behavior Tree Editor ──
		if (ImGui::CollapsingHeader("Behavior Tree", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.15f, 1));
			ImGui::BeginChild("##behaviorTree", ImVec2(0, 200), true);

			int idCounter = 0;
			BehaviorExprEditor::render(m_inspectEditor.sharedBehavior, 0, idCounter);

			ImGui::EndChild();
			ImGui::PopStyleColor();

			// Python preview (compiled from tree)
			if (ImGui::TreeNode("Python Preview")) {
				std::string code = BehaviorCompiler::compile(m_inspectEditor.sharedBehavior);
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.08f, 1));
				ImGui::BeginChild("##pyPreview", ImVec2(0, 150), true);
				std::istringstream stream(code);
				std::string line;
				int lineNum = 1;
				while (std::getline(stream, line)) {
					ImGui::TextColored({0.45f, 0.50f, 0.45f, 1}, "%3d ", lineNum++);
					ImGui::SameLine();
					// Simple syntax coloring
					if (line.find("def ") != std::string::npos || line.find("if ") != std::string::npos ||
					    line.find("else") != std::string::npos || line.find("for ") != std::string::npos)
						ImGui::TextColored({0.4f, 0.6f, 1.0f, 1}, "%s", line.c_str());
					else if (line.find("return ") != std::string::npos || line.find("import ") != std::string::npos)
						ImGui::TextColored({0.7f, 0.4f, 0.9f, 1}, "%s", line.c_str());
					else if (line.empty() || line[0] == '#')
						ImGui::TextColored({0.4f, 0.55f, 0.4f, 1}, "%s", line.c_str());
					else
						ImGui::TextColored({0.35f, 0.75f, 0.35f, 1}, "%s", line.c_str());
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::TreePop();
			}

			// ── Apply buttons with scope ──
			ImGui::Spacing();
			std::string typeName = target->def().display_name;

			char applyOneLabel[64], applyAllLabel[64];
			snprintf(applyOneLabel, sizeof(applyOneLabel), "Apply to This %s", typeName.c_str());
			snprintf(applyAllLabel, sizeof(applyAllLabel), "Apply to All %ss", typeName.c_str());

			if (ImGui::Button(applyOneLabel)) {
				std::string code = BehaviorCompiler::compile(m_inspectEditor.sharedBehavior);
				char filename[64];
				snprintf(filename, sizeof(filename), "entity_%u_behavior", eid);
				m_behaviorStore.save(filename, code);
				ActionProposal reload;
				reload.type = ActionProposal::ReloadBehavior;
				reload.actorId = eid;
				reload.blockType = code;
				m_server->sendAction(reload);
				printf("[Inspect] Applied behavior to entity %u only\n", eid);
			}
			ImGui::SameLine();
			if (ImGui::Button(applyAllLabel)) {
				std::string code = BehaviorCompiler::compile(m_inspectEditor.sharedBehavior);
				// Hash the code for a unique behavior name
				uint32_t hash = 0;
				for (char c : code) hash = hash * 31 + (uint8_t)c;
				char behaviorName[32];
				snprintf(behaviorName, sizeof(behaviorName), "custom_%06x", hash & 0xFFFFFF);
				m_behaviorStore.save(behaviorName, code);
				// Reload ALL entities of this type
				m_server->forEachEntity([&](Entity& e) {
					if (e.typeId() == target->typeId()) {
						ActionProposal reload;
						reload.type = ActionProposal::ReloadBehavior;
						reload.actorId = e.id();
						reload.blockType = code;
						m_server->sendAction(reload);
					}
				});
				printf("[Inspect] Applied behavior '%s' to all %s entities\n",
					behaviorName, target->typeId().c_str());
			}
		}

		// ── Current behavior source (read-only reference) ──
		auto behaviorInfo = m_server->getBehaviorInfo(eid);
		if (!behaviorInfo.sourceCode.empty()) {
			if (ImGui::CollapsingHeader("Current Behavior Source")) {
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.08f, 1));
				ImGui::BeginChild("##curSrc", ImVec2(0, 120), true);
				std::istringstream stream(behaviorInfo.sourceCode);
				std::string line;
				int lineNum = 1;
				while (std::getline(stream, line)) {
					ImGui::TextColored({0.45f, 0.50f, 0.45f, 1}, "%3d ", lineNum++);
					ImGui::SameLine();
					ImGui::TextColored({0.35f, 0.65f, 0.35f, 1}, "%s", line.c_str());
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar();

	m_ui.endFrame();

	// ESC or X button closes
	if (!open || m_controls.pressed(Action::MenuBack)) {
		m_gameplay.clearInspection();
		m_state = m_preInspectState;
		bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
		                    m_camera.mode == CameraMode::ThirdPerson);
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
			needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		m_camera.resetMouseTracking();
	}
}

// ============================================================
// Code editor overlay
// ============================================================
void Game::updateCodeEditor(float dt, float aspect) {
	if (!m_server) { m_state = GameState::MENU; return; }

	// Keep server running (other clients / AI behaviors shouldn't freeze)
	m_server->tick(dt);
	if (!m_server->isConnected()) { m_state = GameState::MENU; return; }

	m_globalTime += dt;

	// Render 3D world in background (skip ImGui — code editor has its own)
	m_worldTime += m_daySpeed * dt;
	m_renderer.setTimeOfDay(m_worldTime);
	m_renderer.tick(dt);
	renderPlaying(dt, aspect, true);

	// Render code editor on top
	m_codeEditor.render(m_text, aspect, m_globalTime);

	// Check editor actions
	if (m_codeEditor.wantsCancel()) {
		m_codeEditor.close();
		m_state = GameState::ENTITY_INSPECT;
		m_codeEditor.clearFlags();
	}

	if (m_codeEditor.wantsApply()) {
		std::string newCode = m_codeEditor.getCode();
		EntityId eid = m_codeEditor.editingEntity();

		// Save to artifacts/ (persists across restarts)
		char filename[64];
		snprintf(filename, sizeof(filename), "entity_%u_behavior", eid);
		m_behaviorStore.save(filename, newCode);

		// Send behavior reload request to server → forwarded to bot client
		// Uses a special action type that the server intercepts
		ActionProposal reload;
		reload.type = ActionProposal::ReloadBehavior;
		reload.actorId = eid;
		reload.blockType = newCode; // reuse blockType field for source code
		m_server->sendAction(reload);
		printf("[CodeEditor] Behavior reload sent for entity %u\n", eid);

		m_codeEditor.clearError();
		m_codeEditor.close();
		m_state = GameState::ENTITY_INSPECT;
		m_codeEditor.clearFlags();
	}

	if (m_codeEditor.wantsReset()) {
		EntityId eid = m_codeEditor.editingEntity();
		auto info = m_server->getBehaviorInfo(eid);
		m_codeEditor.open(eid, info.sourceCode, info.goal);
		m_codeEditor.clearFlags();
	}
}

// ============================================================
// Pause menu overlay (Esc during gameplay)
// ============================================================
void Game::updatePaused(float dt, float aspect) {
	if (!m_server) { m_state = GameState::MENU; return; }

	// Game keeps running (DST/Minecraft multiplayer style — no pause)
	m_server->tick(dt);
	if (!m_server->isConnected()) { m_state = GameState::MENU; return; }

	// Render the world behind the overlay — but NOT the ImGui frame
	// from renderPlaying (we need our own single ImGui frame for the
	// pause overlay buttons to actually receive input).
	{
		auto& srv = *m_server;
		Entity* pe = playerEntity();
		if (!pe) { m_state = GameState::MENU; return; }
		m_globalTime += dt;
		m_worldTime = m_server->worldTime();
		m_renderer.setTimeOfDay(m_worldTime);
		m_renderer.tick(dt);
		m_renderer.updateChunks(srv.chunks(), m_camera, m_renderDistance);
		glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
		m_renderer.render(m_camera, aspect, nullptr, 0, 7, {0,0}, false);
		// Draw entities — same resolveModelKey as renderPlaying
		auto& mr = m_renderer.modelRenderer();
		auto resolveKey = [](const Entity& e) -> std::string {
			std::string skin = e.getProp<std::string>("character_skin", "");
			if (!skin.empty()) {
				auto colon = skin.find(':');
				return (colon != std::string::npos) ? skin.substr(colon + 1) : skin;
			}
			std::string key = e.def().model;
			auto dot = key.rfind('.'); if (dot != std::string::npos) key = key.substr(0, dot);
			return key;
		};
		srv.forEachEntity([&](Entity& e) {
			if (e.typeId() == EntityType::ItemEntity) return;
			auto it = m_models.find(resolveKey(e));
			if (it != m_models.end()) {
				float spd = glm::length(glm::vec2(e.velocity.x, e.velocity.z));
				AnimState anim = {e.getProp<float>(Prop::WalkDistance, 0.0f), spd, m_globalTime};
				mr.draw(it->second, vp, e.position, e.yaw, anim);
			}
		});
		m_particles.update(dt);
		m_particles.render(vp);

		// Restore GL state before ImGui
		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
	}

	// Single ImGui frame for the pause overlay
	m_ui.beginFrame();

	// Dim overlay
	ImDrawList* bg = ImGui::GetBackgroundDrawList();
	bg->AddRectFilled({0, 0}, {(float)m_window.width(), (float)m_window.height()},
		IM_COL32(0, 0, 0, 140));

	float pw = 360, ph = 380;
	float px = (m_window.width() - pw) * 0.5f;
	float py = (m_window.height() - ph) * 0.5f;

	ImGui::SetNextWindowPos({px, py});
	ImGui::SetNextWindowSize({pw, ph});
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.98f, 0.97f, 0.96f, 0.98f));
	ImGui::Begin("##gamemenu", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

	// Title
	ImGui::SetWindowFontScale(1.3f);
	float titleW = ImGui::CalcTextSize("Game Menu").x;
	ImGui::SetCursorPosX((pw - titleW) * 0.5f - 20);
	ImGui::TextColored({0.25f, 0.25f, 0.28f, 1.0f}, "Game Menu");
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

	float btnW = pw - 44;
	auto styledButton = [&](const char* label, ImVec4 bg, ImVec4 bgHover, ImVec4 bgActive,
	                        ImVec4 text, float height = 42.0f) {
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, bg);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgHover);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, bgActive);
		ImGui::PushStyleColor(ImGuiCol_Text, text);
		ImGui::SetCursorPosX(22);
		bool clicked = ImGui::Button(label, {btnW, height});
		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar();
		return clicked;
	};

	// Back to Game
	if (styledButton("Back to Game",
		{0.96f, 0.65f, 0.15f, 1}, {0.98f, 0.72f, 0.28f, 1}, {0.90f, 0.55f, 0.10f, 1},
		{1, 1, 1, 1}) || m_controls.pressed(Action::MenuBack)) {
		m_state = m_preMenuState;
		bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
		                    m_camera.mode == CameraMode::ThirdPerson);
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
			needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		m_camera.resetMouseTracking();
	}
	ImGui::Spacing();

	// Go to Main Menu (keeps game running, can resume)
	if (styledButton("Go to Main Menu",
		{0.40f, 0.42f, 0.48f, 1}, {0.50f, 0.52f, 0.58f, 1}, {0.35f, 0.37f, 0.42f, 1},
		{1, 1, 1, 1})) {
		m_state = GameState::MENU;
		m_imguiMenu.setGameRunning(true); // game still running, can resume
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
	ImGui::Spacing();

	// Game Log toggle
	{
		const char* logLabel = m_showGameLog ? "Hide Game Log" : "Game Log";
		if (styledButton(logLabel,
			{0.18f, 0.42f, 0.55f, 1}, {0.24f, 0.52f, 0.66f, 1}, {0.14f, 0.36f, 0.48f, 1},
			{1, 1, 1, 1})) {
			m_showGameLog = !m_showGameLog;
		}
		ImGui::Spacing();
	}

	// Quit Game
	if (styledButton("Quit Game",
		{0.65f, 0.20f, 0.20f, 1}, {0.75f, 0.28f, 0.28f, 1}, {0.55f, 0.15f, 0.15f, 1},
		{1, 1, 1, 1})) {
		m_state = GameState::MENU;
		m_imguiMenu.setGameRunning(false);
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		m_agentMgr.stopAll();
		m_server->disconnect();
		m_server.reset();
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

	// ── Game Log panel ──────────────────────────────────────────────────────
	if (m_showGameLog && !m_gameLog.empty()) {
		float lw = 580, lh = 420;
		float lx = (m_window.width()  - lw) * 0.5f + 200; // offset right of pause menu
		float ly = (m_window.height() - lh) * 0.5f;
		ImGui::SetNextWindowPos({lx, ly}, ImGuiCond_Always);
		ImGui::SetNextWindowSize({lw, lh}, ImGuiCond_Always);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.07f, 0.10f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
		ImGui::Begin("##gamelog", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);

		ImGui::SetWindowFontScale(0.95f);
		ImGui::TextColored({0.55f, 0.75f, 0.95f, 1.0f}, "Game Log");
		ImGui::SameLine(lw - 70);
		ImGui::TextColored({0.4f, 0.4f, 0.45f, 1.0f}, "%d entries", (int)m_gameLog.size());
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::BeginChild("##logscroll", {lw - 24, lh - 62}, false,
		                  ImGuiWindowFlags_HorizontalScrollbar);
		// Newest at bottom — iterate forward
		for (const auto& line : m_gameLog) {
			// Colour-code: deaths red, damage orange, AI decisions green, pickups gold
			glm::vec4 c = {0.75f, 0.78f, 0.82f, 0.9f}; // default: light grey
			if (line.find("died")   != std::string::npos) c = {1.0f, 0.35f, 0.25f, 1.0f};
			else if (line.find("damage") != std::string::npos) c = {1.0f, 0.62f, 0.20f, 1.0f};
			else if (line.find("Picked") != std::string::npos) c = {1.0f, 0.90f, 0.30f, 1.0f};
			else if (line.find(": ")  != std::string::npos) c = {0.65f, 0.88f, 0.55f, 1.0f}; // AI
			ImGui::TextColored({c.r, c.g, c.b, c.a}, "%s", line.c_str());
		}
		// Auto-scroll to bottom
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10)
			ImGui::SetScrollHereY(1.0f);
		ImGui::EndChild();

		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}

	m_ui.endFrame();
	if (m_state != GameState::PAUSED) m_showGameLog = false; // close log when leaving pause
}

} // namespace agentica
