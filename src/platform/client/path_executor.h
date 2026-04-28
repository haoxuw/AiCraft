#pragma once

// Unified client-side path executor. Replaces both agent/pathfind.h's
// per-entity PathExecutor and the older client/rts_executor.h's FlowField-
// backed RtsExecutor.
//
// Design:
//   * ONE class handles every pathed-movement caller: NPC behaviors (via
//     Navigator), player click-to-move, RTS group commands. Each tracked
//     entity gets its own pop-front Path queue; group commands are sugar
//     around GridPlanner::planBatch + N setPath() calls.
//   * Pop predicate: segment-crossing on (prevPos, pos) per tick. A
//     waypoint retires when the entity's last-tick movement crossed the
//     perpendicular plane through its center, OR the entity is standing
//     on the center within kSnapRadius. Multi-cell overshoot (teleport,
//     collision push-out, clientPos snap-back) drains via the bounded
//     while-loop (kMaxPopsPerTick).
//   * Per-tick driver: PathExecutor::driveOne is the single per-entity
//     routine. Velocity is the RAW direction toward the front (no
//     smoothing) clamped to one tick's worth of motion; visual body
//     rotation is decoupled (server's smoothYawTowardsVelocity, 20 rad/s)
//     so 90° corners produce a brief sideways "strafe" while body catches
//     up. Move emission goes through agent/move_emit.h::emitMoveAction —
//     the shared helper Agent::sendMove also calls for non-pathed steering.
//   * Convergence watchdog: every tick measures distance to the front
//     waypoint; if no new minimum is reached within kConvergeStallTicks,
//     g_pathConvergenceFailures bumps. Tests assert against the counter.
//   * Doors are first-class. A DoorOracle installed at construction is
//     consulted for every tracked entity. passedDoors auto-close fires
//     once the entity has stepped clear of every slab it walked through.
//   * Async planning for groups: detached std::thread runs planBatch;
//     poll() swaps paths in when the worker reports back.
//
// Rule 0: emits only Move / Interact ActionProposals.
// Rule 4: intelligence runs on the agent-client side; this class is it.
// Rule 7: emit pipeline lives in exactly one place (emitMoveAction).

#include "agent/pathfind.h"
#include "agent/move_emit.h"            // MoveContext + emitMoveAction (shared with Agent)
#include "logic/action.h"
#include "logic/block_registry.h"
#include "logic/chunk_source.h"
#include "logic/entity.h"
#include "net/server_interface.h"

#include <glm/glm.hpp>

#include <atomic>
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

namespace solarium {

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

// Server-backed proximity oracle — iterates live entities to answer the
// auto-close politeness check. Cells are doorway block cells; we compare
// against each entity's XZ. Y is ignored (a doorway is conceptually a 2D
// gap on the map even though slabs span 2 blocks vertically).
//
// `ownerEid` is the real entity id this oracle belongs to (the NPC whose
// Navigator owns it). Used to exclude self from the politeness check —
// we shouldn't defer closing the door because we're standing in it.
struct ServerEntityProximityOracle : public EntityProximityOracle {
	ServerInterface& server;
	EntityId         ownerEid;
	ServerEntityProximityOracle(ServerInterface& s, EntityId e)
		: server(s), ownerEid(e) {}
	bool entityNearAny(const std::vector<glm::ivec3>& cells, float radius,
	                   EntityId /*selfFromExec*/) const override {
		const float r2 = radius * radius;
		bool hit = false;
		server.forEachEntity([&](Entity& e) {
			if (hit) return;
			if (e.id() == ownerEid) return;
			for (auto& c : cells) {
				float dx = e.position.x - ((float)c.x + 0.5f);
				float dz = e.position.z - ((float)c.z + 0.5f);
				if (dx * dx + dz * dz < r2) { hit = true; return; }
			}
		});
		return hit;
	}
};

// Walk: cancel if unreachable. Build (long-press): enter experimental builder
// mode (Phase 1 jump-climb; Phase 2+ stack blocks to bridge gaps).
enum class CommandKind { Walk, Build };

class PathExecutor {
public:
	// Waypoint pop: segment-crossing predicate on (prevPos, pos) per tick.
	// A waypoint retires when the entity's last-tick movement either crossed
	// the perpendicular plane through it (within a corridor slack) OR the
	// entity is standing on its center within kSnapRadius. This subsumes
	// magnet-ring approach, "about to overshoot," and "already past" in a
	// single test — and crucially, fixes the orbit-around-waypoint pathology
	// where a tight magnet ring + turn-rate-capped velocity could leave the
	// entity circling the cell forever (min turn radius v/ω > magnet radius).
	// kArriveRadius is retained only for formation-slot arrival.
	static constexpr float kCorridorSlack = 0.5f;  // perpendicular tolerance for the crossing test
	static constexpr float kSnapRadius    = 0.10f; // standing-on-waypoint at zero motion
	static constexpr float kArriveRadius  = 0.9f;  // slot arrival only
	static constexpr float kArriveY       = 1.0f;  // waypoint Y tolerance
	// Predict-arrival pop: retire the front wp once XZ distance to its
	// center is below this radius. Sized comfortably above one tick at
	// walkSpeed=4 (0.067m) so even a separation-deflected approach is
	// caught before the segment-cross predicate (which a curved approach
	// can miss entirely → orbit-until-stall-pop, observed as ratio>>3 in
	// `seg:` logs).
	//
	// Heading auto-corrects the off-axis residual: the next tick's vel is
	// direction(curPos → next_wp_center), so a parallel-offset entry into
	// the next segment is pulled back toward the ideal axis as it travels.
	// No teleport, no clientPos plumbing; the predicate change is the fix.
	static constexpr float kPredictArrivalRadius = 0.30f;
	static constexpr float kDoorReachXZ   = 2.0f;
	// Defensive cap on the per-tick pop while-loop (replaces the old O(N)
	// forward-scan). Multi-cell teleport / snap-back drains this many steps
	// in one tick; pathological loops bail. Sized to support arbitrarily
	// fast entities — at walkSpeed=1500 m/s and dt=1/60 the entity covers
	// 25 cells per tick on a straight, well under this cap.
	static constexpr int   kMaxPopsPerTick = 256;

