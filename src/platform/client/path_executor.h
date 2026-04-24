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
//   * Pop-front arrival with a half-plane retire test: cells that the entity
//     has projected past (along the segment to the next cell) are popped even
//     if the entity never entered the cell's arrive ring. This covers
//     physics-overshoot, collision shove, and teleport-forward without
//     stalling on a waypoint that's now behind the entity.
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

// Unloaded chunks report !isSolid (air), matching GridPlanner's expectation.
struct ClientChunkWorldView : public WorldView {
	ChunkSource&         chunks;
	const BlockRegistry& blocks;
	ClientChunkWorldView(ChunkSource& c, const BlockRegistry& b) : chunks(c), blocks(b) {}
	bool isSolid(glm::ivec3 p) const override {
		return blocks.get(chunks.getBlock(p.x, p.y, p.z)).solid;
	}
};

// Door oracle for client-side path execution. Identifies a door cell by the
// block's mesh_type (matches server-side AgentServerDoorOracle semantics but
// reads straight off ChunkSource instead of ServerInterface).
struct ClientChunkDoorOracle : public DoorOracle {
	ChunkSource&         chunks;
	const BlockRegistry& blocks;
	ClientChunkDoorOracle(ChunkSource& c, const BlockRegistry& b) : chunks(c), blocks(b) {}
	bool isClosedDoor(glm::ivec3 p) const override {
		return blocks.get(chunks.getBlock(p.x, p.y, p.z)).mesh_type == MeshType::Door;
	}
	bool isOpenDoor(glm::ivec3 p) const override {
		return blocks.get(chunks.getBlock(p.x, p.y, p.z)).mesh_type == MeshType::DoorOpen;
	}
};

// Walk: cancel if unreachable. Build (long-press): enter experimental builder
// mode (Phase 1 jump-climb; Phase 2+ stack blocks to bridge gaps).
enum class CommandKind { Walk, Build };

class PathExecutor {
public:
	// Arrival ring on a Walk waypoint. Tight enough that a cell pops before
	// the entity steps out of it at 2.5 blk/s; the half-plane test below
	// catches anything a single-tick step misses.
	static constexpr float kArriveRadius = 0.9f;
	static constexpr float kArriveY      = 1.0f;
	static constexpr float kDoorReachXZ  = 2.0f;

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
		// RTS-only fields. Slot is set by planGroup so formation viz can
		// read it; arrived gates steerTargetFor so the player-side loop knows
		// to stop issuing Moves.
		glm::ivec3        formationSlot{INT_MIN, INT_MIN, INT_MIN};
		bool              arrived = false;
		BuilderUnitState  builder;
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
	std::shared_ptr<PendingPlan>       m_pending;
	CommandKind                        m_kind        = CommandKind::Walk;
	bool                               m_builderMode = false;

	static glm::vec3 centerOf(const Waypoint& w) {
		return {w.pos.x + 0.5f, (float)w.pos.y, w.pos.z + 0.5f};
	}
	bool reached(const Waypoint& w, const glm::vec3& pos) const;
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

}  // namespace civcraft
