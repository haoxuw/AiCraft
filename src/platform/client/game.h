#pragma once

#include "client/types.h"
#include "client/imgui_menu.h"
#include "client/code_editor.h"
#include "client/behavior_editor.h"
#include "client/equipment_ui.h"
#include "client/chest_ui.h"
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
#include "client/entity_drawer.h"
#include "client/lightbulb_drawer.h"
#include "client/model_preview.h"
#include "client/model_icon_cache.h"
#include "client/floating_text.h"
#include "client/attack_anim.h"
#include "client/hotbar.h"
#include "client/ui.h"
#include "client/audio.h"
#include "agent/agent_client.h"
#include "development/debug_capture.h"
#include "server/world_template.h"
#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>
#include <deque>
#include <string>

namespace civcraft {

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

	// Loading state — wait for feet chunk before gameplay
	void updateLoading(float dt, float aspect);

	// Playing state
	void updatePlaying(float dt, float aspect);
	void renderPlaying(float dt, float aspect, bool skipImGui = false);

	// updatePlaying helpers (game_playing.cpp)
	bool handleConnectionReconnect(float dt);   // returns true if connection lost/handled
	void handleGameplayInput(float dt);         // WASD, attack, item use/drop/equip
	void updateItemPickupAnimations(float dt);  // proximity scan + pickup fly animations
	void updateAudioAndDoors(float dt);         // creature sounds, door audio/animation

	// renderPlaying helpers (game_render.cpp)
	void renderWorld(float dt, float aspect);                    // chunk mesh, terrain, crosshair
	void renderEntities(float dt, float aspect);                 // entity models, animations, damage flashes
	void renderPickupAnimations();                               // flying item animations
	void renderAnnotations(float aspect);                        // block decorations (flowers, moss)
	void renderEntityEffects(float dt, float aspect);            // goal bubbles, HP bars, lightbulbs
	void renderHUD(float dt, float aspect, bool skipImGui);      // hotbar, equipment, chest overlay, FPS

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

	// AgentClient — controls all NPCs owned by this player.
	// Created after server connection is established (in setupAfterConnect).
	// Ticked in updatePlaying() after network tick, before render.
	std::unique_ptr<AgentClient> m_agentClient;

	// Per-entity drawing modules. Lazily initialised once the renderer /
	// ModelRenderer is ready (Renderer::init fills m_renderer.modelRenderer()).
	std::unique_ptr<EntityDrawer>     m_entityDrawer;
	std::unique_ptr<LightbulbDrawer>  m_lightbulbDrawer;

	GameplayController          m_gameplay;
	CodeEditor                  m_codeEditor;
	BehaviorEditorState         m_inspectEditor; // visual behavior tree for entity inspect
	EquipmentUI                 m_equipUI;
	ChestUI                     m_chestUI;
	BehaviorStore               m_behaviorStore;
	ImGuiMenu                   m_imguiMenu;
	ArtifactRegistry            m_artifacts;
	HUD                         m_hud;
	Hotbar                      m_hotbar; // client-only 10-slot view over player inventory

	// The entity whose input/camera the user is currently driving.
	// Defaults to the local player; switches when the user enters Control mode
	// on another owned entity (via the entity-inspector "Control" button).
	Entity* playerEntity() {
		return m_server && m_server->isConnected()
			? m_server->getEntity(m_server->controlledEntityId()) : nullptr;
	}

	// The literal player-character entity, regardless of Control mode. Used
	// for operations that must target the player's own body (admin flags,
	// disconnect/reconnect loading gates).
	Entity* localPlayerEntity() {
		return m_server && m_server->isConnected()
			? m_server->getEntity(m_server->localPlayerId()) : nullptr;
	}

	// Enter Control mode on `eid` (owned by the local player). Pauses that
	// entity's agent AI; input + camera follow it. Passing localPlayerId()
	// restores normal control.
	void takeControlOf(EntityId eid);
	void releaseControl() { takeControlOf(m_server->localPlayerId()); }

	// World templates
	std::vector<std::shared_ptr<WorldTemplate>> m_templates;

	// State
	GameState m_state = GameState::MENU;
	float m_worldTime = 0.25f;        // start at dawn (morning begins)
	float m_daySpeed = 1.0f / 1200.0f; // 20-min cycle: 5min night/morning/afternoon/evening

	// Connection (from --host/--port CLI args)
	std::string m_connectHost;  // empty = singleplayer
	int m_connectPort = 7777;
	int m_serverPort = 0;       // port for local server (0 = auto-pick)
	std::string m_currentWorldPath;  // save directory for current game
	int m_currentSeed = 42;
	std::string m_playerName;        // display name (random default, renameable)
	std::string m_selectedCreature; // selected from artifacts/living/, auto-set to first humanoid

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
	// Frame-time budget monitoring
	float m_perfTimer = 0;
	int m_slowFrameCount = 0;
	float m_worstFrameMs = 0;

