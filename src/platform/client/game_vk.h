#pragma once

// Vulkan-native CivCraft client — built on top of the RHI.
//
// Always connects to a civcraft-server over TCP. main.cpp spawns a local
// server if no --port is supplied.
//
// Systems (all talk to civcraft::rhi::IRhi):
//   * GameState machine — MENU → PLAYING → PAUSED → DEAD → MENU.
//   * Server-streamed chunked voxel world via ChunkMesher.
//   * Server entities rendered as box-model silhouettes (humanoid / quad /
//     flyer) with lightbulb + goal label + HP bar overhead.
//   * Player — WASD + 4 camera modes (FPS/TPS/RPG/RTS), click-to-move,
//     server-authoritative position via clientPos.
//   * Combat — left-click swing → Convert proposal (server validates).
//   * HUD — main menu (ImGui), hotbar (10 slots), HP bar, FPS counter.
//   * Particles — torch flames + fireflies.

#include "client/rhi/rhi.h"
#include "client/audio.h"
#include "client/camera.h"
#include "client/debug_triggers.h"
#include "client/entity_raycast.h"
#include "client/rts_executor.h"
#include "logic/artifact_registry.h"

// CivCraft chunk plumbing — civcraft-ui-vk now stores its world in real
// 16³ Chunks and meshes them through ChunkMesher rather than streaming a
// flat instance buffer. Pulling the headers in here means Game.h can hold
// per-chunk MeshHandles directly; only the .cpp needs ChunkMesher.
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/chunk_source.h"
#include "client/hotbar.h"
#include "client/box_model.h"
#include "client/equipment_ui.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct GLFWwindow;

namespace civcraft {
	struct Entity;
	class ServerInterface;
	class AgentClient;
	class BehaviorStore;
}

namespace civcraft::vk {

// ── Tuning ────────────────────────────────────────────────────────────────
// Keep tuning numbers at the top of the header so all systems can see the
// same values. Follows the "Rule 1 / no-hardcoded-magic" spirit — if this
// were the real CivCraft we'd load these from Python.
struct Tuning {
	// Physics
	float gravity       = -20.0f;

	// Player
	float playerSpeed   = 6.0f;
	float playerJumpV   = 8.0f;
	int   playerMaxHP   = 100;
	float playerHPRegen = 6.0f;   // hp/s when out of combat for hpRegenDelay
	float hpRegenDelay  = 3.0f;   // s since last damage before regen starts
	float playerHeight  = 1.8f;
	float playerRadius  = 0.35f;

	// Camera
	float camDistance   = 5.5f;   // 3rd-person orbit distance
	float camHeight     = 1.8f;
	float camSensYaw    = 0.0025f;
	float camSensPitch  = 0.0025f;

	// Combat
	float attackRange   = 3.0f;
	float attackCone    = 1.0f;   // radians; ±57° front cone
	float attackCD      = 0.45f;
	int   attackDmg     = 18;

	// Hold-to-repeat cooldowns (LMB attack reuses attackCD above).
	float placeCD       = 0.18f;  // RMB place rate — ~5.5 blocks/s held
	float dropCD        = 0.35f;  // Q drop rate — ~3 items/s held
};

extern Tuning kTune;


// No Player struct — position, velocity, HP all live on the Entity
// (via playerEntity()). Client-only physics/animation state is kept
// as individual Game members below. See 10_CLIENT_SERVER_PHYSICS.md.

// ── Floating text ─────────────────────────────────────────────────────────
struct FloatText {
	glm::vec3 worldPos{};
	glm::vec3 color{1,1,1};
	std::string text;
	float t = 0.0f;
	float lifetime = 1.2f;
	float rise = 1.6f;   // world units risen over lifetime
};

// ── Game ──────────────────────────────────────────────────────────────────
enum class GameState { Menu, Loading, Playing, GameMenu, Dead };

// Sub-screens within the Menu state. Main → Singleplayer/Multiplayer/Settings.
// Stage C will flesh out Singleplayer (world list + create-world) and
// Multiplayer (LAN/Saved/Direct-Connect tabs); today they're placeholders
// that delegate to the existing auto-spawn-and-join flow.
enum class MenuScreen : uint8_t { Main, Singleplayer, Multiplayer, Settings };

class Game {
public:
	Game();
	// Out-of-line so unique_ptr<BehaviorStore/AgentClient> sees the complete
	// type in game_vk.cpp and main.cpp doesn't have to pull those headers.
	~Game();
	Game(const Game&) = delete;
	Game& operator=(const Game&) = delete;

