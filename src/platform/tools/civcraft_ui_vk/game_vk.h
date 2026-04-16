#pragma once

// Vulkan-native playable slice — built on top of the RHI.
//
// The goal is NOT to mirror the full civcraft-ui game (with chunked TCP
// world, Python agents, inventory, etc.). It's to demonstrate a complete
// gameplay loop — menu → walk → combat → HUD → death → respawn — using the
// same RHI primitives both backends share. That makes the Vulkan backend
// the living reference for a "game-complete" RHI surface, separate from the
// enormous lift of porting CivCraft's renderer.cpp (Phase 3).
//
// Systems here (all talk to civcraft::rhi::IRhi):
//   * GameState machine — MENU → PLAYING → DEAD → MENU.
//   * World — procedural voxel terrain; height query for player collision.
//   * NPC — position, HP, goal state (Wandering / Chasing / Fleeing);
//           emits a "!" lightbulb + goal label + HP bar above its head.
//   * Player — third-person character, WASD + mouse-yaw, HP, HP regen.
//   * Combat — left-click fires a sword slash ribbon toward the nearest NPC
//              in a front cone; damage → floating "-N" number; NPC death →
//              gold coin pickup (adds to hotbar slot 0).
//   * HUD — main menu (ImGui), hotbar (10 slots), HP bar, FPS counter.
//   * Particles — reused torch flames + fireflies + dust from the prior demo.

#include "client/rhi/rhi.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

struct GLFWwindow;

namespace civcraft::vk {

// ── Tuning ────────────────────────────────────────────────────────────────
// Keep tuning numbers at the top of the header so all systems can see the
// same values. Follows the "Rule 1 / no-hardcoded-magic" spirit of the GL
// game — if this were the real CivCraft we'd load these from Python.
struct Tuning {
	// World
	int   worldRadius   = 40;     // half-extent of the flat-ish "village"
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

	// NPC
	int   npcCount      = 12;
	int   npcMaxHP      = 40;
	float npcSpeed      = 2.4f;
	float npcAggroRange = 8.0f;
	float npcFleeHpFrac = 0.35f;  // flees below 35% hp
	float npcTouchDmg   = 8;      // damage/s while the player is in melee
	float npcTouchRange = 1.4f;
};

extern Tuning kTune;

// ── World ─────────────────────────────────────────────────────────────────
// Emits the voxel instance stream for drawVoxels plus a simple terrainTop()
// query for player/NPC foot collision. Built once; colors are per-instance
// so the renderer stays a single pipeline.
class World {
public:
	void generate(int seed = 42);
	const float* instances() const { return m_instances.data(); }
	uint32_t     instanceCount() const { return m_count; }

	// Returns the highest solid-block top Y at world XZ. Used to clamp player
	// and NPC feet to the ground.
	float terrainTop(float x, float z) const;

	// Remove the voxel at integer world coords. Returns true if a voxel was
	// found and removed. Updates m_heightMap so subsequent terrainTop()s
	// reflect the new surface. Caller is expected to follow up with
	// IRhi::updateVoxelMesh to push the new geometry to the GPU.
	bool digAt(int wx, int wy, int wz);

private:
	std::vector<float>        m_instances;       // 6 floats each
	std::vector<int>          m_heightMap;       // size (2R)*(2R)
	uint32_t                  m_count = 0;
	int                       m_R     = 0;       // radius; heightMap is 2R×2R
};

// ── NPC ───────────────────────────────────────────────────────────────────
// Simple AI: wander toward a roaming target; if the player gets close,
// chase + melee-touch damage; if hp drops low, flee. Goal text matches the
// state so the lightbulb indicator reads as a real creature thought.
struct Npc {
	enum class State { Wander, Chase, Flee, Dying };

	int      id = 0;
	glm::vec3 pos{};
	glm::vec3 vel{};
	float    yaw  = 0.0f;
	int      hp   = 0;
	int      maxHp = 0;
	State    state = State::Wander;
	float    stateT = 0.0f;       // seconds in current state
	glm::vec3 roamTarget{};       // wander destination
	float    hitFlash = 0.0f;     // 1.0 on damage, fades to 0
	float    deathT   = 0.0f;     // when Dying, t in [0,1]; removed at 1
	float    phase    = 0.0f;     // arm/leg swing phase
	float    hitstop  = 0.0f;     // brief freeze after landing a hit on us

	// Palette for the box-model: skin/shirt/pants.
	glm::vec3 skin{0.85f,0.68f,0.50f};
	glm::vec3 shirt{0.40f,0.30f,0.25f};
	glm::vec3 pants{0.20f,0.15f,0.12f};

