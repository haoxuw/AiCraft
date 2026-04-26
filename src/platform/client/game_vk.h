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
//   * HUD — custom-drawn menus, HP bar, FPS counter.
//   * Particles — torch flames + fireflies.

#include "client/rhi/rhi.h"
#include "client/audio.h"
#include "client/audio_capture.h"
#include "client/camera.h"
#include "client/debug_triggers.h"
#include "client/dialog_panel.h"
#include "client/entity_raycast.h"
#include "client/path_executor.h"
#include "logic/artifact_registry.h"
#include "llm/llm_client.h"
#include "llm/llm_sidecar.h"
#include "llm/tts_client.h"
#include "llm/tts_sidecar.h"
#include "llm/tts_voice_mux.h"
#include "llm/whisper_client.h"
#include "llm/whisper_sidecar.h"

// CivCraft chunk plumbing — civcraft-ui-vk now stores its world in real
// 16³ Chunks and meshes them through ChunkMesher rather than streaming a
// flat instance buffer. Pulling the headers in here means Game.h can hold
// per-chunk MeshHandles directly; only the .cpp needs ChunkMesher.
#include "logic/chunk.h"
#include "logic/block_registry.h"
#include "logic/chunk_source.h"
#include "client/box_model.h"
#include "client/async_chunk_mesher.h"
#include "client/game_vk_renderers.h"
#include "client/handbook_panel.h"
#include "client/hotbar.h"
#include "client/lan_browser.h"
#include "client/loading_screen.h"
#include "client/menu_plaza.h"
#include "client/screen_shell.h"
#include "agent/agent_client.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
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

	// Player — speed and jump live on the Living artifact (def.walk_speed /
	// def.jump_velocity); kTune used to mirror them but that broke SST. See
	// artifacts/living/base/guy.py.
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

// Sub-screens within the Menu state. Main → CharacterSelect (singleplayer) or
// Multiplayer → CharacterSelect → Connecting → Playing. Multiplayer has its
// own LAN-browser screen; singleplayer jumps straight to character pick.
enum class MenuScreen : uint8_t { Main, CharacterSelect, Connecting, Multiplayer, Handbook, Settings };

