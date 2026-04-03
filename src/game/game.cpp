#include "game/game.h"
#include "server/entity_manager.h"
#include "shared/constants.h"
#include "shared/model_loader.h"
#include "server/python_bridge.h"
#include "server/behavior.h"
#include "server/world_save.h"
#include "shared/physics.h"
#ifndef __EMSCRIPTEN__
#include "client/network_server.h"
#endif
#include "imgui.h"
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <fstream>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace agentworld {

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
	printf("=== AgentWorld v0.9.0 ===\n");

	if (!m_window.init(1600, 900, "AgentWorld")) return false;
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
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--demo") == 0) m_demoMode = true;
		else if (strcmp(argv[i], "--skip-menu") == 0) m_skipMenu = true;
		else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) m_connectHost = argv[++i];
		else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) m_connectPort = atoi(argv[++i]);
	}

	// Models — ALL loaded from Python (artifacts/models/base/ and models/player/)
	m_models = model_loader::loadAllModels("artifacts");
	printf("[Game] Loaded %zu models from Python\n", m_models.size());

	// Validate: every creature, character, and item artifact should have a model
	for (auto* cat : {"creature", "character", "item"}) {
		for (auto* entry : m_artifacts.byCategory(cat)) {
			std::string key = entry->name;
			for (auto& c : key) c = (char)std::tolower((unsigned char)c);
			if (!m_models.count(key))
				printf("[WARNING] %s '%s' has no model in artifacts/models/ (expected %s.py)\n",
					cat, entry->name.c_str(), key.c_str());
		}
	}
	m_modelPreview.init(&m_renderer.highlightShader(), 256, 256);

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

	// --skip-menu: jump directly into a new village world (survival)
	if (m_skipMenu && m_connectHost.empty()) {
		printf("[Game] --skip-menu: starting new world directly\n");
		enterGame(1, GameState::SURVIVAL);
	}

	// If --host was provided, pre-populate the server list but show menu
	// (user clicks "Join" from the menu rather than auto-connecting)
	if (!m_connectHost.empty()) {
		printf("[Game] Server hint: %s:%d (join from menu)\n",
		       m_connectHost.c_str(), m_connectPort);
		m_imguiMenu.addServerHint(m_connectHost, m_connectPort);
	}

	return true;
}

void Game::shutdown() {
	// Save world on quit
	saveCurrentWorld();

	if (m_serverLog) { fclose(m_serverLog); m_serverLog = nullptr; }
	m_audio.shutdown();
	m_ui.shutdown();
	m_hud.shutdown();
	m_particles.shutdown();
	m_text.shutdown();
	m_renderer.shutdown();
	m_window.shutdown();
}

void Game::addFloatingText(glm::vec3 pos, const std::string& text,
                           glm::vec4 color, float scale) {
	FloatingText ft;
	ft.pos = pos + glm::vec3(0, 0.6f, 0);
	ft.velY = 3.0f;
	ft.offsetX = ((rand() % 100) / 100.0f - 0.5f) * 0.4f;
	ft.text = text;
	ft.color = color;
	ft.life = ft.maxLife = 1.6f;
	ft.baseScale = scale;
	m_floatingTexts.push_back(ft);
	if (m_floatingTexts.size() > 40) m_floatingTexts.pop_front();
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
	if (f12 && !prevF12 && (m_state == GameState::ADMIN || m_state == GameState::SURVIVAL)) {
		if (m_state == GameState::ADMIN) {
			m_state = GameState::SURVIVAL;
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
		m_showInventory = !m_showInventory;
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
	snprintf(p, 256, "/tmp/agentworld_screenshot_%d.ppm", m_screenshotCounter++);
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
		m_autoScreenTimer += dt;

		// Demo mode: capture Play page → Handbook → enter game
		if (m_demoMode && m_autoScreenTimer > 0.5f && m_autoScreenTimer < 0.6f) {
			// Capture Play page (world list + create new)
			m_imguiMenu.setPage(0);
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_menu_play.ppm");
		}
		if (m_demoMode && m_autoScreenTimer > 1.0f && m_autoScreenTimer < 1.1f) {
			// Switch to handbook, show pig with 3D preview
			m_imguiMenu.setPage(1);
			m_imguiMenu.handbook().selectEntry("base:pig");
		}
		if (m_demoMode && m_autoScreenTimer > 1.8f && m_autoScreenTimer < 1.9f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_handbook_creature.ppm");
		}
		if (m_demoMode && m_autoScreenTimer > 2.2f) {
			action.type = MenuAction::EnterGame;
			action.templateIndex = 1;
			action.targetState = GameState::ADMIN;
		}

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
	case GameState::SURVIVAL:
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
		}
		break;
	case MenuAction::LoadWorld: {
		m_currentWorldPath = action.worldPath;
		printf("[Game] Loading world from %s\n", action.worldPath.c_str());
		auto localServer = std::make_unique<LocalServer>(m_templates);
		// Create server without init (loadWorld will init)
		localServer->createServerOnly();
		if (loadWorld(*localServer->server(), action.worldPath, m_templates)) {
			localServer->finishLoad(); // register client after load
			m_server = std::move(localServer);
			setupAfterConnect(GameState::SURVIVAL);
		} else {
			printf("[Game] Failed to load world, creating new\n");
			enterGame(action.templateIndex, GameState::SURVIVAL);
		}
		break;
	}
	case MenuAction::DeleteWorld:
		m_imguiMenu.worldManager().deleteWorld(action.worldPath);
		m_imguiMenu.worldManager().refresh();
		break;
	}
}

