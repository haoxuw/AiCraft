#pragma once

// Unified client-side path executor. Replaces both agent/pathfind.h's
// per-entity PathExecutor and the older client/rts_executor.h's FlowField-
// backed RtsExecutor.
//
// Design:
//   * ONE class handles both single-entity navigation (NPC behaviors via
//     Navigator, player click-to-move for a possessed unit) and multi-entity
//     RTS group commands. Each tracked entity gets its own pop-front Path
//     queue; group commands are sugar around GridPlanner::planBatch + N
//     setPath() calls.
//   * Pop-front arrival with strict in-cell containment: the entity must
//     actually enter each waypoint's XZ cell to retire it — no arrive ring,
//     no segment projection. A scan-forward per tick pops every waypoint up
//     through the deepest one the entity is currently inside, which handles
//     both normal single-cell advance AND multi-cell overshoot (teleport,
//     collision push-out, clientPos snap-back) in one pass.
//   * Doors are first-class. A DoorOracle installed at construction is
//     consulted for every tracked entity, so RTS units don't treat closed
//     doors as walls.
//   * Async planning for groups: detached std::thread runs planBatch; poll()
//     swaps paths in when the worker reports back. Single-entity callers
//     plan synchronously via Navigator.
//
// Rule 0: emits only Move / Interact ActionProposals.
// Rule 4: intelligence runs on the agent-client side; this class is it.

#include "agent/pathfind.h"
#include "logic/action.h"
#include "logic/block_registry.h"
#include "logic/chunk_source.h"
#include "logic/entity.h"
#include "net/server_interface.h"

#include <glm/glm.hpp>

#include <chrono>
#include <climits>
#include <cmath>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace civcraft {

// Unified WorldView — works on both sides (server via World, client via
// LocalWorld), since both implement ChunkSource. Doors/trapdoors are treated
// as walkable at plan time; DoorOracle handles the Interact handshake on
// closed doors, trapdoors walk through as-is.
// Unloaded chunks report !isSolid (air), matching GridPlanner's expectation.
struct ChunkWorldView : public WorldView {
	ChunkSource&         chunks;
	const BlockRegistry& blocks;
	ChunkWorldView(ChunkSource& c, const BlockRegistry& b) : chunks(c), blocks(b) {}
	bool isSolid(glm::ivec3 p) const override {
		const BlockDef& def = blocks.get(chunks.getBlock(p.x, p.y, p.z));
		if (!def.solid) return false;
		if (def.mesh_type == MeshType::Door)     return false;
		if (def.mesh_type == MeshType::DoorOpen) return false;
		if (def.mesh_type == MeshType::Trapdoor) return false;
		return true;
	}
};

// Door oracle — identifies a door cell by mesh_type.
struct ChunkDoorOracle : public DoorOracle {
	ChunkSource&         chunks;
	const BlockRegistry& blocks;
	ChunkDoorOracle(ChunkSource& c, const BlockRegistry& b) : chunks(c), blocks(b) {}
	bool isClosedDoor(glm::ivec3 p) const override {
		return blocks.get(chunks.getBlock(p.x, p.y, p.z)).mesh_type == MeshType::Door;
	}
	bool isOpenDoor(glm::ivec3 p) const override {
		return blocks.get(chunks.getBlock(p.x, p.y, p.z)).mesh_type == MeshType::DoorOpen;
	}
};

// Rotate `cur` toward `des` in XZ by at most `maxRad` radians. Used to
// cap per-tick velocity-heading change so waypoint pops don't snap the
// direction. Zero `cur` snaps to `des` (first tick). Returns unit XZ.
inline glm::vec3 rotateTowardXZ(glm::vec3 cur, glm::vec3 des, float maxRad) {
	cur.y = 0; des.y = 0;
	float cl2 = cur.x * cur.x + cur.z * cur.z;
	float dl2 = des.x * des.x + des.z * des.z;
	if (dl2 < 1e-8f) return {0, 0, 0};            // no desired heading
	glm::vec3 d = des / std::sqrt(dl2);
	if (cl2 < 1e-8f) return d;                     // first tick — snap
	glm::vec3 c = cur / std::sqrt(cl2);
	float dot  = c.x * d.x + c.z * d.z;
	if (dot >  1.0f) dot =  1.0f;
	if (dot < -1.0f) dot = -1.0f;
	float ang = std::acos(dot);
	if (ang <= maxRad) return d;                   // target within cap
	float crossY = c.x * d.z - c.z * d.x;           // rotation sign
	float sign   = (crossY >= 0.0f) ? 1.0f : -1.0f;
	float sn     = std::sin(maxRad) * sign;
	float cs     = std::cos(maxRad);
	return { c.x * cs - c.z * sn, 0, c.x * sn + c.z * cs };
}