	bool init(rhi::IRhi* rhi, GLFWwindow* window);
	void shutdown();

	// Server connection (required — civcraft-ui-vk has no local demo world).
	// Non-owning — caller keeps the server alive. Must be called before init().
	void setServer(civcraft::ServerInterface* s) { m_server = s; }

	// Skip the main menu and drop straight into gameplay. Used by
	// `--skip-menu` for headless/CI flows.
	void skipMenu() { enterPlaying(); }

	// Poll input, step sim, render one frame.
	void runOneFrame(float dt, float wallTime);

	// Check if we should quit (menu → Quit, or window close).
	bool shouldQuit() const { return m_shouldQuit; }

	// Window focus notification — opens the in-game menu on focus loss.
	void onWindowFocus(bool focused);

	// Mouse-wheel routed from the platform shell. ImGui consumes first; if the
	// cursor isn't over a UI widget the game dispatches per camera mode
	// (hotbar cycle / orbit zoom / RTS zoom-to-cursor).
	void onScroll(double xoff, double yoff);

private:
	// ── Scene transitions ─────────────────────────────────────────────────
	void enterMenu();
	void enterPlaying();
	void openGameMenu();
	void closeGameMenu();
	void enterDead(const char* cause);
	void respawn();

	// ── Sim phases (playing) ──────────────────────────────────────────────
	void processInput(float dt);
	void tickPlayer(float dt);
	void tickCombat(float dt);
	void tickFloaters(float dt);
	// Proximity scan + Relocate send + anim tick. Reads
	// pickup_range and pickup_fly_duration from the picker's EntityDef.
	void updatePickups(float dt);

	// ── Render phases ─────────────────────────────────────────────────────
	void renderWorld(float wallTime);           // sky + shadows + voxels
	void renderEntities(float wallTime);        // player + NPCs as box-models
	void renderEffects(float wallTime);         // particles + slash ribbons
	void renderHotbarItems3D();                 // held-item models in hotbar slots
	void renderHUD();                           // lightbulbs, HP bars, hotbar
	void renderMenu();                          // main menu (ImGui + RHI UI)
	void renderGameMenu();                      // in-game menu overlay
	void renderDeath();                         // death overlay + respawn btn
	void renderDebugOverlay();                  // F3 debug stats
	void renderTuningPanel();                   // F6 render-tuning sliders
	void renderHandbook();                      // H handbook browser
	void renderRTSSelect();                     // box selection rectangle
	void renderChestUI();                       // chest inventory transfer
	void renderEntityInspect();                 // entity inspection overlay

	// Projects a world-space anchor to NDC. Returns false if behind camera
	// or clipped. `out` receives {ndcX, ndcY, depthLinear}.
	bool projectWorld(const glm::vec3& world, glm::vec3& out) const;

	// Camera math — delegates to m_cam; viewProj adds Vulkan Y-flip.
	glm::mat4 viewProj() const;
	// Picking/mouse unprojection matrix: no Y-flip, no camera shake. NDC here
	// uses +1=top (matches how mouse coords are computed), so unprojecting
	// gives correct rays. Use this for cursor ↔ world math.
	glm::mat4 pickViewProj() const;

	// Keep the camera out of terrain: cast a ray from the orbit target toward
	// m_cam.position and pull forward if it hits a solid block, so the camera
	// never clips into walls when the player backs into one. FPS is skipped
	// (camera is already inside the player's collision capsule).
	void clampCameraCollision();