// LoadingGate + LoadingScreen live in client/loading_screen.h.

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
	void setServer(civcraft::ServerInterface* s) {
		m_server = s;
		// Build a stable WorldView bound to the server's chunks + block
		// registry and hand it to the path executor for wall-slide probing.
		// Doing this here (not on every driveRemote) keeps the executor's
		// probe paths branch-free on the hot tick.
		if (s) {
			m_pathWorldView = std::make_unique<civcraft::ChunkWorldView>(
				s->chunks(), s->blockRegistry());
			m_pathExec.setWorldView(m_pathWorldView.get());
		} else {
			m_pathExec.setWorldView(nullptr);
			m_pathWorldView.reset();
		}
	}

	// Remember which world to connect to; actual handshake is deferred
	// until the user picks a character (or skipMenu fires).
	void setPendingConnect(int seed, int templateIndex) {
		m_pendingSeed = seed; m_pendingTemplate = templateIndex;
	}

	// Seed the AgentClient knobs before it's constructed (happens inside
	// init()). No-op for defaults; CLI flags in client/main.cpp populate it.
	void setAgentConfig(const civcraft::AgentClient::Config& cfg) {
		m_agentCfg = cfg;
	}

	// Send C_HELLO with the chosen creatureType (empty ⇒ server-default).
	// Blocking: busy-waits for S_WELCOME. Only safe for one-shot startup
	// paths like skipMenu() where there's no UI window to keep pumping.
	bool connectAs(const std::string& creatureType);

	// Non-blocking variant for the CharacterSelect UI: sends HELLO and
	// returns immediately. Caller transitions MenuScreen to Connecting and
	// polls m_server->pollWelcome() each frame.
	bool beginConnectAs(const std::string& creatureType);

	// Skip the main menu and go straight to the Connecting loading screen
	// (brass progress meter), then enterPlaying() once S_WELCOME arrives.
	// Used by `--skip-menu` for `make game` and headless/CI flows. The async
	// path is what production `make client` uses; unifying skip-menu onto it
	// means world-prep shows the same progress UI instead of a frozen window.
	void skipMenu() {
		if (beginConnectAs("")) {
			m_state = GameState::Menu;
			m_menuScreen = MenuScreen::Connecting;
		}
	}

	// While true, skip drawing the native bitmap-font menu UI — the CEF HTML
	// overlay (composited in the RHI) is providing the menu instead.
	void setCefMenuActive(bool on) { m_cefMenuActive = on; }
	bool cefMenuActive() const { return m_cefMenuActive; }

	// Poll input, step sim, render one frame.
	void runOneFrame(float dt, float wallTime);

	// Check if we should quit (menu → Quit, or window close).
	bool shouldQuit() const { return m_shouldQuit; }

	// Mouse-wheel routed from the platform shell. Dispatches per camera mode
	// (FPS hotbar scroll, orbit zoom, RTS zoom-to-cursor).
	void onScroll(double xoff, double yoff);

	// Unicode codepoint from GLFW char callback — used by DialogPanel to
	// consume typed text. Queued here and drained in processInput so the
	// panel never races the input loop.
	void onChar(uint32_t codepoint);
	// Non-char keys (Backspace/Enter/Esc) forwarded from GLFW key callback;
	// queued for DialogPanel to consume when open.
	void onKey(int glfwKey, int action);

	// Inventory-UI slot record, published by the 2D pass each frame so the
	// next frame's 3D item-preview pass can render box models at the exact
	// same screen locations. Public so the inventory renderer (a friend of
	// HudRenderer, not of Game) can read/write it through the public API.
	//
	// `kind` + `index` let the hit-tester translate a cursor-over-rect hit
	// into a logical slot for drag/drop resolution without re-walking the
	// rendered layouts.
	struct SlotRect {
		enum class Kind : uint8_t {
			Hotbar,     // index = 0..9
			Inventory,  // index = position in sorted items list
			Other,      // index = position in other entity's inventory
			DragGhost   // index = -1 (follows cursor, not hit-testable)
		};
		float ndcX = 0, ndcY = 0, ndcW = 0, ndcH = 0;
		std::string itemId;
		int  count = 0;
		bool selected = false;
		Kind kind = Kind::Inventory;
		int  index = -1;
	};
	enum class InvSort : uint8_t { ByName = 0, ByValue = 1, ByCount = 2 };

	// Drag-and-drop state. Lives on Game so input (processInput) can start/
	// advance/end the drag and the renderer can read it to paint the ghost.
	struct DragState {
		bool        active = false;
		std::string itemId;
		int         count = 0;
		SlotRect::Kind srcKind = SlotRect::Kind::Inventory;
		int            srcIndex = -1;
	};

