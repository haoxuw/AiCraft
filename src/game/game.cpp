#include "game/game.h"
#include "content/models.h"
#include "content/characters.h"
#include "content/faces.h"
#include "server/entity_manager.h"
#include "shared/constants.h"
#include "server/python_bridge.h"
#include "server/behavior.h"
#ifndef __EMSCRIPTEN__
#include "client/network_server.h"
#endif
#include "imgui.h"
#include <cstdio>
#include <cstring>
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

	m_hud.init(m_renderer.highlightShader());
	m_behaviorStore.init("artifacts/behaviors");

	// Characters + faces
	builtin::registerAllCharacters(m_characters);
	builtin::registerAllFaces(m_faces);

	// World templates
	m_templates = {
		std::make_shared<FlatWorldTemplate>(),
		std::make_shared<VillageWorldTemplate>(),
	};
	m_menu.init(m_templates, &m_characters, &m_faces);
	m_imguiMenu.init(m_templates);

	// Load all artifact definitions (Python files from artifacts/)
	m_artifacts.setPlayerNamespace(ArtifactRegistry::generatePlayerNamespace());
	m_artifacts.loadAll("artifacts");

	// Parse args
	for (int i = 1; i < argc; i++)
		if (strcmp(argv[i], "--demo") == 0) m_demoMode = true;

	// Models
	m_playerModel = builtin::playerModel();
	m_pigModel = builtin::pigModel();
	m_chickenModel = builtin::chickenModel();
	m_dogModel = builtin::dogModel();
	m_catModel = builtin::catModel();
	m_villagerModel = builtin::villagerModel();
	m_modelPreview.init(&m_renderer.highlightShader(), 256, 256);

	// Register ALL models for Handbook 3D preview
	auto& hb = m_imguiMenu.handbook();
	hb.setPreview(&m_modelPreview, &m_renderer.modelRenderer());
	hb.setRegistry(&m_artifacts);

	// Creatures
	hb.registerModel("pig", m_pigModel);
	hb.registerModel("chicken", m_chickenModel);
	hb.registerModel("dog", m_dogModel);
	hb.registerModel("cat", m_catModel);
	hb.registerModel("villager", m_villagerModel);

	// Characters — build from CharacterManager definitions
	for (int i = 0; i < m_characters.count(); i++) {
		auto& cdef = m_characters.get(i);
		// Use lowercase name for model lookup
		std::string lower = cdef.name;
		for (auto& c : lower) c = std::tolower(c);
		// Build the visual model (body only, no face)
		hb.registerModel(lower, cdef.model);
	}

	// Items — simple box models for preview
	{
		BoxModel sword;
		sword.totalHeight = 1.0f;
		sword.parts.push_back({{0, 0.35f, 0}, {0.03f, 0.30f, 0.03f}, {0.72f, 0.72f, 0.78f, 1}}); // blade
		sword.parts.push_back({{0, 0.04f, 0}, {0.02f, 0.06f, 0.04f}, {0.40f, 0.28f, 0.12f, 1}}); // handle
		sword.parts.push_back({{0, 0.08f, 0}, {0.08f, 0.015f, 0.015f}, {0.60f, 0.55f, 0.45f, 1}}); // guard
		hb.registerModel("sword", sword);
	}
	{
		BoxModel shield;
		shield.totalHeight = 0.8f;
		shield.parts.push_back({{0, 0.30f, 0}, {0.02f, 0.22f, 0.18f}, {0.45f, 0.30f, 0.15f, 1}}); // face
		shield.parts.push_back({{0.02f, 0.30f, 0}, {0.02f, 0.08f, 0.08f}, {0.55f, 0.50f, 0.40f, 1}}); // boss
		hb.registerModel("shield", shield);
	}
	{
		BoxModel potion;
		potion.totalHeight = 0.5f;
		potion.parts.push_back({{0, 0.12f, 0}, {0.06f, 0.10f, 0.06f}, {0.80f, 0.20f, 0.30f, 1}}); // bottle
		potion.parts.push_back({{0, 0.24f, 0}, {0.03f, 0.04f, 0.03f}, {0.70f, 0.15f, 0.20f, 1}}); // neck
		potion.parts.push_back({{0, 0.29f, 0}, {0.04f, 0.02f, 0.04f}, {0.50f, 0.45f, 0.35f, 1}}); // cork
		hb.registerModel("potion", potion);
	}
	{
		BoxModel bucket;
		bucket.totalHeight = 0.5f;
		bucket.parts.push_back({{0, 0.10f, 0}, {0.08f, 0.10f, 0.08f}, {0.60f, 0.60f, 0.62f, 1}}); // body
		bucket.parts.push_back({{0, 0.10f, 0}, {0.09f, 0.02f, 0.09f}, {0.55f, 0.55f, 0.58f, 1}}); // rim
		bucket.parts.push_back({{0, 0.22f, 0}, {0.06f, 0.01f, 0.01f}, {0.50f, 0.50f, 0.52f, 1}}); // handle
		hb.registerModel("bucket", bucket);
	}
	{
		BoxModel torch;
		torch.totalHeight = 0.6f;
		torch.parts.push_back({{0, 0.12f, 0}, {0.03f, 0.14f, 0.03f}, {0.40f, 0.28f, 0.12f, 1}}); // stick
		torch.parts.push_back({{0, 0.28f, 0}, {0.04f, 0.04f, 0.04f}, {1.00f, 0.80f, 0.20f, 1}}); // flame
		torch.parts.push_back({{0, 0.34f, 0}, {0.02f, 0.03f, 0.02f}, {1.00f, 0.90f, 0.40f, 0.8f}}); // tip
		hb.registerModel("torch", torch);
	}

	// Blocks — simple single cube
	{
		BoxModel block;
		block.totalHeight = 1.0f;
		block.parts.push_back({{0, 0.5f, 0}, {0.4f, 0.4f, 0.4f}, {0.48f, 0.48f, 0.50f, 1}});
		hb.registerModel("terrain", block); // generic block
		hb.registerModel("stone", block);
	}
	{
		BoxModel dirt;
		dirt.totalHeight = 1.0f;
		dirt.parts.push_back({{0, 0.5f, 0}, {0.4f, 0.4f, 0.4f}, {0.45f, 0.32f, 0.18f, 1}});
		hb.registerModel("dirt", dirt);
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
	return true;
}