	// World mutation (right-click + headless file trigger).
	void digInFront();
	// Server-mode block placement: raycast → place at adjacent air cell.
	void placeBlock();
	// RPG / RTS click-to-move: raycast through cursor NDC → ground cell →
	// sendSetGoal so the server's greedy steering walks us there. Uses the
	// screen cursor in top-down modes (not the camera forward vector).
	void clickToMove();

	// Server-mode combat: cone-pick nearest entity → Convert proposal.
	// Returns true if a target was hit. Shared by LMB input + file trigger.
	bool tryServerAttack();

	// Server-mode chunk streaming (#24): walks a render-radius box around the
	// player chunk, meshes any chunk that the server has delivered but we
	// haven't uploaded yet, and re-meshes anything in m_serverDirtyChunks
	// (populated by the onChunkDirty callback). Both passes are throttled so
	// a player teleporting into a fresh world doesn't stall the frame.
	void streamServerChunks();
	// Mesh+upload one ChunkPos against the active source (server or local
	// world). Creates a new MeshHandle if absent, updateChunkMesh if present.
	void uploadChunkMesh(civcraft::ChunkPos cp);

	// The entity the player is currently controlling. Returns nullptr if
	// the server hasn't delivered the entity yet. ALL player position,
	// velocity, and HP reads go through here — no dual state.
	civcraft::Entity* playerEntity();

	// XZ forward vector from the camera's look yaw (pitch is camera-only).
	glm::vec3 playerForward() const;

	// ── State ─────────────────────────────────────────────────────────────
	rhi::IRhi*   m_rhi    = nullptr;
	GLFWwindow*  m_window = nullptr;
	GameState    m_state  = GameState::Menu;
	bool         m_shouldQuit = false;
	civcraft::ServerInterface* m_server = nullptr;  // non-owning; always set
	// Agents run inside this process now (server stopped spawning civcraft-agent
	// children). One BehaviorStore + one AgentClient drives every NPC the
	// server hands us. unique_ptr so we can defer construction until after the
	// server handshake (BehaviorStore needs Python initialized) and tear them
	// down before the server interface goes away.
	std::unique_ptr<civcraft::BehaviorStore> m_behaviorStore;
	std::unique_ptr<civcraft::AgentClient>   m_agentClient;

	// Menu
	float        m_menuTitleT = 0.0f;
	std::string  m_lastDeathReason;
	MenuScreen   m_menuScreen = MenuScreen::Main;

	// One persistent chunk-mesh handle per loaded chunk. ChunkMesher emits
	// world-space vertices, so each handle just needs drawChunkMeshOpaque +
	// renderShadowsChunkMesh once per frame. A dig() invalidates the chunk
	// (and its face-adjacent neighbors), Game re-meshes them, and we call
	// updateChunkMesh in place so the handle stays valid.
	std::unordered_map<civcraft::ChunkPos, rhi::IRhi::MeshHandle,
	                   civcraft::ChunkPosHash> m_chunkMeshes;
	// Chunks the server has marked dirty (initial delivery or block change).
	// Drained by streamServerChunks() each frame.
	std::unordered_set<civcraft::ChunkPos, civcraft::ChunkPosHash> m_serverDirtyChunks;
	std::vector<FloatText> m_floaters;

	// Client-only physics/animation state (not on Entity).
	bool         m_onGround  = false;
	float        m_walkDist  = 0.0f;  // animation swing phase
	float        m_attackCD  = 0.0f;  // cooldown until next swing
	float        m_placeCD   = 0.0f;  // cooldown until next RMB block place
	float        m_dropCD    = 0.0f;  // cooldown until next Q drop
	float        m_regenIdle = 0.0f;  // seconds since last damage