	// Convergence watchdog threshold — see Unit::nonConvergeTicks. ~1.5 s
	// at 60 Hz, generous enough to survive brief slide-around-obstacle
	// excursions, but tight enough that a stable orbit is caught quickly.
	// Cooperates with stall-pop (kStallTicks=30) by giving stall-pop first
	// crack at "stuck in place" before convergence-fail flags "moving but
	// going nowhere."
	static constexpr int   kConvergeStallTicks = 90;

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
	// Connected-door BFS — flood-fill horizontal cardinals from the seed
	// slab so a 2-wide / 4-wide / L-shaped door opens as one unit. Capped
	// to keep the search bounded under pathological layouts.
	static constexpr int   kDoorClusterMaxCells = 8;
	// Auto-close: once entity is this far (XZ) from every previously-
	// opened slab, fire a closing Interact so the door doesn't sit open.
	static constexpr float kDoorCloseDistance   = 1.6f;
	// Don't close if anyone else is standing within this radius of the
	// doorway — closing in someone's face is rude. Bumped from 1.5 → 2.5
	// so an NPC walking *toward* the doorway from a few cells away also
	// gets a clear path (any entity within range defers the close).
	static constexpr float kDoorPolitenessRadius = 2.5f;
	// Per-entity passedDoors cap. Anyone who walks through an open-door
	// cell enqueues the cluster; capped FIFO so a long path through many
	// doors doesn't grow unboundedly.
	static constexpr int   kMaxPassedDoors = 16;