void Game::shutdown() {
	m_ui.shutdown();
	m_hud.shutdown();
	m_particles.shutdown();
	m_text.shutdown();
	m_renderer.shutdown();
	m_window.shutdown();
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

		// Demo mode: capture menu → handbook → enter game
		if (m_demoMode && m_autoScreenTimer > 0.8f && m_autoScreenTimer < 0.9f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_menu_screenshot.ppm");
			// Switch to handbook, show creature with 3D preview
			m_imguiMenu.setPage(1);
			m_imguiMenu.handbook().selectEntry("base:pig");
		}
		if (m_demoMode && m_autoScreenTimer > 1.8f && m_autoScreenTimer < 1.9f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_handbook_creature.ppm");
			// Now show a character
			m_imguiMenu.handbook().selectEntry("base:knight");
		}
		if (m_demoMode && m_autoScreenTimer > 2.6f && m_autoScreenTimer < 2.7f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/agentworld_handbook_character.ppm");
		}
		if (m_demoMode && m_autoScreenTimer > 2.0f) {
			action.type = MenuAction::EnterGame;
			action.templateIndex = 1;
			action.targetState = GameState::ADMIN;
		}

		handleMenuAction(action);
		break;
	}
	case GameState::SERVER_BROWSER: {
		auto action = m_menu.updateServerBrowser(dt, m_window, m_text, m_controls, aspect);
		handleMenuAction(action);
		break;
	}
	case GameState::TEMPLATE_SELECT: {
		auto action = m_menu.updateTemplateSelect(dt, m_window, m_text, m_controls, aspect);
		handleMenuAction(action);
		break;
	}
	case GameState::CONTROLS: {
		auto action = m_menu.updateControls(dt, m_window, m_text, m_controls, aspect);
		handleMenuAction(action);
		break;
	}
	case GameState::CHARACTER: {
		auto action = m_menu.updateCharacterSelect(dt, m_window, m_text, m_controls,
		                                           m_renderer.modelRenderer(), aspect, m_globalTime);
		m_globalTime += dt;
		handleMenuAction(action);
		break;
	}
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
	}
}

