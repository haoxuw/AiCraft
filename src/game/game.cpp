#include "game/game.h"
#include "builtin/models.h"
#include "builtin/characters.h"
#include "builtin/faces.h"
#include "common/entity_manager.h"
#include "common/constants.h"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace aicraft {

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
	printf("=== AiCraft v0.9.0 ===\n");

	if (!m_window.init(1600, 900, "AiCraft")) return false;
	if (!m_renderer.init("shaders")) return false;
	if (!m_text.init("shaders")) return false;
	if (!m_particles.init("shaders")) return false;

	m_controls.load("config/controls.yaml");

	m_hud.init(m_renderer.highlightShader());

	// Characters + faces
	builtin::registerAllCharacters(m_characters);
	builtin::registerAllFaces(m_faces);

	// World templates
	m_templates = {
		std::make_shared<FlatWorldTemplate>(),
		std::make_shared<VillageWorldTemplate>(),
	};
	m_menu.init(m_templates, &m_characters, &m_faces);

	// Parse args
	for (int i = 1; i < argc; i++)
		if (strcmp(argv[i], "--demo") == 0) m_demoMode = true;

	// Models
	m_playerModel = builtin::playerModel();
	m_pigModel = builtin::pigModel();
	m_chickenModel = builtin::chickenModel();

	// Scroll callback
	struct ScrollData { int* slot; Camera* cam; };
	static ScrollData sd = {&m_player.selectedSlot, &m_camera};
	glfwSetWindowUserPointer(m_window.handle(), &sd);
	glfwSetScrollCallback(m_window.handle(), [](GLFWwindow* w, double, double y) {
		auto* d = (ScrollData*)glfwGetWindowUserPointer(w);
		if (d->cam->mode == CameraMode::FirstPerson) {
			*d->slot = ((*d->slot - (int)y) % HOTBAR_SIZE + HOTBAR_SIZE) % HOTBAR_SIZE;
		} else if (d->cam->mode == CameraMode::ThirdPerson) {
			d->cam->orbitDistance = std::clamp(d->cam->orbitDistance - (float)y, 2.0f, 20.0f);
		} else if (d->cam->mode == CameraMode::GodView) {
			d->cam->godDistance = std::clamp(d->cam->godDistance - (float)y * 2, 8.0f, 50.0f);
		} else if (d->cam->mode == CameraMode::RTS) {
			d->cam->rtsHeight = std::clamp(d->cam->rtsHeight - (float)y * 3, 15.0f, 80.0f);
		}
	});

	m_lastTime = std::chrono::steady_clock::now();
	return true;
}

void Game::shutdown() {
	m_hud.shutdown();
	m_particles.shutdown();
	m_text.shutdown();
	m_renderer.shutdown();
	m_window.shutdown();
}

// ============================================================
// Main loop
// ============================================================
void Game::run() {
	while (!m_window.shouldClose()) {
		float dt = beginFrame();
		float aspect = m_window.aspectRatio();
		handleGlobalInput();
		updateAndRender(dt, aspect);
		endFrame();
	}
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
}

void Game::endFrame() {
	m_window.swapBuffers();
}

void Game::saveScreenshot() {
	char p[256];
	snprintf(p, 256, "/tmp/aicraft_screenshot_%d.ppm", m_screenshotCounter++);
	writeScreenshot(m_window.width(), m_window.height(), p);
}