	struct Intent {
		enum Kind { None, Move, Interact } kind = None;
		glm::vec3  target      = {0, 0, 0};
		// All connected door slabs to toggle in this Interact. A double-door
		// (or wider) emits ≥2 entries; the agent fires one ActionProposal per
		// cell so the whole set opens together. Single-slab doors → size 1.
		// Vertical stack propagation stays server-side.
		std::vector<glm::ivec3> interactPos;
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
	// Optional — enables auto-close politeness check. Null = skip the check
	// (close fires whenever distance threshold is met).
	void setEntityProximityOracle(const EntityProximityOracle* o) { m_entities = o; }

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
	// Mutable access to a tracked Unit's MoveContext — used by Navigator to
	// forward Agent-side goalText into the per-Unit emit pipeline. Caller
	// is responsible for has(eid) check.
	MoveContext& unitMoveCtx(EntityId eid) { return m_units.at(eid).moveCtx; }
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
	// Single per-entity-per-tick routine. THIS IS THE ONLY PLACE pathed
	// movement emission happens — both the RTS multi-entity loop
	// (driveRemote) and NPC AI (Navigator::driveTick) route through here.
	// Calls tick() once, dispatches by Intent kind:
	//   • Move      → emitMoveAction (sep, stuck, e.velocity, ActionProposal)
	//   • Interact  → one ActionProposal::Interact per slab + a stop Move
	//                 so server halts residual velocity while the door swings
	//   • None      → if formationSlot is set (RTS async pending or builder
	//                 mode), naive steer toward slot. Else emit a stop.
	// Returns the Intent so callers can read the kind and update higher-
	// level status (Navigator::Status, RTS done-marker, etc.).
	Intent driveOne(EntityId eid, ServerInterface& server,
	                float walkSpeedFallback = 2.0f,
	                float jumpVelocity      = 8.3f);
	// Emit Move ActionProposals for every tracked entity except excludedId
	// (the possessed player, which is driven by input directly). Thin loop
	// over driveOne. Builder mode jumps are picked up inside driveOne.
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
		int               interactCooldown = 0;
		// Slabs we walked *through* (not just opened) — anyone closes any
		// door behind them. Filled by the pop loop when the popped cell is
		// currently isOpenDoor (BFS the open-cluster around it). Auto-close
		// fires once we're ≥ kDoorCloseDistance from every slab here.
		// Cleared on the closing Interact, on cancel, and on a fresh setPath.
		std::vector<glm::ivec3> passedDoors;
		// Previous-tick XZ position, fed into the segment-crossing pop test.
		// Lazy-init: hasPrevPos=false until the first tick after setPath, so
		// a sentinel value can't trigger a spurious pop on a huge fake seg.
		glm::vec3         prevPos{0, 0, 0};
		bool              hasPrevPos = false;

		// ── Convergence watchdog ─────────────────────────────────────────
		// Per-tick invariant: distance to the current front waypoint must
		// reach a new minimum within kConvergeStallTicks of the front
		// becoming the front. Otherwise the entity is orbiting / sweeping
		// past in a curve that never crosses the corridor — same symptom
		// the speed-clamp is supposed to prevent. Stall-pop catches "stuck
		// in place"; this catches "moving but not making progress."
		//
		// minDistToFront: smallest XZ distance to centerOf(front) seen
		//   since this front took the slot.
		// nonConvergeTicks: consecutive ticks where current dist >= minDist.
		// lastFrontWp: detect front change → reset trackers.
		// lastConvergePos: dedupes intra-frame double-tick (driveTick →
		//   driveOne → steerTargetFor → tick is two tick() calls per frame).
		glm::ivec3        lastFrontWp{INT_MIN, INT_MIN, INT_MIN};
		float             minDistToFront = 1e30f;
		int               nonConvergeTicks = 0;
		glm::vec3         lastConvergePos{0, 0, 0};
		bool              hasLastConvergePos = false;

		// ── Per-segment timing + arc tracking (waypoint cadence + orbit) ──
		// `seg:` log per pop reports dist (direct), arc (sum of |Δpos|), and
		// ratio = arc/direct. Healthy straight Walk: ratio ≈ 1. Orbit (sep
		// or other deflection redirecting velocity sideways): ratio >> 1
		// even when minDistToFront stays > kSnapRadius — that's the signal.
		glm::vec3         lastPopPos{0, 0, 0};
		bool              hasLastPopPos = false;
		int               ticksSinceLastPop = 0;
		glm::vec3         lastTickPos{0, 0, 0};
		bool              hasLastTickPos = false;
		float             arcLengthInSeg = 0.0f;

		// Per-call state for the shared Move emit pipeline (separation LPF,
		// stuck watchdog, goalText). Same struct Agent holds for non-pathed
		// callers — emitMoveAction is the single helper that mutates this.
		MoveContext       moveCtx;
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
	const EntityProximityOracle*       m_entities    = nullptr;
	std::shared_ptr<PendingPlan>       m_pending;
	CommandKind                        m_kind        = CommandKind::Walk;
	bool                               m_builderMode = false;