// Walk: cancel if unreachable. Build (long-press): enter experimental builder
// mode (Phase 1 jump-climb; Phase 2+ stack blocks to bridge gaps).
enum class CommandKind { Walk, Build };

class PathExecutor {
public:
	// Waypoint pop: entity must reach within kMagnetRadius of cell center.
	// Tight radius + turn-rate-capped velocity keep the entity on the
	// planned path instead of cutting corners. kArriveRadius is retained
	// only for formation-slot arrival (steerTargetFor), not waypoint pops.
	static constexpr float kMagnetRadius = 0.15f;  // waypoint pop
	static constexpr float kArriveRadius = 0.9f;   // slot arrival only
	static constexpr float kArriveY      = 1.0f;   // waypoint Y tolerance
	static constexpr float kDoorReachXZ  = 2.0f;

	// Per-tick XZ direction-change cap. ~10°/tick @ 60 Hz → 90° corner
	// takes ~9 ticks, keeping dv/dt smooth across pops.
	static constexpr float kMaxTurnRate      = 10.0f;
	static constexpr float kMaxTurnPerTick60 = 0.17f;

	// Wall-slide: deflect toward the best standable cardinal neighbour when
	// the desired XZ direction would clip a solid cell. Hysteresis keeps
	// the chosen side sticky across ticks (no flicker).
	static constexpr float kSlideRadius     = 0.30f;   // entity AABB half-width
	static constexpr float kSlideLookahead  = 0.15f;   // begin slide before contact
	static constexpr float kSlideHysteresis = 0.30f;   // sticky-side score bonus

	// Stall pop — fires when movement drops below kStallRadius for
	// kStallTicks in a row (boxed-in / dynamic blockage).
	static constexpr float kStallRadius = 0.10f;
	static constexpr int   kStallTicks  = 30;     // ~0.5 s at 60 Hz
	// Probe the forward cell for a closed door before the 30-tick pop
	// eats the waypoint. Collinear-Walk compression drops doors on
	// straight corridors, so the front-waypoint check can't see them.
	static constexpr int   kDoorScanTicks   = 8;   // ~0.13 s at 60 Hz
	// BlockChange broadcast lags ~1 tick; an un-gated loop would re-
	// Interact the door we just opened and toggle it closed again.
	static constexpr int   kInteractCooldownTicks = 15;  // ~0.25 s at 60 Hz

	struct Intent {
		enum Kind { None, Move, Interact } kind = None;
		glm::vec3  target      = {0, 0, 0};
		glm::ivec3 interactPos = {0, 0, 0};
	};

	// Disable to make Build fall back to Walk. Phase 1 = jump-climb only.
	static constexpr bool kExperimentalPathBuilder = true;
	struct BuilderConfig {
		int   maxClimbBlocks   = 8;
		int   maxPlacedBlocks  = 50;   // Phase 2+
		float maxDurationSec   = 60.f; // Phase 2+
		int   stuckTicksThresh = 6;
	} m_builderCfg;

	PathExecutor() = default;
	explicit PathExecutor(const DoorOracle* doors) : m_doors(doors) {}

	void setDoorOracle(const DoorOracle* doors) { m_doors = doors; }
	// Optional — enables wall-slide in tick(). Null = slide is a no-op, which
	// matches test harnesses that plan over an AirWorld.
	void setWorldView(const WorldView* world) { m_world = world; }

	// ── Per-entity primitive ──────────────────────────────────────────
	// Install a Path for one entity. Overwrites any existing path. Clearing
	// the DoorOracle wait-flag is automatic — a replan starts fresh.
	void setPath(EntityId eid, Path p);
	// Consume the front of the path, return the next Intent. Move.target is
	// the front waypoint's cell center; Interact fires when the front is a
	// closed-door cell within kDoorReachXZ.
	Intent tick(EntityId eid, const glm::vec3& entityPos);
	bool   done(EntityId eid) const;
	bool   has(EntityId eid)  const { return m_units.count(eid) > 0; }
	void   cancel(EntityId eid);
	// Remaining (unconsumed) waypoints — F3 viz + tests read this.
	const Path& path(EntityId eid) const;