	// Third-person body yaw (radians). In FPS it's snapped to cam.lookYaw each
	// frame (body is hidden anyway). In TPS/RPG/RTS it lerps toward the
	// horizontal velocity direction when the player is moving, and holds
	// otherwise — so the character faces the way they're running (Fortnite/
	// ARPG convention) rather than whipping with the camera.
	float        m_playerBodyYaw = 0.0f;
	bool         m_playerBodyYawInit = false;

	// Sprint FOV punch — layered on Camera::fov. Target = +6° while Shift-
	// sprinting in FPS/TPS, 0 otherwise. Asymmetric rates for feel: faster
	// ramp up (snap to a full run) than ramp down (ease back on stop).
	float        m_sprintFovBoost = 0.0f;

	// First-time-in-mode hint — bit i set once we've already shown the toast
	// for CameraMode(i); cleared on enterPlaying().
	unsigned     m_modeHintsShown = 0;

	// Climb/mantle animation. Physics instantly snaps the player up a ledge
	// (Minecraft-style step-up at `stepHeight = 1.0f`), but to the eye we
	// smoothly interpolate the rendered Y and raise an FPS hand toward the
	// ledge for the duration. Purely cosmetic — no effect on collision or
	// server sync. Duration scales with height (base 0.22s per 1-block climb).
	struct ClimbAnim {
		float     t        = 0.0f;
		float     duration = 0.0f;
		float     fromY    = 0.0f;
		float     toY      = 0.0f;
		glm::vec2 forward  = {0, 0};   // XZ direction climbed (hand reach)
		bool      active() const { return duration > 0.0f && t < duration; }
	} m_climb;

	// Combat VFX — one active slash ribbon per swing.
	struct Slash {
		glm::vec3 center;
		glm::vec3 dir;       // XZ world direction the swing faces
		float t = 0.0f;
		float duration = 0.35f;
		glm::vec3 early{4,4,2.5f};
		glm::vec3 late{1.4f,1.0f,0.4f};
	};
	std::vector<Slash> m_slashes;

	// Mining hit event — particle burst at block face per swing.
	struct HitEvent {
		glm::vec3 pos{};
		glm::vec3 color{1,1,1};
		float     t = 0;
		bool      active = false;
	};
	std::vector<HitEvent> m_hitEvents;

	// Dropped-item pickup — single source of truth.
	// An anim for itemId == eid means the client has optimistically claimed
	// that ItemEntity: the Relocate is already in flight, the ground cube is
	// hidden, and updatePickups() treats the entity as taken. At t=1 we
	// confirm outcome from server state — entity removed = success, entity
	// still present = denied (race / out-of-range / capacity rejected).
	// Duration comes from the picker's EntityDef::pickup_fly_duration so
	// tuning lives in artifacts, not here.
	struct PickupAnim {
		civcraft::EntityId itemId = 0;
		glm::vec3 startPos{0};
		glm::vec3 color{1};
		std::string itemType;   // "base:dirt" etc. — HUD display key
		int count = 1;
		float t = 0.0f;
		float duration = 0.0f;
	};
	std::vector<PickupAnim> m_pickupAnims;

	// Door swing — hinged-panel sweep between door ↔ door_open.
	struct DoorAnim {
		glm::ivec3 basePos{0,0,0};
		int        height = 1;
		float      timer  = 0.0f;
		bool       opening = true;
		bool       hingeRight = false;
		glm::vec3  color{0.55f, 0.35f, 0.15f};
	};
	std::vector<DoorAnim> m_doorAnims;
	rhi::IRhi::MeshHandle m_doorAnimMesh = rhi::IRhi::kInvalidMesh;

	float        m_hitmarkerTimer = 0.0f;
	bool         m_hitmarkerKill  = false;

	// FPS hand swing — camera-relative held-item box. -1 = idle.
	static constexpr float kHandSwingDur = 0.28f;
	float        m_handSwingT = -1.0f;
	glm::vec3    m_handColor{0.78f, 0.62f, 0.48f};