	static glm::vec3 centerOf(const Waypoint& w) {
		return {w.pos.x + 0.5f, (float)w.pos.y, w.pos.z + 0.5f};
	}
	// Segment-crossing pop predicate. Reached iff the entity's last-tick
	// movement crossed the perpendicular plane through wp center within the
	// corridor slack, OR the entity is standing on it within kSnapRadius.
	// Y must be within kArriveY (Jump/Descend slack).
	static bool passedThisTick(const glm::vec3& prev, const glm::vec3& pos,
	                           const Waypoint& w);
	// BFS the connected door cluster (4-cardinal horizontal, same Y).
	// `wantClosed=true` walks closed-door neighbours (used when opening);
	// false walks open-door neighbours (used when popping a passed cell).
	// Always includes seed if it matches. Capped at kDoorClusterMaxCells.
	std::vector<glm::ivec3> findConnectedDoorSlabs(glm::ivec3 seed,
	                                               bool wantClosed = true) const;
	// Append `cluster` slabs to passedDoors with FIFO cap and dedup.
	void recordPassedDoors(Unit& u, const std::vector<glm::ivec3>& cluster) const;
	// Deflect `target` toward the best standable cardinal neighbour when the
	// straight-line approach would clip a wall. No-op when m_world is unset.
	glm::vec3 slideAroundObstacle(const glm::vec3& pos, glm::vec3 target,
	                              Unit& u) const;
	// ── tick() phase helpers ─────────────────────────────────────────
	// Each helper either returns a final Intent (short-circuit) or nullopt
	// to let tick() proceed. PERF_SCOPE'd individually so the per-phase
	// histogram surfaces in `make perf_fps`.
	std::optional<Intent> stepAutoCloseDoors(EntityId eid, Unit& u,
	                                         const glm::vec3& pos);
	bool                  stepDetectStall   (Unit& u, const glm::vec3& pos);
	std::optional<Intent> stepDoorScanProbe (Unit& u, const glm::vec3& pos);
	void                  stepPopReached    (EntityId eid, Unit& u,
	                                         const glm::vec3& pos);
	// Convergence watchdog — see Unit::nonConvergeTicks. Called once per
	// tick (deduped against intra-frame double-tick). Per-tick PATHLOG of
	// (front, dist, minDist, stagnant-tick-count) for log inspection;
	// counts a failure into the test-readable g_pathConvergenceFailures
	// when stagnation exceeds the threshold.
	void                  stepConvergenceWatchdog(EntityId eid, Unit& u,
	                                              const glm::vec3& pos);
	std::optional<Intent> stepFrontDoorHandshake(Unit& u, const glm::vec3& pos);
	glm::vec3             stepComputeMoveTarget (Unit& u, const glm::vec3& pos);
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
		// Connected door slabs to toggle (≥1 — multi for double/wide doors).
		std::vector<glm::ivec3> interactPos;
	};

	// `eid` is the agent's real entity id — used both as the executor's
	// per-Unit key and as the actorId on emitted ActionProposals.
	Navigator(EntityId eid, const WorldView& world,
	          const DoorOracle* doors = nullptr);

	// Start (or re-plan) to the block at `goal`. Re-planning only fires if the
	// goal cell differs from the active plan's goal — cheap call-every-tick.
	// Returns false if planner gave up (partial or empty path).
	bool setGoal(glm::ivec3 goal);

	// Single per-frame entry for NPC pathed movement. ALL emission lives
	// inside PathExecutor::driveOne (path_executor.cpp). Navigator owns
	// the lazy-plan + status translation; PathExecutor owns the predicate,
	// clamp, and Move / Interact / Stop emission. Agent::navigateApproach
	// calls this once per tick; `goalText` is forwarded to the Unit's
	// MoveContext so the emitted ActionProposal carries it. Returns the
	// Step (kind / target / interactPos) for the agent's logging layer.
	Step driveTick(Entity& e, ServerInterface& server,
	               const std::string& goalText,
	               float walkSpeedFallback = 2.0f,
	               float jumpVelocity      = 8.3f);

	void   clear();
	// Optional politeness oracle for the auto-close path — see
	// PathExecutor::setEntityProximityOracle.
	void   setEntityProximityOracle(const EntityProximityOracle* o) {
		m_exec.setEntityProximityOracle(o);
	}
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
	const Path& path() const { return m_exec.path(m_eid); }

	// Replan trigger — drop the cached plan on block changes in the corridor.
	bool invalidatedBy(glm::ivec3 changedBlock) const {
		return m_hasGoal && m_planner.pathInvalidatedBy(m_path, changedBlock);
	}

private:
	// Lazy plan with sub-cell-edge correction. Called by driveTick on the
	// first tick after setGoal. Returns true on success (path installed in
	// m_exec); false on planner give-up — caller sets m_status = Failed.
	bool lazyPlanIfNeeded(const glm::vec3& entityPos);

	const EntityId    m_eid;            // real entity id; key into m_exec.m_units
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

// Test-readable counter for convergence-watchdog hits. ALWAYS available
// (not gated on SOLARIUM_PATHFINDING_DEBUG) so headless tests can read it
// without changing build mode. The watchdog logic itself is also always-on
// (it's cheap — three floats + a comparison per tick per entity); only the
// stderr emit is debug-gated.
extern std::atomic<int> g_pathConvergenceFailures;

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

}  // namespace solarium