	// ── Group commands (RTS) ──────────────────────────────────────────
	// Async planBatch on a detached thread; poll() swaps results in. N=1 is
	// a legal group (single unit), just plans on the worker instead of inline.
	void planGroup(const std::vector<EntityId>& entityIds,
	               const std::vector<glm::ivec3>& starts,
	               glm::ivec3 goal,
	               ChunkSource& chunks, const BlockRegistry& blocks,
	               CommandKind kind = CommandKind::Walk);
	void poll();
	// Convenience: returns tick(eid, pos).target if kind is Move, else nullopt.
	// Used by player-possessed-unit steering in game_vk_playing.cpp.
	std::optional<glm::vec3> steerTargetFor(EntityId eid, const glm::vec3& pos);
	// Emit Move ActionProposals for every tracked entity except excludedId
	// (the possessed player, which is driven by input directly). Builder mode
	// injects jumps to climb 1-block ledges.
	void driveRemote(ServerInterface& server, EntityId excludedId,
	                 float walkSpeedFallback = 2.0f,
	                 float jumpVelocity      = 8.3f);
	void cancelAll();
	bool isPlanning() const { return (bool)m_pending; }

	// ── Viz / Diagnostics ─────────────────────────────────────────────
	std::optional<glm::ivec3> formationSlot(EntityId eid) const;
	CommandKind currentKind()   const { return m_kind; }
	bool        inBuilderMode() const { return m_builderMode; }

	// Count of tracked entities (for "any RTS order active?" checks in viz).
	size_t size() const { return m_units.size(); }

private:
	struct BuilderUnitState {
		glm::ivec3 lastCell{INT_MIN, INT_MIN, INT_MIN};
		int        stuckTicks   = 0;
		bool       phase2Logged = false;
	};

	struct Unit {
		Path              path;
		bool              waitOpen = false;   // door handshake
		// Group-command fields. Slot is set by planGroup so formation viz can
		// read it; arrived gates steerTargetFor so the player-side loop knows
		// to stop issuing Moves.
		glm::ivec3        formationSlot{INT_MIN, INT_MIN, INT_MIN};
		bool              arrived = false;
		BuilderUnitState  builder;
		// Stall detector — wall-slide pops front if the entity can't make
		// progress on the current waypoint.
		glm::vec3         stallLastPos{0, 0, 0};
		int               stallTicks = 0;
		// Wall-slide hysteresis — last cardinal offset we steered around on
		// (keyed by ±X / ±Z), so the next obstructed tick prefers the same
		// side. {0,0,0} = no active slide.
		glm::ivec3        slideLockDir{0, 0, 0};
		// Smoothed XZ heading last emitted — fed back into rotateTowardXZ
		// next tick so desiredVel stays C¹-continuous across pops.
		glm::vec3         lastMoveDir{0, 0, 0};
		int               interactCooldown = 0;
	};

	struct PendingPlan {
		std::future<std::vector<Path>>        future;
		std::vector<EntityId>                 eids;
		std::vector<glm::ivec3>               starts;
		std::vector<glm::ivec3>               slots;
		glm::ivec3                            goal{};
		std::chrono::steady_clock::time_point t0;
		double                                setupMs = 0.0;
		CommandKind                           kind = CommandKind::Walk;
	};

	std::unordered_map<EntityId, Unit> m_units;
	const DoorOracle*                  m_doors       = nullptr;
	const WorldView*                   m_world       = nullptr;
	std::shared_ptr<PendingPlan>       m_pending;
	CommandKind                        m_kind        = CommandKind::Walk;
	bool                               m_builderMode = false;