	std::string goalText() const;
	glm::vec4   tint()     const;  // warm gold when healthy, red when hurt
};

// ── Player ────────────────────────────────────────────────────────────────
struct Player {
	glm::vec3 pos{};
	glm::vec3 vel{};
	float    yaw    = 0.0f;   // body facing — follows camera
	float    pitch  = -0.25f; // camera pitch only
	float    hp     = 0.0f;
	float    regenIdle = 0.0f;  // seconds since last damage
	float    walkDist  = 0.0f;  // for swing animation
	bool     onGround = false;
	float    attackCD = 0.0f;   // cooldown until next swing

	glm::vec3 forward() const;  // from body yaw (pitch stays on camera)
};

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
enum class GameState { Menu, Playing, Dead };

class Game {
public:
	bool init(rhi::IRhi* rhi, GLFWwindow* window);
	void shutdown();

	// Skip the main menu and drop straight into gameplay. Used by
	// `--skip-menu` for headless/CI flows; mirrors the GL build.
	void skipMenu() { enterPlaying(); }

	// Poll input, step sim, render one frame.
	void runOneFrame(float dt, float wallTime);

	// Check if we should quit (menu → Quit, or window close).
	bool shouldQuit() const { return m_shouldQuit; }

private:
	// ── Scene transitions ─────────────────────────────────────────────────
	void enterMenu();
	void enterPlaying();
	void enterDead(const char* cause);
	void respawn();

	// ── Sim phases (playing) ──────────────────────────────────────────────
	void processInput(float dt);
	void tickPlayer(float dt);
	void tickNpcs(float dt);
	void tickCombat(float dt);
	void tickFloaters(float dt);

	// ── Render phases ─────────────────────────────────────────────────────
	void renderWorld(float wallTime);           // sky + shadows + voxels
	void renderEntities(float wallTime);        // player + NPCs as box-models
	void renderEffects(float wallTime);         // particles + slash ribbons
	void renderHUD();                           // lightbulbs, HP bars, hotbar
	void renderMenu();                          // main menu (ImGui + RHI UI)
	void renderDeath();                         // death overlay

	// Projects a world-space anchor to NDC. Returns false if behind camera
	// or clipped. `out` receives {ndcX, ndcY, depthLinear}.
	bool projectWorld(const glm::vec3& world, glm::vec3& out) const;

	// Camera math used by both input and render.
	glm::mat4 viewProj() const;
	glm::mat4 viewMatrix() const;
	glm::vec3 cameraEye() const;

	// World mutation (right-click + headless file trigger).
	void digInFront();

	// NPC helpers
	void spawnNpcs();
	Npc* nearestNpcInCone(const glm::vec3& from, const glm::vec3& fwd,
	                       float range, float coneRad);
	void damageNpc(Npc& n, int dmg, const glm::vec3& fromDir);

	// ── State ─────────────────────────────────────────────────────────────
	rhi::IRhi*   m_rhi    = nullptr;
	GLFWwindow*  m_window = nullptr;
	GameState    m_state  = GameState::Menu;
	bool         m_shouldQuit = false;

	// Menu
	float        m_menuTitleT = 0.0f;
	std::string  m_lastDeathReason;

	World        m_world;
	// One-shot upload of the static village voxels into the RHI. Built in
	// init() and reused every frame by drawVoxelsMesh / renderShadowsMesh
	// so the per-frame loop stops re-streaming ~6k floats it never changes.
	rhi::IRhi::MeshHandle m_worldMesh = rhi::IRhi::kInvalidMesh;
	// Demonstration of the rich-vertex chunk-mesh pipeline (Phase 3 prep
	// for porting CivCraft's chunk_mesher). A small stone obelisk built
	// out of cube faces in the 13-float-per-vertex format, uploaded once.
	rhi::IRhi::MeshHandle m_chunkDemoMesh = rhi::IRhi::kInvalidMesh;
	Player       m_player;
	std::vector<Npc> m_npcs;
	std::vector<FloatText> m_floaters;

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

	// Input latches
	bool         m_mouseCaptured = false;
	bool         m_firstMouse    = true;
	double       m_lastMouseX    = 0, m_lastMouseY = 0;
	bool         m_lmbLast       = false;
	bool         m_rmbLast       = false;
	bool         m_spaceLast     = false;
	bool         m_escLast       = false;

	// Screen
	int          m_fbW = 0, m_fbH = 0;
	float        m_aspect = 1.0f;

	// Wall-clock time (for sky/particle animation phases that don't
	// want to pause with the game).
	float        m_wallTime = 0.0f;

	// Camera state (derived from player + captured mouse delta).
	float        m_camYaw   = -90.0f * 3.14159f / 180.0f;
	float        m_camPitch = -0.25f;

	// Score counter (coins from killed NPCs). Shown in hotbar slot 0.
	int          m_coins = 0;
};

} // namespace civcraft::vk