void Game::joinServer(const std::string& host, int port, GameState targetState) {
#ifndef __EMSCRIPTEN__
	printf("[Game] Joining server at %s:%d\n", host.c_str(), port);
	bool creative = (targetState == GameState::ADMIN);
	auto netServer = std::make_unique<NetworkServer>(host, port);
	netServer->setDisplayName(m_playerName);
	netServer->setCreatureType(m_selectedCreature);
	if (netServer->createGame(42, 0, creative)) {
		printf("[Game] Connected to %s:%d as %s (%s)\n",
		       host.c_str(), port, m_playerName.c_str(), m_selectedCreature.c_str());
		m_server = std::move(netServer);
		setupAfterConnect(targetState);
		return;
	}
	printf("[Game] Failed to join %s:%d\n", host.c_str(), port);
#endif
	// Stay in menu on failure — don't fallback to local (would cause infinite loop)
	m_state = GameState::MENU;
}

// ============================================================
// World creation — always creates a local server
// ============================================================
void Game::enterGame(int templateIndex, GameState targetState, const WorldGenConfig& wgc) {
	// enterGame always creates a local server. To join a remote server,
	// use joinServer() directly (from the server browser UI).
	printf("[Game] Starting local server\n");

	// Open server log file: /tmp/agentica_log_local.log
	if (m_serverLog) { fclose(m_serverLog); m_serverLog = nullptr; }
	m_serverLog = fopen("/tmp/agentica_log_local.log", "w");
	if (m_serverLog) {
		setvbuf(m_serverLog, nullptr, _IONBF, 0);
		fprintf(m_serverLog, "=== AgentWorld Local Server ===\n");
		fprintf(m_serverLog, "seed=%d template=%d creative=%d\n",
		        m_currentSeed, templateIndex, (targetState == GameState::ADMIN));
		printf("[Game] Server log: /tmp/agentica_log_local.log\n");
	}

	bool creative = (targetState == GameState::ADMIN);
	auto localServer = std::make_unique<LocalServer>(m_templates);
	localServer->setCreatureType(m_selectedCreature);
	localServer->createGame(m_currentSeed, templateIndex, creative, wgc);
	m_server = std::move(localServer);
	printf("[Game] Playing as %s (%s)\n", m_playerName.c_str(), m_selectedCreature.c_str());
	setupAfterConnect(targetState);
}