private:
	// ── init() helpers (see game_vk.cpp). Factored out so init() reads
	//    as a short outline — each helper is one bounded sub-stage. ────
	void setupServerCallbacks();    // S_BLOCK / S_INVENTORY / S_REMOVE
	void registerDebugTriggers();   // /tmp/civcraft_vk_*_request hooks
	void initAiSidecars();          // LLM / Whisper / TTS sidecar spawns

	// ── processInput() helpers. Each returns control to processInput so
	//    the outer function keeps orchestrating sequencing + early-exit.
	void drainDialogInput();        // text/key queue → DialogPanel
	void processEscapeKey();        // ESC: drag → dialog → inv → pause
	void processTalkKey();          // T key: open dialog with NPC under cursor

	// ── tickPlayer() helpers. The raw LMB/RMB/MMB event handling is each
	//    hundreds of lines. Extracted so tickPlayer reads as an outline of
	//    "movement → cooldowns → mouse buttons".
	void processLmbInput(float dt);  // attack / dig / RTS box-select / wheel
	void processRmbInput(float dt);  // inspect / place / drag-circle / wheel
	void processMmbInput();          // rotate-or-eyedropper on held block

	// If `bp` holds a Chest block, locate the matching Structure entity,
	// open the inventory panel with m_invOther pointed at it, and return
	// true. Returns false if the block isn't a chest (caller falls through
	// to the normal interact/place path). Shared by the E key and RMB.
	bool tryOpenChestAt(glm::ivec3 bp);

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
	// Presentation is decomposed into five friend renderer classes that hold
	// only a `Game&` back-reference — see game_vk_renderers.h. The state
	// (camera, server, m_rhi, inventories, …) lives here; the pixel-pushing
	// code lives there.
	friend class WorldRenderer;
	friend class HudRenderer;
	friend class MenuRenderer;
	friend class PanelRenderer;
	friend class EntityUiRenderer;
	friend class HandbookPanel;
	WorldRenderer     m_worldRenderer    { *this };
	HudRenderer       m_hudRenderer      { *this };
	MenuRenderer      m_menuRenderer     { *this };
	bool              m_cefMenuActive    = false;
	PanelRenderer     m_panelRenderer    { *this };
	EntityUiRenderer  m_entityUiRenderer { *this };

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
	// Cycle m_placementParam2 forward by one within the held block's
	// rotationCount(). No-op if the held item isn't a rotatable block.
	// Bound to R key and MMB click (MMB falls through to eyedropper
	// when rotationCount == 1).
	void cyclePlacementRotation();
	// True when the currently-selected hotbar item resolves to a
	// BlockDef whose shape has rotationCount > 1. Drives the MMB
	// router (rotate vs eyedropper) and the ghost preview.
	bool isHeldBlockRotatable();
	// Return the param2 the held block should be placed with, clamped
	// modulo its rotationCount. Used by placeBlock and the ghost preview.
	uint8_t placementParam2ForHeld(const civcraft::BlockDef& def) const;
	// BlockDef for the currently-held hotbar item, or null if empty or
	// the held item isn't a registered block. Private hotbar+registry
	// lookup shared by the rotation helpers.
	const civcraft::BlockDef* heldBlockDef();
	// RPG / RTS click-to-move: raycast through cursor NDC → ground cell →
	// install a client-side move order that the virtual-joystick tick drives
	// toward. Uses the screen cursor in top-down modes (not the camera forward).
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
	// Snapshot the live 18³ block neighborhood around `cp` on the main thread
	// (under ChunkSource's invariants) and enqueue an async mesh build. The
	// worker pool runs ChunkMesher::buildMeshFromSnapshot; the result is picked
	// up by drainAsyncMeshes() and applied via createChunkMesh/updateChunkMesh.
	void enqueueMeshBuild(civcraft::ChunkPos cp);
	// Drain worker results and upload verts to the RHI. Main-thread only.
	void drainAsyncMeshes();
	// Apply one finished mesh (create or update the GPU buffer).
	void applyMeshResult(civcraft::AsyncChunkMesher::Result&& r);
	// Synchronously rebuild the chunk containing `wpos` on the main thread
	// and upload the fresh mesh. Called by predictBlockBreak/Place paths so
	// the player's optimistic edit becomes visible on the same frame as
	// the click instead of waiting for the async worker. Also flags any
	// in-flight async job on that chunk as stale so its pre-predict result
	// doesn't overwrite the fresh mesh when it lands.
	void syncRemeshBlock(glm::ivec3 wpos);
	// Pre-load chunks before the loading screen hands off to gameplay. Fills
	// the entire render radius + vertical range so the first rendered frame
	// already has a full horizon instead of popping in.
	void preloadVisibleChunks();

	// Gather loading signals (welcome / world-prepare / chunk stream+quiesce /
	// agent init) and push them into m_loading. Called once per frame on the
	// Connecting menu screen; the renderer reads m_loading directly and the
	// main loop calls enterPlaying() once m_loading.ready() plus a dismiss
	// click/key edge fires.
	void updateLoadingGate(float dt);

	// Helpers broken out of updateLoadingGate — each owns one chunk-stream
	// concern and is only meant to be called from the loading-gate path.
	void  pumpChunkStream();                 // 8ms net + mesher burst
	float computeChunkStreamFrac();          // peak-pinned meshed / total
	bool  updateChunkQuiesce(float dt);      // true once growth has stalled

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
	// In-process menu plaza — 3 mascots wandering on a tiny client-only
	// world. Ticked while m_state == Menu; idle once the user enters the
	// real game. See client/menu_plaza.h for the Rule 3 exception note.
	std::unique_ptr<MenuPlaza>               m_menuPlaza;
	// Populated from CLI flags before the AgentClient is constructed — see
	// client/main.cpp `--decide-base-cooldown` / `--decide-max-cooldown` /
	// `--decide-backoff-base`. Stays at struct defaults when the flags are
	// absent so release builds behave identically to before the knob landed.
	civcraft::AgentClient::Config m_agentCfg;

	// Menu
	float        m_menuTitleT = 0.0f;
	std::string  m_lastDeathReason;
	MenuScreen   m_menuScreen = MenuScreen::Main;
	// Listens on UDP 7778 for civcraft-server LAN broadcasts. Ticked every
	// frame while in the Menu state; populates the Multiplayer screen list.
	civcraft::LanBrowser m_lanBrowser;

	// World to request on the pending C_HELLO handshake. main.cpp fills these
	// before init(); CharacterSelect passes them into createGame().
	int          m_pendingSeed      = 42;
	int          m_pendingTemplate  = 1;
	std::string  m_connectError;        // last handshake error, displayed on CharacterSelect
	bool         m_connecting = false;  // true while awaiting S_WELCOME
	float        m_connectStartTime = 0.0f; // m_wallTime when beginConnect fired; timeout at +60s
	// Connecting → Playing handoff controller (gate + smoothing + sticky
	// ready + any-key dismiss). Fed per-frame by updateLoadingGate().
	LoadingScreen m_loading;
	// Chunk-quiesce tracker — watches m_chunkMeshes.size() during the loading
	// phase. When it stops growing for m_chunkQuiesceWindow AND there's nothing
	// pending, ChunksLoaded hits 1.0.
	float        m_chunkQuiesceAccum   = 0.0f;
	size_t       m_chunkMeshesLastSeen = 0;
	// Peak streaming fraction (meshed / (meshed+pending)) seen so far. Pinned
	// at peak so the bar never regresses when new dirty chunks arrive late.
	float        m_chunkStreamPeak     = 0.0f;
	// Shared chrome for immersive preview screens (CharacterSelect, Handbook).
	// Holds the previewed artifact id, clip, cover-toggle state, and layout
	// constants. Camera pin (game_vk.cpp) and world injection
	// (game_vk_renderer_world.cpp) both read `m_shell.previewId` — the owning
	// screen mutates it each frame based on cursor selection.
	ScreenShell  m_shell;

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
	// Chunks currently queued on the async mesher worker pool. Keeps Pass 1/
	// Pass 2 from double-enqueuing and lets dirty events wait for the in-flight
	// build to land before re-queueing.
	std::unordered_set<civcraft::ChunkPos, civcraft::ChunkPosHash> m_inFlightMesh;
	// cp flagged here has a newer sync-built mesh than the in-flight worker
	// job; drainAsyncMeshes drops the stale result instead of overwriting.
	std::unordered_set<civcraft::ChunkPos, civcraft::ChunkPosHash> m_staleInflightMeshes;
	// Worker pool lives as long as Game. Constructed lazily once the server
	// handshake is done and the BlockRegistry is known. Destroyed in
	// shutdown() before the server reference goes away.
	std::unique_ptr<civcraft::AsyncChunkMesher> m_asyncMesher;
	// Per-frame scratch so we don't realloc the 13-float interleaved buffer.
	std::vector<float> m_meshUploadScratch;
	std::vector<FloatText> m_floaters;

	// Client-only physics/animation state (not on Entity).
	bool         m_onGround  = false;
	float        m_walkDist  = 0.0f;  // animation swing phase
	// applySeparation LPF state for the local player. Zero on first call,
	// carried across frames; cleared by applySeparation when player is idle.
	glm::vec2    m_playerSepDvPrev{0.0f, 0.0f};
	float        m_attackCD  = 0.0f;  // cooldown until next swing
	float        m_placeCD   = 0.0f;  // cooldown until next RMB block place
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

	// Block-break burst (Minecraft-style). Two concepts share one struct so
	// they're cleaned up together:
	//   • Black crack flash — 18 fracture marks × 6 faces, stationary at the
	//     block's ghost cube, fades in first kCrackDuration. Reads as "the
	//     block cracked before shattering" — what the user actually sees as
	//     "the break animation", since the block itself is already gone.
	//   • Flying debris — ~14 cubes in block color, gravity + drag, lifetime
	//     kDuration. Reads as "chunks flying off".
	// Per-debris state needs memory frame-to-frame (gravity integration);
	// the crack flash is procedural (computed each frame from origin).
	struct BreakBurst {
		struct Part {
			glm::vec3 pos{};
			glm::vec3 vel{};
			float     size = 0.06f;
		};
		std::vector<Part> parts;
		glm::vec3 origin{};    // block corner (float-cast ivec3), for crack-face math
		glm::vec3 color{1};
		float t = 0.0f;
		static constexpr float kDuration      = 0.7f;
		static constexpr float kCrackDuration = 0.35f;  // crack flash ends halfway
	};
	std::vector<BreakBurst> m_breakBursts;
	void spawnBreakBurst(glm::vec3 center, glm::vec3 color);

	// Dropped-item pickup.
	//
	// Two-phase model:
	//   1. A PickupRequest is created when updatePickups() sends a Relocate.
	//      Its presence in m_pickupRequests acts as a per-entity cooldown:
	//      we do not re-send Relocate for the same item for 5s. No anim
	//      plays yet.
	//   2. When the server accepts (observed as the item entity disappearing
	//      from m_server), the request converts to a PickupAnim that plays
	//      the fly-to-player arc. When the server rejects (no disappearance
	//      within kPickupWait), we emit ONE "Pickup denied" floater and
	//      keep the request around until kPickupCooldown elapses, blocking
	//      retries.
	// Duration of the arc comes from the picker's EntityDef::pickup_fly_duration.
	struct PickupAnim {
		civcraft::EntityId itemId = 0;
		glm::vec3 startPos{0};
		glm::vec3 color{1};
		std::string itemType;
		int count = 1;
		float t = 0.0f;
		float duration = 0.0f;
	};
	std::vector<PickupAnim> m_pickupAnims;

	struct PickupRequest {
		std::string itemType;
		int         count = 1;
		glm::vec3   startPos{0};
		glm::vec3   color{1};
		float       age = 0.0f;       // seconds since Relocate sent
		bool        deniedShown = false;
	};
	std::unordered_map<civcraft::EntityId, PickupRequest> m_pickupRequests;
	// Tuning. kPickupWait = grace period before we assume the server rejected.
	// kPickupCooldown = total time an entry lingers, blocking re-requests.
	static constexpr float kPickupWait     = 0.5f;
	static constexpr float kPickupCooldown = 5.0f;

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
	// FPS/TPS RMB-over-interactive: press latches the target; release fires
	// the Interact once. Without this, the held-place synthesis spams a
	// fresh Interact every frame and the door open/closes at frame rate.
	bool         m_rmbInteractPending    = false;
	glm::ivec3   m_rmbInteractTarget     = {0, 0, 0};
	bool         m_mmbLast       = false;
	bool         m_spaceLast     = false;
	bool         m_escLast       = false;
	bool         m_vLast         = false;
	bool         m_eLast         = false;
	bool         m_f3Last        = false;
	bool         m_f6Last        = false;
	bool         m_f12Last       = false;
	bool         m_f11Last       = false;
	bool         m_hLast         = false;   // H: handbook
	bool         m_tKeyLast      = false;   // T: talk to NPC
	bool         m_rKeyLast      = false;   // R: rotate held block

	// Orientation the next placed block gets. Cycled by R key or MMB
	// click (MMB click on a non-rotatable item keeps its eyedropper
	// behavior). Reset to 0 when the hotbar slot changes.
	uint8_t      m_placementParam2 = 0;
	int          m_placementHotbarSlot = -1;

	// RPG/RTS right-click orbit: hold+drag to orbit camera, quick click = action.
	// wantCapture is set only while actively orbiting.
	struct RightClickState {
		bool   held     = false;
		bool   orbiting = false;
		bool   action   = false;   // set on quick-release → triggers dig/place
		double startX   = 0;
		double startY   = 0;
	} m_rightClick;

	// UI overlay wants cursor free (handbook, etc.)
	bool         m_uiWantsCursor = false;

	// F3 debug overlay. Env-var init lets headless screenshots boot with F3 on.
	bool         m_showDebug = []{
		const char* v = std::getenv("CIVCRAFT_DEBUG_F3");
		return v && std::atoi(v) != 0;
	}();

	// Per-frame section timings — feeds CIVCRAFT_PERF histograms and the
	// [perf-spike] console line when a frame busts spikeMs.
	struct FrameProbe {
		using clock = std::chrono::steady_clock;
		clock::time_point frameStart;
		clock::time_point last;
		std::vector<std::pair<const char*, double>> sections;
		const double spikeMs = 40.0;
		void begin() { frameStart = clock::now(); last = frameStart; sections.clear(); }
		void mark(const char* name) {
			auto now = clock::now();
			double ms = std::chrono::duration<double, std::milli>(now - last).count();
			sections.push_back({name, ms});
			last = now;
		}
	} m_frameProbe;

	// F6 render-tuning panel — drives the composite GradingParams UBO.
	bool                    m_showTuning = false;  // toggled by F6
	rhi::IRhi::GradingParams m_grading = rhi::IRhi::GradingParams::Vivid();

	// Rolling FPS counter. Ticked each frame by runOneFrame; exposed to HUD
	// + F3 debug overlay. Recomputed once per second so the displayed number
	// doesn't flicker.
	float m_fpsDisplay   = 0.0f;
	float m_fpsWindowS   = 0.0f;
	int   m_fpsWindowFrames = 0;

	// Perf-session anchor: wall-time (seconds since epoch) of the first frame
	// spent in Playing state. Zero until then, which also makes the exit
	// summary a no-op if the user quits from the main menu. Only read in
	// CIVCRAFT_PERF builds but kept unconditional to avoid ABI divergence.
	double m_perfSessionStart = 0.0;
