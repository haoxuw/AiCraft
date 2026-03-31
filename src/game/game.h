#pragma once

#include "game/types.h"
#include "game/player.h"
#include "game/menu.h"
#include "game/gameplay.h"
#include "game/hud.h"
#include "client/window.h"
#include "client/camera.h"
#include "client/renderer.h"
#include "client/text.h"
#include "client/particles.h"
#include "client/controls.h"
#include "client/model.h"
#include "common/world.h"
#include "common/world_template.h"
#include "common/character.h"
#include "common/face.h"
#include <chrono>
#include <memory>
#include <vector>

namespace aicraft {

class Game {
public:
	bool init(int argc, char** argv);
	void run();
	void shutdown();

private:
	// Main loop phases
	float beginFrame();
	void handleGlobalInput();
	void updateAndRender(float dt, float aspect);
	void endFrame();

	// State transitions
	void handleMenuAction(const MenuAction& action);
	void enterGame(int templateIndex, GameState targetState);

	// Playing state
	void updatePlaying(float dt, float aspect);
	void renderPlaying(float dt, float aspect);

	// Screenshot
	void saveScreenshot();

	// Subsystems
	Window          m_window;
	Renderer        m_renderer;
	TextRenderer    m_text;
	ParticleSystem  m_particles;
	ControlManager  m_controls;
	Camera          m_camera;

	// Game objects
	std::unique_ptr<World>  m_world;
	Player                  m_player;
	GameplayController      m_gameplay;
	MenuSystem              m_menu;
	HUD                     m_hud;

	// World templates
	std::vector<std::shared_ptr<WorldTemplate>> m_templates;

	// Characters + faces
	CharacterManager m_characters;
	FaceLibrary m_faces;

	// State
	GameState m_state = GameState::MENU;
	float m_worldTime = 0.30f;
	float m_daySpeed = 1.0f / 600.0f;

	// Timing
	std::chrono::steady_clock::time_point m_lastTime;
	float m_fpsTimer = 0;
	int m_frameCount = 0;
	float m_currentFPS = 0;

	// Demo
	bool m_demoMode = false;
	int m_demoStep = 1;
	float m_demoTimer = 0;
	float m_autoScreenTimer = 0;
	bool m_autoScreenDone = false;
	int m_screenshotCounter = 0;

	// Display
	bool m_showDebug = false;

	// Models (lazily initialized)
	BoxModel m_playerModel, m_pigModel, m_chickenModel;
	float m_playerWalkDist = 0;
	float m_globalTime = 0;
};

} // namespace aicraft