	// Startup flags
	bool m_skipMenu = false;   // --skip-menu: skip main menu, start survival world directly
	bool m_logOnly  = false;   // --log-only: hidden window, event log to /tmp (forces --skip-menu)
	int  m_skipMenuTemplate = 1;  // --template N with --skip-menu: world template index (default village)

	// Screenshots (F2 manual, or external trigger via /tmp/civcraft_screenshot_request)
	int m_screenshotCounter = 0;

	// Debug capture (--debug-scenario flag)
	development::DebugCapture m_debugCapture;

	// Graphics settings (exposed in pause menu)
	int m_renderDistance = 8;
	bool m_vsync = true;
	bool m_showGoalBubbles = true;  // lightbulb icons above AI entities

	// Display
	bool m_showDebug = false;
	bool m_showProfiler = false;

	// Frame profiler (F5) — per-phase timing in milliseconds
	struct FrameProfile {
		float worldMs  = 0;
		float entityMs = 0;
		float hudMs    = 0;
		float totalMs  = 0;
	};
	FrameProfile m_profile;
	bool m_adminFly = false;  // F11 fly (separate from F12 admin)
	GameState m_preInspectState = GameState::PLAYING;
	GameState m_preMenuState = GameState::PLAYING;

	// Process management (singleplayer: spawns server process)
	AgentManager m_agentMgr;
	std::string m_execDir;           // directory containing civcraft-* binaries

	// Audio
	AudioManager m_audio;
	float m_creatureSoundTimer = 3.0f;

	// NOTE: m_proximityTimer removed — agents run in-process now, no proximity notifications needed

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

	// Attack animation — combo sequencer + FPS viewmodel keyframes
	AttackAnimPlayer m_attackAnim;
	std::string      m_comboItemId;  // tracks held item to reload combo on change
	float m_dropCooldown = 0;        // prevents auto-pickup right after dropping
	float m_useCooldown  = 0;        // per-item consume cooldown (read from artifact "cooldown")
	float m_attackCD = 0;            // per-item attack cooldown
	std::vector<PickupAnim> m_pickupAnims;

	// Floating text (damage numbers, pickup notifications, Minecraft Dungeons style)

	// HP snapshot for damage/death detection (client-side, works over network)
	std::unordered_map<EntityId, int> m_prevEntityHP;

	// Last-seen goal per entity — used to log goal-change lines to the in-game log
	std::unordered_map<EntityId, std::string> m_entityGoals;

	// Last-seen inventory per entity — used to derive pickup/drop/deposit events
	// from S_INVENTORY deltas. Logged category = INV.
	std::unordered_map<EntityId, std::unordered_map<std::string,int>> m_prevInv;

	// Damage flash timer: entity flashes red for this many seconds after taking a hit
	std::unordered_map<EntityId, float> m_damageFlash;

	// NOTE: the old m_entityAttackPhase / m_entityWorkTimer maps were removed
	// when mob tool-swing animation migrated to the named-clip system.
	// Mobs now pick clips from goalText via pickClip() in game_render.cpp.


	// Game log — timestamped event stream (damage, deaths, AI decisions, pickups)
	std::deque<std::string> m_gameLog;
	bool m_showGameLog = false;
	void appendLog(const std::string& msg); // prepends game-time timestamp

	// Floating text notifications (damage, pickups, heals)
	FloatingTextManager m_floatText;

	// Door swing animations (client-side, 0.25s rotation overlay)
	std::vector<DoorAnim> m_doorAnims;

	// Models — keyed by base name (model filename without extension, e.g. "pig", "chicken")
	std::unordered_map<std::string, BoxModel> m_models;
	ModelPreview m_modelPreview;
	ModelIconCache m_iconCache;
	float m_playerWalkDist = 0;
	float m_globalTime = 0;

	// Local-player named animation clip (dance, wave, ...). Empty = default
	// walk/idle. Toggled by keybinds (C = dance) and cleared automatically
	// when the player starts moving so the walk cycle takes over.
	std::string m_playerClip;

	// Debug: scenario-driven override for the animation clock fed into the
	// player model. Negative = use real time (m_globalTime). Used by
	// character_views to sample a clip at specific phases.
	float m_debugAnimTime = -1.0f;

	// Debug: scenario-driven override for the walk cycle. Negative walk
	// phase = no override (use m_playerWalkDist). When set, overrides both
	// walkDistance (as phase / model.walkCycleSpeed-ish) and speed.
	float m_debugWalkPhase  = -1.0f;
	float m_debugWalkSpeed  = 0.0f;

	// Head/body target tracking: when attacking or mining, the model
	// looks at the target.  In TPS/RPG/RTS, the head also tracks the
	// camera yaw with a ±45° neck limit; excess rotates the body.
	EntityId m_lastAttackTargetId = ENTITY_NONE;
	float m_actionBodyYawOffset  = 0.0f; // smoothed body rotation toward action target
	float m_cameraBodyYawOffset  = 0.0f; // smoothed body rotation toward camera look dir
};

} // namespace civcraft