	// Damage feedback — vignette alpha and camera-shake timer/intensity.
	float        m_damageVignette = 0.0f;
	float        m_cameraShake    = 0.0f;
	float        m_shakeIntensity = 0.0f;

	struct Notification {
		std::string text;
		glm::vec3   color{0.95f, 0.92f, 0.78f};
		float       t = 0.0f;
		float       lifetime = 3.5f;
	};
	std::vector<Notification> m_notifs;
	void pushNotification(const std::string& text,
	                      glm::vec3 color = glm::vec3(0.95f, 0.92f, 0.78f),
	                      float lifetime = 3.5f);

	// Input latches
	bool         m_mouseCaptured = false;
	bool         m_lmbLast       = false;
	bool         m_rmbLast       = false;
	bool         m_mmbLast       = false;
	bool         m_spaceLast     = false;
	bool         m_escLast       = false;
	bool         m_vLast         = false;
	bool         m_tabLast       = false;
	bool         m_eLast         = false;
	bool         m_f3Last        = false;
	bool         m_f6Last        = false;
	bool         m_f12Last       = false;
	bool         m_f11Last       = false;
	bool         m_hLast         = false;   // H: handbook
	bool         m_numKeyLast[10] = {};  // 1..0 → hotbar slots 0..9

	// RPG/RTS right-click orbit: hold+drag to orbit camera, quick click = action.
	// wantCapture is set only while actively orbiting.
	struct RightClickState {
		bool   held     = false;
		bool   orbiting = false;
		bool   action   = false;   // set on quick-release → triggers dig/place
		double startX   = 0;
		double startY   = 0;
	} m_rightClick;

	// UI overlay wants cursor free (inventory, handbook, chest, etc.)
	bool         m_uiWantsCursor = false;

	// Inventory panel (Tab toggle) — Diablo-style equipment UI.
	// m_invOpen is the authoritative state; m_equipUI.render() is a no-op
	// when closed. Kept as a separate bool (rather than delegating to
	// EquipmentUI::isOpen) so the cursor/capture logic reads a single field.
	bool                m_invOpen = false;
	civcraft::EquipmentUI m_equipUI;

	// Chest interaction UI
	struct ChestUI {
		bool   open     = false;
		glm::ivec3 pos{0, 0, 0};
		civcraft::EntityId chestEid = 0;
	} m_chestUI;

	// F3 debug overlay
	bool         m_showDebug = false;

	// F6 render-tuning panel — drives the composite GradingParams UBO.
	bool                    m_showTuning = false;  // toggled by F6
	rhi::IRhi::GradingParams m_grading = rhi::IRhi::GradingParams::Vivid();

	// H handbook panel
	bool         m_handbookOpen = false;
	civcraft::ArtifactRegistry m_artifactRegistry;

	// Python-defined BoxModels for all entities + items, loaded once at
	// startup via model_loader::loadAllModels. Keyed by model stem (e.g.
	// "pig", "sword"). Used by the box-model flattener to render oriented
	// parts with walk/clip/held-item animation.
	std::unordered_map<std::string, civcraft::BoxModel> m_models;

	// RTS box selection + long-press (Walk vs Build command)
	struct RTSSelection {
		bool dragging = false;
		glm::vec2 start{0, 0};
		glm::vec2 end{0, 0};
		std::vector<civcraft::EntityId> selected;
	} m_rtsSelect;
	struct RTSLongPress {
		bool   active    = false;
		double startTime = 0;
		glm::vec2 startNdc{0, 0};
	} m_rtsLongPress;
	static constexpr float kBuildHoldSec = 1.0f;
	civcraft::RtsExecutor m_rtsExec;
	struct MoveOrder { glm::vec3 target; bool active; };
	std::unordered_map<civcraft::EntityId, MoveOrder> m_moveOrders;

	// Screen
	int          m_fbW = 0, m_fbH = 0;
	float        m_aspect = 1.0f;