void Game::handleMenuAction(const MenuAction& action) {
	switch (action.type) {
	case MenuAction::None:
		break;
	case MenuAction::Quit:
		glfwSetWindowShouldClose(m_window.handle(), true);
		break;
	case MenuAction::StartGame:
		if (m_demoMode) {
			// Demo mode: skip server browser, go straight to game
			enterGame(0, GameState::ADMIN);
		} else {
			m_state = GameState::SERVER_BROWSER;
		}
		break;
	// ShowTemplateSelect removed -- server browser handles this now
	case MenuAction::ShowControls:
		m_state = GameState::CONTROLS;
		break;
	case MenuAction::ShowCharacter:
		m_state = GameState::CHARACTER;
		break;
	case MenuAction::BackToMenu:
		m_state = GameState::MENU;
		m_menu.resetCooldown(0.5f);
		break;
	case MenuAction::EnterGame:
		enterGame(action.templateIndex, action.targetState);
		break;
	case MenuAction::JoinServer:
		joinServer(action.serverHost, action.serverPort, action.targetState);
		break;
	}
}

void Game::joinServer(const std::string& host, int port, GameState targetState) {
#ifndef __EMSCRIPTEN__
	printf("[Game] Joining server at %s:%d\n", host.c_str(), port);
	bool creative = (targetState == GameState::ADMIN);
	auto netServer = std::make_unique<NetworkServer>(host, port);
	if (netServer->createGame(42, 0, creative)) {
		printf("[Game] Connected as %s\n", netServer->clientUUID().c_str());
		m_server = std::move(netServer);
		setupAfterConnect(targetState);
		return;
	}
	printf("[Game] Failed to join server\n");
#endif
	// Fallback: start local
	enterGame(0, targetState);
}

// ============================================================
// World creation — player is now an Entity, same as pigs
// ============================================================
void Game::enterGame(int templateIndex, GameState targetState) {
	printf("[Game] Starting local server\n");
	bool creative = (targetState == GameState::ADMIN);
	auto localServer = std::make_unique<LocalServer>(m_templates);
	localServer->createGame(42, templateIndex, creative);
	m_server = std::move(localServer);
	setupAfterConnect(targetState);
}

void Game::setupAfterConnect(GameState targetState) {
	// Set callbacks for visual effects
	m_server->setEffectCallbacks(
		[this](ChunkPos cp) { m_renderer.markChunkDirty(cp); },
		[this](glm::vec3 pos, glm::vec3 color, int count) { m_particles.emitBlockBreak(pos, color, count); },
		[this](glm::vec3 pos, glm::vec3 color) { m_particles.emitItemPickup(pos, color); }
	);

	m_state = targetState;
	glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	m_camera.mode = CameraMode::FirstPerson;

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
	m_renderer.meshAllPending(chunks, m_camera, 8);

	m_worldTime = 0.30f;
	m_autoScreenDone = false;
	m_autoScreenTimer = 0;
	m_demoStep = 1;
	m_demoTimer = 0;
	m_playerWalkDist = 0;
	m_globalTime = 0;
}

// ============================================================
// Playing state
// ============================================================
void Game::updatePlaying(float dt, float aspect) {
	if (!m_server || !m_server->isConnected()) { m_state = GameState::MENU; return; }
	// Get World reference — for local server, this is direct access.
	// For network server, gameplay will use ServerInterface methods instead.
	auto* localSrv = dynamic_cast<LocalServer*>(m_server.get());
	if (!localSrv) { m_state = GameState::MENU; return; }
	World& world = localSrv->server()->world();
	Entity* pe = playerEntity();
	if (!pe) { m_state = GameState::MENU; return; }

	if (m_controls.pressed(Action::MenuBack)) {
		m_state = GameState::MENU;
		m_menu.resetCooldown(0.5f);
		return;
	}

	// Client-side: gather input → ActionProposals
	float jumpVel = (m_characters.count() > 0) ? m_characters.selected().jumpVelocity : 17.0f;
	m_gameplay.update(dt, m_state, world, *pe, m_camera, m_controls,
	                  m_renderer, m_particles, m_window, jumpVel);

	// Server tick: resolve actions → physics → active blocks → item pickup
	m_server->tick(dt);

	// Sync server state to client
	m_camera.player.feetPos = pe->position;
	m_worldTime = m_server->worldTime();
	m_renderer.setTimeOfDay(m_worldTime);

	// Check if player right-clicked an entity → enter inspection
	if (m_gameplay.inspectedEntity() != ENTITY_NONE) {
		m_preInspectState = m_state; // remember so we restore correctly
		m_state = GameState::ENTITY_INSPECT;
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}

	renderPlaying(dt, aspect);
}