	static glm::vec3 centerOf(const Waypoint& w) {
		return {w.pos.x + 0.5f, (float)w.pos.y, w.pos.z + 0.5f};
	}
	bool reached(const Waypoint& w, const glm::vec3& pos) const;
	// Deflect `target` toward the best standable cardinal neighbour when the
	// straight-line approach would clip a wall. No-op when m_world is unset.
	glm::vec3 slideAroundObstacle(const glm::vec3& pos, glm::vec3 target,
	                              Unit& u) const;
	bool builderTickFor(EntityId eid, const glm::vec3& pos,
	                    const glm::vec3& dirXZ, const WorldView& view);
	static std::vector<glm::ivec3> buildFormationGoals(
		glm::ivec3 clicked, int n, const WorldView& view);
};

// ─── Navigator ─────────────────────────────────────────────────────────────
// Per-entity facade. One Navigator owns its own PathExecutor and wraps the
// pair {planner, executor} behind setGoal / tick. Python NPC behaviors are
// written against this — it must stay API-stable.
class Navigator {
public:
	// Failed: plan once, no valid path found. Covers both "planner returned
	// empty" (no route exists at all) and "executor finished but the planned
	// path was partial" (goal unreachable — budget exhausted, or the goal
	// is inside a solid block and the best-seen standable neighbor is short
	// of it). `failureReason()` carries a coord-bearing string the behavior
	// forwards to its goal text; the behavior then idles / goes home to
	// complain instead of replanning forever.
	enum class Status : uint8_t { Idle, Planning, Walking, OpeningDoor, Arrived, Failed };

	struct Step {
		enum Kind { None, Move, Interact } kind = None;
		glm::vec3  moveTarget  = {0, 0, 0};     // world-space cell center
		glm::ivec3 interactPos = {0, 0, 0};     // door cell to toggle
	};

	Navigator(const WorldView& world, const DoorOracle* doors = nullptr);

	// Start (or re-plan) to the block at `goal`. Re-planning only fires if the
	// goal cell differs from the active plan's goal — cheap call-every-tick.
	// Returns false if planner gave up (partial or empty path).
	bool setGoal(glm::ivec3 goal);

	// Advance one tick. Returns the step to take (Move/Interact/None). When
	// status() == Arrived or Failed, the caller should pick a new goal.
	Step tick(const glm::vec3& entityPos);

	void   clear();
	bool   hasGoal() const   { return m_hasGoal; }
	Status status() const    { return m_status; }
	glm::ivec3 goal() const  { return m_goal; }
	const std::string& failureReason() const { return m_failureReason; }

	// Diagnostics — let callers attribute nav failures to "no plan at all" vs.
	// "plan was partial (walking to nearest standable)". pathStepCount is the
	// *original* plan size (set at plan time, doesn't shrink) so the "steps=0
	// after Failed" diagnostic still distinguishes "A* returned empty" from
	// "walked the whole plan and arrived short."
	size_t pathStepCount() const { return m_path.steps.size(); }
	bool   pathPartial()   const { return m_path.partial; }
	// path() returns the *remaining* waypoints — the ones the executor hasn't
	// consumed yet. This is what F3 viz mirrors every tick so the drawn
	// polyline shrinks cell-by-cell, matching what the entity is walking.
	const Path& path() const { return m_exec.path(kSelfEid); }

	// Replan trigger — drop the cached plan on block changes in the corridor.
	bool invalidatedBy(glm::ivec3 changedBlock) const {
		return m_hasGoal && m_planner.pathInvalidatedBy(m_path, changedBlock);
	}

private:
	// Navigator owns an exclusive PathExecutor with a single tracked entity
	// under this sentinel id. The real entity id is unknown at this layer
	// (behavior plumbing varies); since the executor is private, the
	// sentinel is opaque.
	static constexpr EntityId kSelfEid = 1;

	const WorldView&  m_world;
	const DoorOracle* m_doors;
	GridPlanner       m_planner;
	PathExecutor      m_exec;
	Path              m_path;
	glm::ivec3        m_goal{0};
	bool              m_hasGoal = false;
	Status            m_status  = Status::Idle;
	std::string       m_failureReason;
};

inline const char* toString(Navigator::Status s) {
	switch (s) {
		case Navigator::Status::Idle:        return "Idle";
		case Navigator::Status::Planning:    return "Planning";
		case Navigator::Status::Walking:     return "Walking";
		case Navigator::Status::OpeningDoor: return "OpeningDoor";
		case Navigator::Status::Arrived:     return "Arrived";
		case Navigator::Status::Failed:      return "Failed";
	}
	return "?";
}

}  // namespace civcraft