	// Wall-clock time (for sky/particle animation phases that don't
	// want to pause with the game).
	float        m_wallTime = 0.0f;

	// Camera — uses the shared Camera class (FPS/TPS/RPG/RTS modes).
	civcraft::Camera m_cam;

	// Admin mode (F12 toggle, F11 fly in admin)
	bool         m_adminMode = false;
	bool         m_flyMode   = false;

	// Score counter (coins from killed NPCs). Shown in hotbar slot 0.
	int          m_coins = 0;

	// Active hotbar slot (0..9). Number keys 1-9,0 select. Drives the
	// selection highlight in renderHUD() and which item future equip logic
	// will equip.
	int          m_hotbarSlot = 0;

	// Client-side 10-slot alias map over the player's Inventory. Server
	// stores only {itemId -> count}; the hotbar is purely a local rearrange
	// layer so the player can bind number keys to whichever items they want.
	// First S_INVENTORY after a fresh session either restores the saved
	// layout from config/hotbar.txt or falls back to repopulateFrom;
	// subsequent updates use mergeFrom so drag-drop assignments survive.
	civcraft::Hotbar m_hotbar;
	bool             m_hotbarLoaded = false;
	std::string      m_hotbarSavePath = "config/hotbar.txt";

	// Last block the player dug — placeBlock() uses this as the Convert
	// source so placement works without a full inventory UI (one dig → one
	// place). Falls back to "cobblestone" for fresh admin-mode players.
	std::string  m_lastDugBlock = "cobblestone";

	// Survival-mode block breaking — 3 hits on the same block to break.
	struct BreakState {
		glm::ivec3 target{0, 0, 0};
		int   hits   = 0;
		float timer  = 0;
		bool  active = false;
	} m_breaking;
	float m_breakCD = 0;

	// Click-to-move order (RPG/RTS). While active, tickPlayer drives the player
	// toward m_moveOrderTarget like a virtual joystick — client prediction
	// stays consistent with server authority. WASD cancels; arrival (<1.5b) clears.
	bool         m_hasMoveOrder = false;
	glm::vec3    m_moveOrderTarget{0};

	// GameLogger state tracking — detect deltas across frames for DECIDE,
	// COMBAT, DEATH, INV categories.
	std::unordered_map<civcraft::EntityId, std::string> m_entityGoals;
	std::unordered_map<civcraft::EntityId, int>         m_prevEntityHP;
	std::unordered_map<civcraft::EntityId, std::unordered_map<std::string,int>> m_prevInv;

	// Entity inspection (right-click on entity in RPG/RTS, or Shift+RMB in FPS/TPS)
	civcraft::EntityId m_inspectedEntity = 0;

	// Window focus — pause gameplay when window loses focus.
	bool         m_windowFocused = true;

	// File-based debug triggers (compiled out in Release via NDEBUG).
	civcraft::DebugTriggers m_debugTriggers;

	// Audio — miniaudio-backed spatial sound. All sounds are derived
	// client-side from the TCP stream (Rule 5). Listener follows the camera
	// each frame in tickPlayer.
	civcraft::AudioManager m_audio;
	int   m_lastWalkStep    = 0;  // integer floor of m_walkDist at last footstep
	float m_footstepCooldown = 0.0f;

	// Per-frame scratch buffers. Vectors are cleared (not freed) between
	// frames so capacity amortizes — avoids ~30KB of heap churn every frame
	// across terrain/entity/particle/ribbon streams.
	struct Scratch {
		std::vector<float> charBoxes;
		std::vector<float> hlBoxes;      // 12-edge block highlight wireframe
		std::vector<float> fpsHand;      // FPS held-item single box
		std::vector<float> particles;
		std::vector<float> ribbons;
		std::vector<float> hitParts;
		std::vector<float> spinParts;
		std::vector<float> crackParts;
		std::vector<float> doorVerts;
		std::vector<civcraft::RaycastEntity> ents;
	} m_scratch;
};

} // namespace civcraft::vk