void Game::setupAfterConnect(GameState targetState) {
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
				char buf[64];
				snprintf(buf, sizeof(buf), "+%d %s", count, name.c_str());
				addFloatingText(pos, buf, {1.0f, 0.92f, 0.30f, 1.0f}, 2.2f);
				if (m_serverLog) fprintf(m_serverLog, "[pickup] %s x%d at (%.0f,%.0f,%.0f)\n",
					item.c_str(), count, pos.x, pos.y, pos.z);
			};
			cb.onBreakText = [this](glm::vec3 pos, const std::string& blockName) {
				addFloatingText(pos + glm::vec3(0, 0.5f, 0), blockName,
				                {0.85f, 0.85f, 0.85f, 0.9f}, 1.4f);
				if (m_serverLog) fprintf(m_serverLog, "[break] %s at (%.0f,%.0f,%.0f)\n",
					blockName.c_str(), pos.x, pos.y, pos.z);
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
	m_camera.player.yaw = -90;
	m_camera.lookYaw = -90;
	m_camera.lookPitch = -5;
	m_camera.resetSmoothing();
	m_camera.rtsCenter = spawn;

	ChunkSource& chunks = m_server->chunks();
	int sx = (int)spawn.x, sh = (int)spawn.y, sz = (int)spawn.z;
	chunks.ensureChunksAround(worldToChunk(sx, sh, sz), 8);
	m_renderer.meshAllPending(chunks, m_camera, m_renderDistance);

	m_worldTime = 0.30f;
	m_autoScreenDone = false;
	m_autoScreenTimer = 0;
	m_demoStep = 1;
	m_demoTimer = 0;
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
	meta.gameMode = ls->server()->isCreative() ? "admin" : "survival";
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
	if (!m_server || !m_server->isConnected()) { m_state = GameState::MENU; return; }

	// Tick server (polls for entity updates from network)
	m_server->tick(dt);

	Entity* pe = playerEntity();
	if (!pe) {
		// Player entity not received yet — wait for server to broadcast it.
		// This happens on network clients: S_WELCOME arrives before S_ENTITY.
		m_connectTimer += dt;
		if (m_connectTimer > 10.0f) {
			printf("[Game] Timeout waiting for player entity\n");
			m_state = GameState::MENU;
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

	if (m_controls.pressed(Action::MenuBack)) {
		m_preMenuState = m_state;
		m_state = GameState::PAUSED;
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		return;
	}

	// Tell gameplay if UI wants the cursor (inventory, ImGui, etc.)
	m_gameplay.setUIWantsCursor(m_showInventory || m_equipUI.isOpen() || m_ui.wantsMouse());

	// Client-side: gather input → ActionProposals (works for local AND network)
	float jumpVel = 8.3f; // standard jump velocity for all living entities
	m_gameplay.update(dt, m_state, *m_server, *pe, m_camera, m_controls,
	                  m_renderer, m_particles, m_window, jumpVel);

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

	// Update audio listener + background music
	m_audio.setListener(m_camera.position, m_camera.front());
	m_audio.updateMusic();

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
		m_preInspectState = m_state; // remember so we restore correctly
		m_state = GameState::ENTITY_INSPECT;
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}

	renderPlaying(dt, aspect);
}

void Game::renderPlaying(float dt, float aspect) {
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
	AnimState playerAnim = {m_playerWalkDist, playerSpeed, m_globalTime};

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

	// Draw local player — skip in first-person (camera at eyes)
	if (m_camera.mode != CameraMode::FirstPerson) {
		auto pit = m_models.find(resolveModelKey(*pe));
		if (pit != m_models.end())
			mr.draw(pit->second, vp, m_camera.smoothedFeetPos(), m_camera.player.yaw, playerAnim);
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
			mr.draw(mit->second, vp, e.position, e.yaw, mobAnim);
		} else if (!modelKey.empty() && e.typeId() != EntityType::ItemEntity) {
			// Warn once per model key
			static std::unordered_set<std::string> warned;
			if (warned.insert(modelKey).second)
				printf("[Render] WARNING: no model for key '%s' (entity %s)\n",
					modelKey.c_str(), e.typeId().c_str());
		} else if (e.typeId() == EntityType::ItemEntity) {
			float bobY = std::sin(e.getProp<float>(Prop::Age, 0.0f) * 3.0f) * 0.08f;
			float spinYaw = e.getProp<float>(Prop::Age, 0.0f) * 90.0f;
			std::string itemType = e.getProp<std::string>(Prop::ItemType);
			const BlockDef* idef = srv.blockRegistry().find(itemType);
			glm::vec3 itemColor = idef ? idef->color_top : glm::vec3(0.8f, 0.5f, 0.2f);
			BoxModel itemModel;
			itemModel.parts.push_back({{0, 0.15f, 0}, {0.12f, 0.12f, 0.12f},
				{itemColor.r, itemColor.g, itemColor.b, 1.0f}});
			mr.draw(itemModel, vp, e.position + glm::vec3(0, bobY, 0), spinYaw, {});
		}
	});

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
		if (e.def().max_hp <= 0) return; // only living entities

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
	});

	// Particles
	m_particles.render(vp);

	// HUD — read all player state from entity
	int playerHP = pe->getProp<int>(Prop::HP, pe->def().max_hp);
	float playerHunger = pe->getProp<float>(Prop::Hunger, 20.0f);
	Inventory emptyInv;
	HUDContext ctx{
		aspect, m_state, selectedSlot,
		pe->inventory ? *pe->inventory : emptyInv,
		m_camera, srv.blockRegistry(), &srv.chunks(),
		m_worldTime, m_currentFPS, m_showDebug, m_showInventory,
		hit, m_gameplay.currentEntityHit(),
		m_renderer.sunStrength(),
		srv.entityCount(), m_particles.count(),
		playerHP, pe->def().max_hp, playerHunger
	};
	m_hud.render(ctx, m_text, m_renderer.highlightShader());

	// Floating text (Minecraft Dungeons style — damage numbers, pickup names)
	{
		glm::mat4 ftVP = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
		for (auto it = m_floatingTexts.begin(); it != m_floatingTexts.end(); ) {
			auto& ft = *it;
			ft.life -= dt;
			ft.pos.y += ft.velY * dt;
			ft.velY *= 0.96f; // decelerate
			if (ft.life <= 0) { it = m_floatingTexts.erase(it); continue; }

			glm::vec4 clip = ftVP * glm::vec4(ft.pos, 1.0f);
			if (clip.w <= 0.01f) { ++it; continue; }
			float ndcX = clip.x / clip.w;
			float ndcY = clip.y / clip.w;

			// Minecraft Dungeons scale: pop in 1.5x, settle to 1.0x, shrink out
			float t = 1.0f - ft.life / ft.maxLife;
			float s;
			if (t < 0.12f)
				s = ft.baseScale * (1.0f + 0.6f * (1.0f - t / 0.12f)); // pop in
			else
				s = ft.baseScale * (1.0f - 0.2f * (t - 0.12f));        // gentle shrink

			// Fade out in last 35%
			float alpha = ft.life < ft.maxLife * 0.35f
				? ft.life / (ft.maxLife * 0.35f) : 1.0f;
			glm::vec4 col = ft.color;
			col.a *= alpha;

			float charW = 0.018f * s;
			float tw = ft.text.size() * charW;
			float tx = ndcX + ft.offsetX - tw * 0.5f;

			// Draw with title mode (outline + glow) for cool pop effect
			m_text.drawTitle(ft.text, tx, ndcY, s, col, aspect);
			++it;
		}
	}

	// ImGui overlays (equipment, FPS)
	m_ui.beginFrame();

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

			// Item content: rotating isometric block
			std::string itemId = inv.hotbar(i);
			int itemCount = inv.hotbarCount(i);
			if (!itemId.empty() && itemCount > 0) {
				const BlockDef* bdef = blocks.find(itemId);
				glm::vec3 c = bdef ? bdef->color_top : glm::vec3(0.5f, 0.6f, 0.75f);

				// Rotating isometric cube
				float cx = sx + slotPx * 0.5f;
				float cy = sy + slotPx * 0.40f;
				float sz = slotPx * 0.34f;
				float angle = m_globalTime * 0.8f + i * 0.5f; // slow rotation, offset per slot

				// 8 corners of unit cube
				float ca = std::cos(angle), sa = std::sin(angle);
				ImVec2 proj[8];
				float cubeCorners[8][3] = {
					{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1}, // bottom
					{-1, 1,-1},{1, 1,-1},{1, 1,1},{-1, 1,1}, // top
				};
				for (int v = 0; v < 8; v++) {
					float rx = cubeCorners[v][0]*ca - cubeCorners[v][2]*sa;
					float rz = cubeCorners[v][0]*sa + cubeCorners[v][2]*ca;
					float ry = cubeCorners[v][1];
					proj[v] = {cx + (rx - rz) * sz * 0.5f,
					           cy - (rx + rz) * sz * 0.25f - ry * sz * 0.5f};
				}

				// Determine visible side faces
				auto drawFace = [&](int a, int b, int c2, int d, float shade) {
					ImVec2 pts[] = {proj[a], proj[b], proj[c2], proj[d]};
					ImU32 col = IM_COL32(
						(int)(c.r * shade * 255), (int)(c.g * shade * 255),
						(int)(c.b * shade * 255), 230);
					dl->AddConvexPolyFilled(pts, 4, col);
					dl->AddPolyline(pts, 4, IM_COL32(0,0,0,80), true, 1.0f);
				};

				// Top face (always visible)
				drawFace(7, 6, 5, 4, 1.0f);

				// Side faces: check normals
				float nx_r = ca + sa;    // dot(right_normal, iso_camera)
				float nx_f = -sa + ca;   // dot(front_normal, iso_camera)
				if (nx_r > 0) drawFace(1, 2, 6, 5, 0.72f);  // right face
				else          drawFace(3, 0, 4, 7, 0.72f);    // left face
				if (nx_f > 0) drawFace(2, 3, 7, 6, 0.85f);   // front face
				else          drawFace(0, 1, 5, 4, 0.85f);    // back face

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

	m_ui.endFrame();

	// Auto screenshot
	m_autoScreenTimer += dt;
	if (!m_autoScreenDone && m_autoScreenTimer > 3.0f) {
		writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_auto_screenshot.ppm");
		m_autoScreenDone = true;
	}

	// Demo mode
	if (m_demoMode && m_state != GameState::MENU && m_demoStep >= 1) {
		m_demoTimer += dt;
		if (m_demoStep == 1 && m_demoTimer > 2.0f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_view_1_fps.ppm");
			m_camera.cycleMode();
			m_camera.orbitYaw = m_camera.player.yaw + 30;
			m_camera.orbitPitch = 25;
			m_demoStep = 2; m_demoTimer = 0;
		}
		if (m_demoStep == 2 && m_demoTimer > 1.5f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_view_2_3rd.ppm");
			// Open inventory for screenshot
			if (pe->inventory) {
				pe->inventory->equip(WearSlot::LeftHand, "base:sword");
				pe->inventory->equip(WearSlot::RightHand, "base:shield");
			}
			m_equipUI.toggle();
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			m_demoStep = 25; m_demoTimer = 0;
		}
		if (m_demoStep == 25 && m_demoTimer > 1.0f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_view_25_inventory.ppm");
			m_equipUI.close();
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
			m_camera.cycleMode();
			m_demoStep = 3; m_demoTimer = 0;
		}
		if (m_demoStep == 3 && m_demoTimer > 1.5f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_view_3_god.ppm");
			m_camera.cycleMode();
			m_camera.rtsCenter = pe->position;
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			m_demoStep = 4; m_demoTimer = 0;
		}
		if (m_demoStep == 4 && m_demoTimer > 1.5f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_view_4_rts.ppm");
			printf("Demo complete.\n");
			glfwSetWindowShouldClose(m_window.handle(), true);
			m_demoStep = 99;
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

	// ESC or right-click closes inspection
	if (m_controls.pressed(Action::MenuBack) || m_controls.pressed(Action::PlaceBlock)) {
		m_gameplay.clearInspection();
		m_state = m_preInspectState; // restore survival/creative
		bool needCapture = (m_camera.mode == CameraMode::FirstPerson ||
		                    m_camera.mode == CameraMode::ThirdPerson);
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR,
			needCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
		return;
	}

	// Keep server running (other clients / AI behaviors shouldn't freeze)
	m_server->tick(dt);
	if (!m_server->isConnected()) { m_state = GameState::MENU; return; }

	// Still render the world in background
	m_globalTime += dt;
	m_worldTime += m_daySpeed * dt;
	m_renderer.setTimeOfDay(m_worldTime);
	renderPlaying(dt, aspect);

	// Draw inspection panel overlay
	EntityId eid = m_gameplay.inspectedEntity();
	Entity* target = m_server->getEntity(eid);
	if (!target) {
		m_gameplay.clearInspection();
		return;
	}

	// Semi-transparent background
	float panelW = 0.6f;
	float panelH = 0.5f;
	float px = -panelW / 2;
	float py = -panelH / 2;
	m_text.drawRect(px, py, panelW, panelH, {0.05f, 0.05f, 0.1f, 0.85f});

	// Border
	m_text.drawRect(px, py + panelH - 0.002f, panelW, 0.002f, {0.4f, 0.6f, 1.0f, 0.8f});
	m_text.drawRect(px, py, panelW, 0.002f, {0.4f, 0.6f, 1.0f, 0.8f});

	float textX = px + 0.03f;
	float textY = py + panelH - 0.06f;
	float lineH = 0.045f;

	// Title
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", target->def().display_name.c_str());
	m_text.drawText(buf, textX, textY, 1.0f, {1.0f, 0.9f, 0.5f, 1.0f}, aspect);
	textY -= lineH;

	// Type ID
	snprintf(buf, sizeof(buf), "Type: %s", target->typeId().c_str());
	m_text.drawText(buf, textX, textY, 0.65f, {0.7f, 0.7f, 0.7f, 1.0f}, aspect);
	textY -= lineH;

	// HP
	int hp = target->getProp<int>(Prop::HP, target->def().max_hp);
	snprintf(buf, sizeof(buf), "HP: %d / %d", hp, target->def().max_hp);
	m_text.drawText(buf, textX, textY, 0.7f, {0.4f, 1.0f, 0.4f, 1.0f}, aspect);
	textY -= lineH;

	// Position
	snprintf(buf, sizeof(buf), "Pos: %.1f, %.1f, %.1f", target->position.x, target->position.y, target->position.z);
	m_text.drawText(buf, textX, textY, 0.65f, {0.8f, 0.8f, 0.8f, 1.0f}, aspect);
	textY -= lineH;

	// Goal
	snprintf(buf, sizeof(buf), "Goal: %s", target->goalText.empty() ? "(none)" : target->goalText.c_str());
	m_text.drawText(buf, textX, textY, 0.7f, {0.5f, 1.0f, 0.8f, 1.0f}, aspect);
	textY -= lineH;

	// Behavior error (if any)
	if (target->hasError) {
		snprintf(buf, sizeof(buf), "ERROR: %s", target->errorText.c_str());
		m_text.drawText(buf, textX, textY, 0.6f, {1.0f, 0.3f, 0.3f, 1.0f}, aspect);
		textY -= lineH;
	}

	// Behavior source preview (first 3 lines)
	auto behaviorInfo = m_server->getBehaviorInfo(eid);
	if (!behaviorInfo.sourceCode.empty()) {
		textY -= lineH * 0.5f;
		m_text.drawText("--- Behavior Code ---", textX, textY, 0.6f, {0.6f, 0.6f, 0.8f, 1.0f}, aspect);
		textY -= lineH;

		std::string src = behaviorInfo.sourceCode;
		// Show first few lines
		int lineCount = 0;
		size_t pos = 0;
		while (pos < src.size() && lineCount < 4) {
			size_t nl = src.find('\n', pos);
			if (nl == std::string::npos) nl = src.size();
			std::string line = src.substr(pos, std::min(nl - pos, (size_t)60));
			m_text.drawText(line.c_str(), textX, textY, 0.55f, {0.5f, 0.8f, 0.5f, 0.9f}, aspect);
			textY -= lineH * 0.85f;
			pos = nl + 1;
			lineCount++;
		}
		if (pos < src.size()) {
			m_text.drawText("  ...", textX, textY, 0.55f, {0.5f, 0.5f, 0.5f, 0.7f}, aspect);
		}
	}

	// Close hint
	// [E] key opens code editor
	if (glfwGetKey(m_window.handle(), GLFW_KEY_E) == GLFW_PRESS && !behaviorInfo.sourceCode.empty()) {
		m_codeEditor.open(eid, behaviorInfo.sourceCode, target->goalText);
		m_state = GameState::CODE_EDITOR;
	}

	m_text.drawText("[ESC] Close    [E] Edit Behavior", px + 0.03f, py + 0.02f,
	                0.55f, {0.6f, 0.6f, 0.6f, 0.8f}, aspect);
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

	// Render world in background
	m_worldTime += m_daySpeed * dt;
	m_renderer.setTimeOfDay(m_worldTime);
	renderPlaying(dt, aspect);

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

		// Load and execute via Python bridge
		std::string error;
		auto handle = pythonBridge().loadBehavior(newCode, error);
		if (handle >= 0) {
			// Replace the entity's behavior with the Python version
			// Note: behavior replacement only works on local server (singleplayer).
			// For network play, this would need a C_SET_BEHAVIOR protocol message.
			auto* ls = dynamic_cast<LocalServer*>(m_server.get());
			if (ls && ls->server()) {
				auto* behaviorState = ls->server()->world().entities.getBehaviorState(eid);
				if (behaviorState) {
					behaviorState->behavior = std::make_unique<PythonBehavior>(handle, newCode);
					printf("[CodeEditor] Python behavior applied to entity %u\n", eid);
				}
			} else {
				printf("[CodeEditor] Behavior replacement not supported on remote server yet\n");
			}
			m_codeEditor.clearError();
		} else {
			printf("[CodeEditor] Python error: %s\n", error.c_str());
			m_codeEditor.setError(error);
			m_codeEditor.clearFlags();
			return; // keep editor open on error
		}

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
		m_renderer.updateChunks(srv.chunks(), m_camera, m_renderDistance);
		glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
		m_renderer.render(m_camera, aspect, nullptr, 0, 7, {0,0}, false);
		// Draw entities
		auto& mr = m_renderer.modelRenderer();
		srv.forEachEntity([&](Entity& e) {
			if (e.typeId() == EntityType::ItemEntity) return;
			std::string mk = e.def().model;
			auto dot = mk.rfind('.'); if (dot != std::string::npos) mk = mk.substr(0, dot);
			auto it = m_models.find(mk);
			if (it != m_models.end()) {
				float spd = glm::length(glm::vec2(e.velocity.x, e.velocity.z));
				AnimState anim = {e.getProp<float>(Prop::WalkDistance, 0.0f), spd, m_globalTime};
				mr.draw(it->second, vp, e.position, e.yaw, anim);
			}
		});
		m_particles.update(dt);
		m_particles.render(vp);
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
	}
	ImGui::Spacing();

	// Options
	if (ImGui::CollapsingHeader("Options...")) {
		ImGui::SliderFloat("FOV", &m_camera.fov, 50.0f, 120.0f, "%.0f");
		ImGui::SliderInt("Render Distance", &m_renderDistance, 4, 16);
		if (ImGui::Checkbox("VSync", &m_vsync))
			glfwSwapInterval(m_vsync ? 1 : 0);
		ImGui::Spacing();
		float master = m_audio.masterVolume();
		if (ImGui::SliderFloat("Master Volume", &master, 0.0f, 1.0f, "%.0f%%"))
			m_audio.setMasterVolume(master);
		float music = m_audio.musicVolume();
		if (ImGui::SliderFloat("Music Volume", &music, 0.0f, 1.0f, "%.0f%%"))
			m_audio.setMusicVolume(music);
	}

	ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

	// Disconnect
	if (styledButton("Disconnect",
		{0.72f, 0.20f, 0.20f, 1}, {0.82f, 0.28f, 0.28f, 1}, {0.62f, 0.15f, 0.15f, 1},
		{1, 1, 1, 1})) {
		m_state = GameState::MENU;
		m_imguiMenu.setGameRunning(false);
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		m_server->disconnect();
		m_server.reset();
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	m_ui.endFrame();
}

} // namespace agentworld