void Game::renderPlaying(float dt, float aspect) {
	auto& srv = *m_server; // ServerInterface — works for both local and network
	Entity* pe = playerEntity();
	if (!pe) return;

	// Update chunks
	m_renderer.updateChunks(srv.chunks(), m_camera, 8);

	// Render terrain + sky + crosshair
	auto& hit = m_gameplay.currentHit();
	glm::ivec3 hlPos;
	glm::ivec3* hlPtr = nullptr;
	if (hit && m_camera.mode != CameraMode::RTS) {
		hlPos = hit->blockPos;
		hlPtr = &hlPos;
	}
	int selectedSlot = pe->getProp<int>(Prop::SelectedSlot, 0);
	m_renderer.render(m_camera, aspect, hlPtr, selectedSlot);

	// 3D models
	glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
	auto& mr = m_renderer.modelRenderer();

	// Player walk animation (from entity velocity + walk distance)
	m_globalTime += dt;
	float playerSpeed = glm::length(glm::vec2(pe->velocity.x, pe->velocity.z));
	m_playerWalkDist += playerSpeed * dt;
	AnimState playerAnim = {m_playerWalkDist, playerSpeed, m_globalTime};

	if (m_camera.mode != CameraMode::FirstPerson) {
		BoxModel activeModel;
		if (m_characters.count() > 0 && m_faces.count() > 0) {
			activeModel = m_characters.buildSelectedModel(
				m_faces.selected(), pe->inventory.get());
		} else {
			activeModel = m_playerModel;
		}

		// Add equipped weapon/shield to the character model
		if (pe->inventory) {
			const float PI = 3.14159265f;
			if (pe->inventory->hasEquipped(WearSlot::LeftHand)) {
				// Sword blade (swings with left arm)
				activeModel.parts.push_back({
					{-0.42f, 0.65f, -0.15f}, {0.03f, 0.22f, 0.03f},
					{0.72f, 0.72f, 0.78f, 1},
					{-0.37f, 1.40f, 0}, {1,0,0}, 55.0f, PI, 1.0f
				});
				// Handle
				activeModel.parts.push_back({
					{-0.42f, 0.42f, -0.15f}, {0.02f, 0.06f, 0.04f},
					{0.40f, 0.28f, 0.12f, 1},
					{-0.37f, 1.40f, 0}, {1,0,0}, 55.0f, PI, 1.0f
				});
				// Crossguard
				activeModel.parts.push_back({
					{-0.42f, 0.46f, -0.15f}, {0.06f, 0.015f, 0.015f},
					{0.60f, 0.55f, 0.45f, 1},
					{-0.37f, 1.40f, 0}, {1,0,0}, 55.0f, PI, 1.0f
				});
			}
			if (pe->inventory->hasEquipped(WearSlot::RightHand)) {
				// Shield face (swings with right arm)
				activeModel.parts.push_back({
					{0.46f, 0.90f, -0.10f}, {0.02f, 0.18f, 0.14f},
					{0.45f, 0.30f, 0.15f, 1},
					{0.37f, 1.40f, 0}, {1,0,0}, 55.0f, 0, 1.0f
				});
				// Shield boss
				activeModel.parts.push_back({
					{0.48f, 0.90f, -0.10f}, {0.02f, 0.06f, 0.06f},
					{0.55f, 0.50f, 0.40f, 1},
					{0.37f, 1.40f, 0}, {1,0,0}, 55.0f, 0, 1.0f
				});
			}
		}

		mr.draw(activeModel, vp, m_camera.smoothedFeetPos(), m_camera.player.yaw, playerAnim);
	}

	// Data-driven item particle effects (defined in Python, mirrored in C++ builtins).
	// The client reads entity props (set by server) to know WHEN to emit.
	// The emitter definitions (from ItemVisual.effects) define WHAT to emit.
	if (m_camera.mode != CameraMode::FirstPerson && pe->inventory) {
		auto activeEmitters = m_characters.getActiveEffects(
			m_characters.selectedIndex(), *pe->inventory,
			[&](const std::string& trigger) { return pe->getProp<int>(trigger, 0) > 0; });

		glm::vec3 feetPos = m_camera.smoothedFeetPos();
		float yawRad = glm::radians(-m_camera.player.yaw - 90.0f);
		float cy = std::cos(yawRad), sy = std::sin(yawRad);

		static unsigned int effectSeed = 0;
		for (auto& ae : activeEmitters) {
			glm::vec3 lo = ae.slot.offset + ae.emitter.offset;
			glm::vec3 worldOff = {lo.x*cy - lo.z*sy, lo.y, lo.x*sy + lo.z*cy};
			glm::vec3 emitPos = feetPos + worldOff;

			int numColors = (int)ae.emitter.colors.size();
			for (int i = 0; i < ae.emitter.rate; i++) {
				effectSeed++;
				float r1 = ((effectSeed * 73856093u) & 0xFFFF) / 65535.0f;
				float r2 = ((effectSeed * 19349663u) & 0xFFFF) / 65535.0f;
				float r3 = ((effectSeed * 83492791u) & 0xFFFF) / 65535.0f;

				Particle p;
				float sp = ae.emitter.velocitySpread;
				p.pos = emitPos + glm::vec3((r1-0.5f)*sp*0.1f, 0, (r2-0.5f)*sp*0.1f);
				p.vel = ae.emitter.velocity + glm::vec3((r1-0.5f)*sp, r3*sp*0.5f, (r2-0.5f)*sp);
				// Pick color layer based on particle index
				int ci = std::min(i, numColors - 1);
				p.color = (numColors > 0) ? ae.emitter.colors[ci] : glm::vec4(1,1,1,1);
				p.life = ae.emitter.lifeMin + r3 * (ae.emitter.lifeMax - ae.emitter.lifeMin);
				p.maxLife = p.life;
				p.size = ae.emitter.sizeMin + r1 * (ae.emitter.sizeMax - ae.emitter.sizeMin);
				m_particles.addParticle(p);
			}
		}
	}

	// Mob models — all entities (except the player, already drawn above)
	srv.forEachEntity([&](Entity& e) {
		if (e.id() == m_server->localPlayerId()) return; // skip player (drawn separately with character model)

		float mobSpeed = glm::length(glm::vec2(e.velocity.x, e.velocity.z));
		float mobDist = e.getProp<float>(Prop::WalkDistance, 0.0f);
		AnimState mobAnim = {mobDist, mobSpeed, m_globalTime};

		if (e.typeId() == EntityType::Pig)
			mr.draw(m_pigModel, vp, e.position, e.yaw, mobAnim);
		else if (e.typeId() == EntityType::Chicken)
			mr.draw(m_chickenModel, vp, e.position, e.yaw, mobAnim);
		else if (e.typeId() == EntityType::Dog)
			mr.draw(m_dogModel, vp, e.position, e.yaw, mobAnim);
		else if (e.typeId() == EntityType::Cat)
			mr.draw(m_catModel, vp, e.position, e.yaw, mobAnim);
		else if (e.typeId() == EntityType::Villager)
			mr.draw(m_villagerModel, vp, e.position, e.yaw, mobAnim);
		else if (e.typeId() == EntityType::ItemEntity) {
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

	// Lightbulbs above living entities (behavior indicator)
	static BoxModel lightbulb = builtin::lightbulbModel();
	srv.forEachEntity([&](Entity& e) {
		if (e.id() == m_server->localPlayerId()) return;
		if (e.def().category != Category::Animal) return;

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

	// ImGui overlays (equipment, FPS)
	m_ui.beginFrame();

	// Equipment/Inventory UI ([I] to toggle)
	if (pe->inventory) {
		auto* ls = dynamic_cast<LocalServer*>(m_server.get());
		const BlockRegistry& blocks = ls ? ls->server()->world().blocks : m_server->blockRegistry();
		m_equipUI.render(*pe->inventory, blocks,
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
	auto* localSrv = dynamic_cast<LocalServer*>(m_server.get());
	if (!localSrv) { m_state = m_preInspectState; return; }
	World& world = localSrv->server()->world();
	Entity* pe = playerEntity();
	if (!pe) { m_state = GameState::MENU; return; }

	// ESC or right-click closes inspection
	if (m_controls.pressed(Action::MenuBack) || m_controls.pressed(Action::PlaceBlock)) {
		m_gameplay.clearInspection();
		m_state = m_preInspectState; // restore survival/creative
		glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		return;
	}

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

	m_globalTime += dt;

	// Render world in background (frozen, no gameplay update)
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
			auto* ls = dynamic_cast<LocalServer*>(m_server.get());
			if (ls && ls->server()) {
				auto* behaviorState = ls->server()->world().entities.getBehaviorState(eid);
				if (behaviorState) {
					behaviorState->behavior = std::make_unique<PythonBehavior>(handle, newCode);
					printf("[CodeEditor] Python behavior applied to entity %u\n", eid);
				}
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

} // namespace agentworld