// ============================================================
// State dispatch
// ============================================================
void Game::updateAndRender(float dt, float aspect) {
	switch (m_state) {
	case GameState::MENU: {
		auto action = m_menu.updateMainMenu(dt, m_window, m_text, m_controls,
		                                    aspect, m_demoMode, m_autoScreenTimer);
		m_autoScreenTimer += dt;
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
	case GameState::CREATIVE:
	case GameState::SURVIVAL:
		updatePlaying(dt, aspect);
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
	case MenuAction::ShowTemplateSelect:
		m_state = GameState::TEMPLATE_SELECT;
		break;
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
	}
}

// ============================================================
// World creation
// ============================================================
void Game::enterGame(int templateIndex, GameState targetState) {
	m_world = std::make_unique<World>(42, m_templates[templateIndex]);
	World& world = *m_world;

	m_state = targetState;
	glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	m_camera.mode = CameraMode::FirstPerson;

	if (targetState == GameState::CREATIVE)
		m_player.fillCreativeInventory(world.blocks);
	else
		m_player.fillSurvivalInventory();

	// Find spawn position
	float sx = 30, sz = 30;
	for (int t = 0; t < 50; t++) {
		float h = world.surfaceHeight(sx, sz);
		if (h > 2 && h < 15) break;
		sx += 7; sz += 3;
	}
	float sh = world.surfaceHeight(sx, sz);

	m_player.reset({sx, sh + 1, sz}, m_camera.player.feetPos);
	m_camera.player.yaw = -90;
	m_camera.lookYaw = -90;
	m_camera.lookPitch = -5;
	m_camera.resetSmoothing();
	m_camera.rtsCenter = m_camera.player.feetPos;

	// Generate and mesh chunks around spawn
	world.ensureChunksAround(World::worldToChunk((int)sx, (int)sh, (int)sz), 8);
	m_renderer.meshAllPending(world, m_camera, 8);

	// Spawn mobs
	for (int m = 0; m < 4; m++) {
		float emx = sx + (m % 2 == 0 ? 8.0f : -6.0f) + m * 3;
		float emz = sz + (m % 2 == 0 ? 5.0f : -8.0f) + m * 2;
		float emh = world.surfaceHeight(emx, emz) + 1;
		world.entities.spawn(EntityType::Pig, {emx, emh, emz});
	}
	for (int m = 0; m < 3; m++) {
		float emx = sx + 4.0f + m * 5;
		float emz = sz - 3.0f + m * 4;
		float emh = world.surfaceHeight(emx, emz) + 1;
		world.entities.spawn(EntityType::Chicken, {emx, emh, emz});
	}

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
	if (!m_world) { m_state = GameState::MENU; return; }
	World& world = *m_world;

	if (m_controls.pressed(Action::MenuBack)) {
		m_state = GameState::MENU;
		m_menu.resetCooldown(0.5f);
		return;
	}

	// Day/night
	m_worldTime += m_daySpeed * dt;
	m_renderer.setTimeOfDay(m_worldTime);

	// Gameplay: movement, block interaction, entity ticking, item pickup
	m_gameplay.update(dt, m_state, world, m_player, m_camera, m_controls,
	                  m_renderer, m_particles, m_window);

	renderPlaying(dt, aspect);
}

void Game::renderPlaying(float dt, float aspect) {
	World& world = *m_world;

	// Update chunks
	m_renderer.updateChunks(world, m_camera, 8);

	// Render terrain + sky + crosshair
	auto& hit = m_gameplay.currentHit();
	glm::ivec3 hlPos;
	glm::ivec3* hlPtr = nullptr;
	if (hit && m_camera.mode != CameraMode::RTS) {
		hlPos = hit->blockPos;
		hlPtr = &hlPos;
	}
	m_renderer.render(m_camera, aspect, hlPtr, m_player.selectedSlot);

	// 3D models
	glm::mat4 vp = m_camera.projectionMatrix(aspect) * m_camera.viewMatrix();
	auto& mr = m_renderer.modelRenderer();

	// Player walk animation
	m_globalTime += dt;
	float playerSpeed = glm::length(glm::vec2(m_player.velocity.x, m_player.velocity.z));
	m_playerWalkDist += playerSpeed * dt;
	AnimState playerAnim = {m_playerWalkDist, playerSpeed, m_globalTime};

	if (m_camera.mode != CameraMode::FirstPerson) {
		if (m_characters.count() > 0 && m_faces.count() > 0) {
			BoxModel activeModel = m_characters.buildSelectedModel(m_faces.selected());
			mr.draw(activeModel, vp, m_camera.smoothedFeetPos(), m_camera.player.yaw, playerAnim);
		} else {
			mr.draw(m_playerModel, vp, m_camera.player.feetPos, m_camera.player.yaw, playerAnim);
		}
	}

	// Mob models
	world.entities.forEach([&](Entity& e) {
		float mobSpeed = glm::length(glm::vec2(e.velocity.x, e.velocity.z));
		float mobDist = e.getProp<float>(Prop::WalkDistance, 0.0f);
		AnimState mobAnim = {mobDist, mobSpeed, m_globalTime};

		if (e.typeId() == EntityType::Pig)
			mr.draw(m_pigModel, vp, e.position, e.yaw, mobAnim);
		else if (e.typeId() == EntityType::Chicken)
			mr.draw(m_chickenModel, vp, e.position, e.yaw, mobAnim);
		else if (e.typeId() == EntityType::ItemEntity) {
			float bobY = std::sin(e.getProp<float>(Prop::Age, 0.0f) * 3.0f) * 0.08f;
			float spinYaw = e.getProp<float>(Prop::Age, 0.0f) * 90.0f;
			std::string itemType = e.getProp<std::string>(Prop::ItemType);
			const BlockDef* idef = world.blocks.find(itemType);
			glm::vec3 itemColor = idef ? idef->color_top : glm::vec3(0.8f, 0.5f, 0.2f);
			BoxModel itemModel;
			itemModel.parts.push_back({{0, 0.15f, 0}, {0.12f, 0.12f, 0.12f},
				{itemColor.r, itemColor.g, itemColor.b, 1.0f}});
			mr.draw(itemModel, vp, e.position + glm::vec3(0, bobY, 0), spinYaw, {});
		}
	});

	// Particles
	m_particles.render(vp);

	// HUD
	HUDContext ctx{
		aspect, m_state, m_player.selectedSlot,
		m_player.inventory, m_camera, world,
		m_worldTime, m_currentFPS, m_showDebug,
		hit, m_renderer.sunStrength(),
		world.entities.count(), m_particles.count(),
		m_player.hp, m_player.maxHP, m_player.hunger
	};
	m_hud.render(ctx, m_text, m_renderer.highlightShader());

	// Auto screenshot
	m_autoScreenTimer += dt;
	if (!m_autoScreenDone && m_autoScreenTimer > 3.0f) {
		writeScreenshot(m_window.width(), m_window.height(), "/tmp/aicraft_auto_screenshot.ppm");
		m_autoScreenDone = true;
	}

	// Demo mode
	if (m_demoMode && m_state != GameState::MENU && m_demoStep >= 1) {
		m_demoTimer += dt;
		if (m_demoStep == 1 && m_demoTimer > 2.0f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/aicraft_view_1_fps.ppm");
			m_camera.cycleMode();
			m_camera.orbitYaw = m_camera.player.yaw + 30;
			m_camera.orbitPitch = 25;
			m_demoStep = 2; m_demoTimer = 0;
		}
		if (m_demoStep == 2 && m_demoTimer > 1.5f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/aicraft_view_2_3rd.ppm");
			m_camera.cycleMode();
			m_demoStep = 3; m_demoTimer = 0;
		}
		if (m_demoStep == 3 && m_demoTimer > 1.5f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/aicraft_view_3_god.ppm");
			m_camera.cycleMode();
			m_camera.rtsCenter = m_camera.player.feetPos;
			glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
			m_demoStep = 4; m_demoTimer = 0;
		}
		if (m_demoStep == 4 && m_demoTimer > 1.5f) {
			writeScreenshot(m_window.width(), m_window.height(), "/tmp/aicraft_view_4_rts.ppm");
			printf("Demo complete.\n");
			glfwSetWindowShouldClose(m_window.handle(), true);
			m_demoStep = 99;
		}
	}
}

} // namespace aicraft
