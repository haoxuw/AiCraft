#pragma once

#include "game/types.h"
#include "game/imgui_menu.h"
#include "game/code_editor.h"
#include "game/equipment_ui.h"
#include "shared/artifact_registry.h"
#include "shared/server_interface.h"
#include "server/local_server.h"
#include "server/behavior_store.h"
#include "game/gameplay.h"
#include "game/hud.h"
#include "client/window.h"
#include "client/camera.h"
#include "client/renderer.h"
#include "client/text.h"
#include "client/particles.h"
#include "client/controls.h"
#include "client/model.h"
#include "client/model_preview.h"
#include "client/ui.h"
#include "server/world_template.h"
#include "shared/character.h"
#include "shared/face.h"
#include <chrono>
#include <memory>
#include <vector>

namespace agentworld {

class Game {
public:
	bool init(int argc, char** argv);
	void run();
	void runOneFrame();
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
	void joinServer(const std::string& host, int port, GameState targetState);
	void setupAfterConnect(GameState targetState);
	void saveCurrentWorld();

	// Playing state
	void updatePlaying(float dt, float aspect);
	void renderPlaying(float dt, float aspect);

	// Entity inspection overlay
	void updateEntityInspect(float dt, float aspect);

	// Code editor overlay
	void updateCodeEditor(float dt, float aspect);

	// Screenshot
	void saveScreenshot();

	// Subsystems
	Window          m_window;
	Renderer        m_renderer;
	TextRenderer    m_text;
	ParticleSystem  m_particles;
	ControlManager  m_controls;
	Camera          m_camera;
	UI              m_ui;

	// Server interface — abstracts local vs network server
	// Local: each client runs its own server when creating a game
	// Network: client connects to a remote/global server
	std::unique_ptr<ServerInterface> m_server;

	GameplayController          m_gameplay;
	CodeEditor                  m_codeEditor;
	EquipmentUI                 m_equipUI;
	BehaviorStore               m_behaviorStore;
	ImGuiMenu                   m_imguiMenu;
	ArtifactRegistry            m_artifacts;
	HUD                         m_hud;

	// Player entity (from server)
	Entity* playerEntity() {
		return m_server && m_server->isConnected()
			? m_server->getEntity(m_server->localPlayerId()) : nullptr;
	}

	// World templates
	std::vector<std::shared_ptr<WorldTemplate>> m_templates;

	// Characters + faces
	CharacterManager m_characters;
	FaceLibrary m_faces;

	// State
	GameState m_state = GameState::MENU;
	float m_worldTime = 0.30f;
	float m_daySpeed = 1.0f / 600.0f;

	// Connection (from --host/--port CLI args)
	std::string m_connectHost;  // empty = singleplayer
	int m_connectPort = 7777;
	std::string m_currentWorldPath;  // save directory for current game

	float m_connectTimer = 0;  // timeout waiting for player entity from server

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
	GameState m_preInspectState = GameState::SURVIVAL;
	GameState m_preMenuState = GameState::SURVIVAL;
	bool m_showInventory = false;

	// Models
	BoxModel m_playerModel, m_pigModel, m_chickenModel, m_dogModel, m_catModel, m_villagerModel;
	ModelPreview m_modelPreview;
	float m_playerWalkDist = 0;
	float m_globalTime = 0;
};

} // namespace agentworld
