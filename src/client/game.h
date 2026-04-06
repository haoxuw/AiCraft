#pragma once

#include "client/types.h"
#include "client/imgui_menu.h"
#include "client/code_editor.h"
#include "client/behavior_editor.h"
#include "client/equipment_ui.h"
#include "shared/artifact_registry.h"
#include "shared/server_interface.h"
#include "server/behavior_store.h"
#include "client/process_manager.h"
#include "client/gameplay.h"
#include "client/hud.h"
#include "client/window.h"
#include "client/camera.h"
#include "client/renderer.h"
#include "client/text.h"
#include "client/particles.h"
#include "client/controls.h"
#include "client/model.h"
#include "client/model_preview.h"
#include "client/model_icon_cache.h"
#include "client/floating_text.h"
#include "client/ui.h"
#include "client/audio.h"
#include "development/debug_capture.h"
#include "server/world_template.h"
#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>
#include <deque>
#include <string>

namespace agentica {

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
	void enterGame(int templateIndex, GameState targetState,
	               const WorldGenConfig& wgc = WorldGenConfig{});
	void joinServer(const std::string& host, int port, GameState targetState);
	void setupAfterConnect(GameState targetState);
	void saveCurrentWorld();

	// Playing state
	void updatePlaying(float dt, float aspect);
	void renderPlaying(float dt, float aspect, bool skipImGui = false);

	// Entity inspection overlay
	void updateEntityInspect(float dt, float aspect);

	// Code editor overlay
	void updateCodeEditor(float dt, float aspect);

	// Pause overlay (Esc during gameplay)
	void updatePaused(float dt, float aspect);

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
	BehaviorEditorState         m_inspectEditor; // visual behavior tree for entity inspect
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

	// State
	GameState m_state = GameState::MENU;
	float m_worldTime = 0.30f;
	float m_daySpeed = 1.0f / 600.0f;

	// Connection (from --host/--port CLI args)
	std::string m_connectHost;  // empty = singleplayer
	int m_connectPort = 7777;
	int m_serverPort = 0;       // port for local server (0 = auto-pick)
	std::string m_currentWorldPath;  // save directory for current game
	int m_currentSeed = 42;
	std::string m_playerName;        // display name (random default, renameable)
	std::string m_selectedCreature; // selected from artifacts/characters/, auto-set to first

	float m_connectTimer = 0;  // timeout waiting for player entity / WebSocket welcome
	GameState m_connectTargetState = GameState::PLAYING; // state to enter after connect

	// Reconnect on mid-game disconnect
	std::string m_reconnectHost;  // last connected host (empty = local server, no reconnect)
	int m_reconnectPort = 0;
	int m_reconnectAttempt = 0;
	static constexpr int kMaxReconnectAttempts = 3;

	// Timing
	std::chrono::steady_clock::time_point m_lastTime;
	float m_fpsTimer = 0;
	int m_frameCount = 0;
	float m_currentFPS = 0;

	// Startup flags
	bool m_skipMenu = false;   // --skip-menu: skip main menu, start survival world directly

	// Screenshots (F2 manual, or external trigger via /tmp/agentica_screenshot_request)
	int m_screenshotCounter = 0;

	// Debug capture (--debug-scenario flag)
	development::DebugCapture m_debugCapture;

	// Graphics settings (exposed in pause menu)
	int m_renderDistance = 8;
	bool m_vsync = true;
	bool m_showGoalBubbles = true;

	// Display
	bool m_showDebug = false;
	GameState m_preInspectState = GameState::PLAYING;
	GameState m_preMenuState = GameState::PLAYING;

	// Process management (singleplayer: spawns server process)
	AgentManager m_agentMgr;
	std::string m_execDir;           // directory containing agentica-* binaries

	// Server log file (local server → /tmp/agentica_log_local.log)
	FILE* m_serverLog = nullptr;

	// Audio
	AudioManager m_audio;
	float m_creatureSoundTimer = 3.0f;

	// Items the client has sent a PickupItem request for — hidden from rendering immediately
	std::unordered_set<EntityId> m_pendingPickups;

	// Pickup animation: tracks items flying toward the player (client-side visual)
	struct PickupAnim {
		EntityId itemId;
		glm::vec3 startPos;
		glm::vec3 color;
		std::string itemName;
		std::string modelKey; // for rendering actual 3D model during fly
		int count;
		float t = 0;         // 0→1 progress
		float duration = 0.35f;
	};

	// First-person held item animation
	float m_fpSwingTimer = 0;
	float m_fpSwingDuration = 0.25f;
	bool  m_fpSwingActive = false;
	float m_dropCooldown = 0;     // prevents auto-pickup right after dropping
	float m_attackCD = 0;         // per-item attack cooldown
	std::vector<PickupAnim> m_pickupAnims;

	// Floating text (damage numbers, pickup notifications, Minecraft Dungeons style)

	// HP snapshot for damage/death detection (client-side, works over network)
	std::unordered_map<EntityId, int> m_prevEntityHP;

	// Damage flash timer: entity flashes red for this many seconds after taking a hit
	std::unordered_map<EntityId, float> m_damageFlash;

	// Per-entity attack phase (0→1 during swing). Currently only populated for
	// the local player via m_fpSwingTimer; mob swings require server attack events.
	std::unordered_map<EntityId, float> m_entityAttackPhase;


	// Game log — timestamped event stream (damage, deaths, AI decisions, pickups)
	std::deque<std::string> m_gameLog;
	bool m_showGameLog = false;
	void appendLog(const std::string& msg); // prepends game-time timestamp

	// Floating text notifications (damage, pickups, heals)
	FloatingTextManager m_floatText;

	// Models — keyed by base name (model filename without extension, e.g. "pig", "chicken")
	std::unordered_map<std::string, BoxModel> m_models;
	ModelPreview m_modelPreview;
	ModelIconCache m_iconCache;
	float m_playerWalkDist = 0;
	float m_globalTime = 0;
};

} // namespace agentica