public:
	double perfSessionStart() const { return m_perfSessionStart; }
private:

	// H handbook panel
	bool         m_handbookOpen = false;
	HandbookPanel m_handbook;
	civcraft::ArtifactRegistry m_artifactRegistry;

	// ── Hotbar + Inventory UI ────────────────────────────────────────────
	// Hotbar: 10-slot alias over the player's inventory. The selected slot's
	// itemId becomes the main-hand model. Drag layout persists to
	// m_hotbarSavePath; `mergeFrom` runs on every S_INVENTORY so drag edits
	// survive pickups.
	civcraft::Hotbar m_hotbar;
	std::string      m_hotbarSavePath;  // set after login; empty before.
	bool             m_hotbarSeeded = false;  // false until first S_INVENTORY.

	// Tab opens the inventory panel. When m_invOther != 0, renders a second
	// pane for that entity (chest/NPC). Sort mode cycles via a UI button.
	bool             m_invOpen = false;
	bool             m_tabLast = false;
	bool             m_qLast   = false;
	// Hold-Q auto-repeat. Tap = 1 drop; hold past kQInitialDelay starts
	// auto-repeating at kQRepeatInterval so Q can empty a stack.
	float            m_qHoldTime   = 0.0f;  // seconds held this press
	float            m_qRepeatAccum = 0.0f; // seconds since last auto-drop
	civcraft::EntityId m_invOther = 0;
	InvSort          m_invSort = InvSort::ByName;

	// Cached slot rects — 2D pass records, 3D pass (next frame) reads. The
	// one-frame lag is invisible (UI doesn't move in a single frame) and
	// lets the 3D pass precede the 2D UI pass without reordering code.
	std::vector<SlotRect> m_slotRectsLast;
	std::vector<SlotRect> m_slotRectsThis;

	// Cursor + LMB tracking for custom hit-testing. NDC y grows upward (matches
	// rhi.drawRect2D). processInput refreshes these every frame.
	float        m_mouseNdcX = 0.0f;
	float        m_mouseNdcY = 0.0f;
	bool         m_mouseLHeld     = false;
	bool         m_mouseLPressed  = false;   // edge: not-held → held this frame
	bool         m_mouseLReleased = false;   // edge: held → not-held this frame
	bool         m_mouseLLast     = false;

	// Which slot in m_slotRectsLast the cursor is over (-1 = none). Updated
	// per-frame by the inventory-UI hit-tester before drawing.
	int          m_hoverSlot = -1;

	// Active drag-and-drop (inventory↔hotbar + drop-to-world).
	DragState    m_drag;

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
	// Unified path executor — drives NPC follow-to-slot for RTS commands and
	// possessed-player click-to-move through the same pop-front cell queue.
	civcraft::PathExecutor                       m_pathExec;
	std::unique_ptr<civcraft::ChunkWorldView>    m_pathWorldView;
	struct MoveOrder { glm::vec3 target; bool active; };
	std::unordered_map<civcraft::EntityId, MoveOrder> m_moveOrders;

	// RTS drag-command: in RTS mode with a non-empty selection, hold RMB and
	// drag on the ground to define a circle; on release, a 4-slice wheel
	// (Gather/Attack/Mine/Cancel) opens at the cursor. A non-Cancel slice
	// preempts AI with a manual order; Cancel returns units to AI control.
	// Slice indices — also the display order (N/E/S/W):
	//   0 = Gather (top), 1 = Attack (right), 2 = Mine (bottom), 3 = Cancel (left)
	struct RTSDragCmd {
		bool      active        = false;
		glm::vec2 startNdc{0, 0};
		glm::vec2 currentNdc{0, 0};
		glm::vec3 startWorld{0, 0, 0};
		glm::vec3 currentWorld{0, 0, 0};
		float     radiusWorld   = 0.0f;
		bool      hasStartWorld = false;
	} m_rtsDragCmd;
	struct RTSWheel {
		bool      active            = false;
		glm::vec2 centerNdc{0, 0};
		glm::vec3 circleCenterWorld{0, 0, 0};
		float     circleRadiusWorld = 0.0f;
		int       hoverSlice        = -1;
		// Shift-held this frame → slice commit will queue instead of replace.
		// Tracked for display so the wheel can badge "+GATHER" etc.
		bool      shiftQueue        = false;
	} m_rtsWheel;

	// Screen
	int          m_fbW = 0, m_fbH = 0;
	float        m_aspect = 1.0f;

	// Wall-clock time (for sky/particle animation phases that don't
	// want to pause with the game).
	float        m_wallTime = 0.0f;
	// Frame delta (seconds) — set from runOneFrame so renderers can drive
	// dt-based lerps (EMA smoothing, etc.) without threading dt through.
	float        m_frameDt  = 0.0f;

	// Per-entity anim-smoothing trails. Raw e.velocity / e.yaw step when
	// decide() re-aims; walk/run clip selection and body yaw would snap.
	// EMA this input before handing to appendBoxModel. Cleaned up by the
	// remote-entity loop in WorldRenderer::renderWorld.
	struct EntityAnimSmooth {
		float speed       = 0.0f;     // smoothed horizontal speed (blocks/s)
		float bodyYawDeg  = 0.0f;     // smoothed body yaw (degrees)
		bool  initialized = false;
	};
	std::unordered_map<civcraft::EntityId, EntityAnimSmooth> m_entityAnimSmooth;
	// Player uses the existing m_playerBodyYaw lerp for yaw; only speed needs
	// its own trail here.
	float        m_playerAnimSpeed     = 0.0f;
	bool         m_playerAnimSpeedInit = false;

	// Camera — uses the shared Camera class (FPS/TPS/RPG/RTS modes).
	civcraft::Camera m_cam;

	// Admin mode (F12 toggle, F11 fly in admin)
	bool         m_adminMode = false;
	bool         m_flyMode   = false;

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

	// ── Coord peek (click a (x,y,z) link in Inspect to fly the camera there) ─
	// Single-slot save: entering stacks the current m_cam onto m_peekSavedCam
	// and retargets it at m_peekTarget. ESC pops (restores + clears marker).
	// Nested peeks are not supported — re-clicking just re-aims.
	bool                          m_peekActive = false;
	glm::ivec3                    m_peekTarget{0};
	std::unique_ptr<civcraft::Camera> m_peekSavedCam;

	void enterCoordPeek(glm::ivec3 target);
	void exitCoordPeek();

	// ── NPC dialog (local LLM) ───────────────────────────────────────────
	// Lazily initialised on the first T-key press (so the sidecar health
	// probe doesn't block boot). The panel + session are client-only —
	// server has no idea dialog is happening (Rule 5).
	std::unique_ptr<civcraft::llm::LlmClient>     m_llmClient;
	std::unique_ptr<civcraft::llm::WhisperClient> m_whisperClient; // STT over HTTP to whisper-server
	std::unique_ptr<civcraft::AudioCapture>       m_audioCapture;  // mic, s16 mono 16 kHz
	std::unique_ptr<civcraft::llm::TtsVoiceMux>   m_ttsMux;        // lazily spawns 1 piper per distinct voice
	std::unique_ptr<civcraft::llm::LlmSidecar>    m_llmSidecar;    // child llama-server, spawned in init()
	std::unique_ptr<civcraft::llm::WhisperSidecar> m_whisperSidecar; // child whisper-server (STT), port 8081
	DialogPanel                                   m_dialogPanel;
	std::vector<uint32_t>                         m_charQueue;     // drained each input tick
	std::vector<int>                              m_keyQueue;      // GLFW key codes (press only)

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
		std::vector<float> doorVerts;
		std::vector<civcraft::RaycastEntity> ents;
	} m_scratch;
};

} // namespace civcraft::vk
