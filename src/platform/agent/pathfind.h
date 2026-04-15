#pragma once

/**
 * pathfind.h — Waypoint planner for agent-client NPCs.
 *
 * STATUS: SKELETON / PSEUDOCODE. Nothing here is wired into agent_client yet.
 * Methods return empty/default values. Fill in once the shape is accepted.
 *
 * ─── Sources (every design decision below is lifted, not invented) ────────
 *
 *  [Baritone]   github.com/cabaletta/baritone
 *               Java, Minecraft — canonical "A* over movement primitives".
 *               Node = (blockPos, MovementType). We copy the vocabulary.
 *
 *  [Mineflayer] github.com/PrismarineJS/mineflayer-pathfinder
 *               JS, Minecraft-protocol — simpler Baritone cousin. Source of
 *               the `allowDoors` / open-on-observation pattern (see tick()).
 *
 *  [Recast]     github.com/recastnavigation/recastnavigation
 *               C++ — off-mesh links inspired "door as tagged edge". We do
 *               NOT take the nav-mesh substrate (rebuild cost too high for
 *               destructible voxels); only the tag-the-edge idea.
 *
 *  [SupCom2]    Emerson, "Efficient and Reactive Crowd Navigation" GDC 2011
 *               Reverse search from shared goal for N-agents-to-one-goal.
 *               We keep the reverse-A* / Dijkstra core; skip the vector-field
 *               because <100 NPCs with heterogeneous goals don't need it.
 *
 *  [HNR 1968]   Hart/Nilsson/Raphael — A* itself. Nothing novel here.
 *
 * ─── Not adopted (and why) ────────────────────────────────────────────────
 *   - Full Recast nav mesh: rebuild cost prohibitive when players mine.
 *   - JPS / JPS+: precompute incompatible with dynamic blocks; 3D gains small.
 *   - HPA*: premature complexity at <100 NPCs and short paths.
 *   - Flow fields: only win for N→1 attack-moves; not the common case.
 *
 * ─── Design decision: doors handled in executor, not planner ─────────────
 *   Per 2026-04-15 review, planner treats door cells as plain walkable
 *   (following Mineflayer's `allowDoors=true` behaviour). PathExecutor
 *   observes the door block each tick and emits TYPE_INTERACT when it
 *   finds the next waypoint's cell is a closed door. Rationale:
 *     - One less MoveKind, no blockId on Waypoint.
 *     - No locked doors in CivCraft today → no need for negative feedback.
 *     - Re-add MoveKind::Door later if locked/team-owned doors ship.
 */

#include "shared/entity.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace civcraft {

// ─────────────────────────────────────────────────────────────────────────
// WorldView — abstract block-query interface the planner depends on.
// [Baritone] IPlayerContext, [Recast] rcContext: inject the world query so
// the planner doesn't know about chunks, caches, or servers. Tests can
// provide a TestServer-backed view; the agent client later provides a
// LocalWorld-backed view.
// ─────────────────────────────────────────────────────────────────────────
struct WorldView {
	virtual ~WorldView() = default;
	// True if the block at p blocks entity movement (feet can stand on it,
	// body cannot occupy it). Air, water, and passable blocks return false.
	virtual bool isSolid(glm::ivec3 p) const = 0;
};

// ── Primitive movements an NPC can take between adjacent grid cells ─────
// Vocabulary from [Baritone] MovementType (subset). Break/Place deferred.
enum class MoveKind : uint8_t {
	Walk,     // horizontal step, same Y          — MovementTraverse
	Jump,     // +1 Y step onto a block           — MovementAscend
	Descend,  // -1 Y drop                        — MovementDescend / Fall
};

struct Waypoint {
	glm::ivec3 pos;
	MoveKind   kind = MoveKind::Walk;
};

struct Path {
	std::vector<Waypoint> steps;
	float cost    = 0.0f;
	bool  partial = false;   // budget ran out before reaching goal
};

// Hash + eq for glm::ivec3 keys in FlowField. Named FlowCell* to avoid
// colliding with IVec3Hash/IVec3Eq defined elsewhere in the project.
struct FlowCellHash {
	size_t operator()(glm::ivec3 p) const noexcept {
		size_t h = std::hash<int>()(p.x);
		h ^= std::hash<int>()(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<int>()(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}
};
struct FlowCellEq {
	bool operator()(glm::ivec3 a, glm::ivec3 b) const noexcept {
		return a.x == b.x && a.y == b.y && a.z == b.z;
	}
};

// ════════════════════════════════════════════════════════════════════════
// FlowField — shared reverse-Dijkstra result. [SupCom2 2007] / Planetary
// Annihilation / Factorio. One Dijkstra sweep outward from the goal;
// every visited cell stores the next-step direction toward the goal.
// Units read `step[myCell]` each tick — O(1) per unit, regardless of
// how many units share the field.
// ════════════════════════════════════════════════════════════════════════
struct FlowField {
	struct Entry {
		glm::ivec3 next;              // cell to step into next
		MoveKind   kind = MoveKind::Walk;
		float      g    = 0.0f;       // accumulated cost from cell to goal
	};
	glm::ivec3 goal{};
	std::unordered_map<glm::ivec3, Entry, FlowCellHash, FlowCellEq> step;

	bool   empty() const { return step.empty(); }
	size_t size()  const { return step.size(); }
	bool   reaches(glm::ivec3 c) const { return step.count(c) > 0; }
};

// ════════════════════════════════════════════════════════════════════════
// GridPlanner — A* over movement primitives. [Baritone]-style.
// ════════════════════════════════════════════════════════════════════════
class GridPlanner {
public:
	struct Config {
		int   maxNodes       = 4096;   // A* expansion budget [Baritone: similar cap]
		float jumpCost       = 1.4f;   // [Baritone: ASCEND_COST ≈ sqrt(2)]
		float descendCost    = 1.1f;   // [Baritone: DESCEND_COST slightly > walk]
		int   corridorRadius = 4;      // block-change within N of path → invalidate
		// Per-wall-neighbor penalty added to Walk step cost. Counts how many
		// of the 4 horizontal neighbours at the destination cell's feet level
		// are solid, multiplied by this. 0 = hug walls tightly (old A*);
		// 0.15 = slight clearance preference; 0.3+ = strong detour toward
		// open ground. Only applied to Walk (not Jump/Descend — those have
		// physical Y constraints, not cosmetic spacing).
		float wallClearancePenalty = 0.25f;
	};

	explicit GridPlanner(const WorldView& world) : m_world(world) {}
	GridPlanner(const WorldView& world, Config cfg) : m_world(world), m_cfg(cfg) {}

	// Accessor for tests; not for production use.
	const Config& config() const { return m_cfg; }

	// ────────────────────────────────────────────────────────────────
	// Single-unit plan. [HNR 1968] A*, [Baritone] primitive expansion.
	// ────────────────────────────────────────────────────────────────
	// PSEUDOCODE:
	//   // init — scratch buffers reused across calls to avoid alloc
	//   clear(open, gScore, cameFrom)
	//   gScore[start] = 0
	//   open.push({start, h(start, goal)})
	//   bestSeen = start                           // for partial fallback
	//   expanded = 0
	//
	//   while !open.empty() && expanded < cfg.maxNodes:
	//     cur = open.pop()                         // min fScore
	//     expanded++
	//     if cur == goal:                          return reconstruct(cur)
	//     if h(cur, goal) < h(bestSeen, goal):     bestSeen = cur
	//
	//     // expand(cur) yields at most 8 walk + 4 jump + 4 descend = 16
	//     for (next, kind, stepCost) in expand(cur):
	//       if !walkable(next, kind): continue     // see walkable() below
	//       tentative = gScore[cur] + stepCost
	//       if tentative < gScore[next]:
	//         gScore[next]   = tentative
	//         cameFrom[next] = (cur, kind)
	//         open.push({next, tentative + h(next, goal)})
	//
	//   // [Baritone] behaviour: if we hit the budget, return the best
	//   // partial path toward the goal; executor treats partial as
	//   // "arrive here, then replan".
	//   return reconstruct(bestSeen) with partial=true
	//
	// expand(p):
	//   // [Baritone] MovementTraverse × 8 compass directions
	//   for d in 8 horizontal dirs:
	//     yield (p+d,       Walk,    1.0)
	//   // [Baritone] MovementAscend — jump onto +1 block
	//   for d in 4 cardinals:
	//     yield (p+d+up,    Jump,    cfg.jumpCost)
	//   // [Baritone] MovementDescend — step off a ledge
	//   for d in 4 cardinals:
	//     yield (p+d+down,  Descend, cfg.descendCost)
	//
	// walkable(p, kind):
	//   // [Mineflayer] Move.getBlock checks for solid feet + air head.
	//   // Door cells are treated as passable here (open-or-closed both OK),
	//   // per the executor-handles-doors decision above.
	//   feet      = block(p - up)                  // must be solid, non-hazard
	//   body      = block(p)                       // must be passable (air/door)
	//   head      = block(p + up)                  // must be passable
	//   if kind == Jump:   require block(p + up*2) passable too
	//   if kind == Descend: require block(p - up*2) solid (landing floor)
	//   return feet.solid && body.passable && head.passable
	//
	// h(a, b):
	//   // 3D octile distance — [standard grid-pathfinding literature].
	//   dx = |a.x-b.x|; dy = |a.y-b.y|; dz = |a.z-b.z|
	//   dHoriz = max(dx,dz) + (sqrt(2)-1) * min(dx,dz)
	//   return dHoriz + dy
	Path plan(glm::ivec3 start, glm::ivec3 goal);

	// ────────────────────────────────────────────────────────────────
	// Batch mode. [SupCom2 GDC 2011] reverse-from-goal, our variant:
	// reverse A* / Dijkstra instead of vector field; each entity reads
	// its own reconstructed path from the shared closed set.
	// batch-of-1 is the same code path — no special case.
	// ────────────────────────────────────────────────────────────────
	// PSEUDOCODE:
	//   // Dijkstra from goal outward. Stop once every start has been
	//   // settled, or budget = maxNodes * starts.size() exhausted.
	//   clear(open, gScore, cameFrom)
	//   gScore[goal] = 0
	//   open.push({goal, 0})
	//   remaining = set(starts)
	//   budget = cfg.maxNodes * starts.size()
	//
	//   while !open.empty() && budget-- > 0 && !remaining.empty():
	//     cur = open.pop()
	//     remaining.erase(cur)                     // may hit 0 early
	//     for (next, kind, stepCost) in expand(cur):   // same expand()
	//       if !walkable(next, kind): continue
	//       tentative = gScore[cur] + stepCost
	//       if tentative < gScore[next]:
	//         gScore[next]   = tentative
	//         cameFrom[next] = (cur, reverseKind(kind))  // flip Jump↔Descend
	//         open.push({next, tentative})
	//
	//   out = []
	//   for s in starts:
	//     if gScore.has(s): out.push_back(reconstructFromGoal(s))
	//     else:             out.push_back(Path{})         // unreachable
	//   return out
	std::vector<Path> planBatch(const std::vector<glm::ivec3>& starts,
	                            glm::ivec3 goal);

	// ────────────────────────────────────────────────────────────────
	// Flow field: one reverse-Dijkstra sweep, no per-unit path
	// reconstruction. The returned FlowField is shared across all
	// units commanded to `goal` — each unit just looks up
	// `field.step[floor(pos)]` every tick. [SupCom2 2007]
	//
	// `startsHint` is an optional early-termination set: once every
	// hint cell has been settled, the sweep stops. Pass the group's
	// start cells for big perf wins on large maps; pass {} for an
	// unrestricted sweep (useful if new units may join mid-command).
	// ────────────────────────────────────────────────────────────────
	FlowField planFlowField(glm::ivec3 goal,
	                        const std::vector<glm::ivec3>& startsHint = {});

	// ────────────────────────────────────────────────────────────────
	// Corridor invalidation. [Baritone] `PathExecutor#onTick` does the
	// equivalent: if a block along the planned corridor changes, drop
	// the plan and request a replan next decide tick.
	// ────────────────────────────────────────────────────────────────
	// PSEUDOCODE:
	//   for wp in path.steps:
	//     if chebyshev(wp.pos, changedBlock) <= cfg.corridorRadius:
	//       return true
	//   return false
	bool pathInvalidatedBy(const Path& path, glm::ivec3 changedBlock) const;

private:
	const WorldView& m_world;
	Config            m_cfg;
	// Scratch buffers (reused across plan() calls):
	//   std::vector<OpenEntry>                m_open;      // min-heap
	//   std::unordered_map<int64_t, float>    m_gScore;
	//   std::unordered_map<int64_t, CameFrom> m_cameFrom;
};

// ════════════════════════════════════════════════════════════════════════
// PathExecutor — per-agent cursor driver.
// Replaces direct-line Navigator.navigate for NPCs.
// Door handling is the [Mineflayer] observe-and-interact pattern.
// ════════════════════════════════════════════════════════════════════════
class PathExecutor {
public:
	struct Intent {
		enum Kind { None, Move, Interact } kind = None;
		glm::vec3 target   = {0,0,0};   // Move: center of next waypoint
		glm::ivec3 interactPos = {0,0,0};   // Interact: block to toggle
	};

	void setPath(Path p) { m_path = std::move(p); m_cursor = 0; m_waitOpen = false; }
	void clear()         { m_path = {}; m_cursor = 0; m_waitOpen = false; }
	bool done() const    { return m_cursor >= (int)m_path.steps.size(); }

	// ────────────────────────────────────────────────────────────────
	// Per-tick. [Mineflayer] movement-state-machine: approach → observe
	// door → if closed emit right-click → wait → step through.
	// ────────────────────────────────────────────────────────────────
	// PSEUDOCODE:
	//   if done(): return Intent{None}
	//   wp = m_path.steps[m_cursor]
	//
	//   // [Baritone] PathExecutor arrival test — within kArriveDist of
	//   // waypoint center horizontally, Y-aligned vertically.
	//   if arrived(entityPos, wp.pos):
	//     m_cursor++; m_waitOpen = false
	//     goto top        // tail-call: advance past 0-distance waypoints
	//
	//   // Executor-handles-doors (see header banner):
	//   nextBlock = world.block(wp.pos)
	//   if nextBlock.isDoor && !nextBlock.isOpen:
	//     // Approach until adjacent before interacting — avoids NPCs
	//     // toggling doors from 3 blocks away.
	//     if adjacent(entityPos, wp.pos):
	//       m_waitOpen = true
	//       return Intent{Interact, wp.pos}
	//     // else fall through to Move (close the gap first)
	//
	//   // If we emitted Interact last tick, hold position until the door
	//   // is observed open — prevents physics-wedging against closed door.
	//   if m_waitOpen:
	//     if nextBlock.isDoor && nextBlock.isOpen: m_waitOpen = false
	//     else: return Intent{None}              // hold
	//
	//   return Intent{Move, center(wp.pos)}
	Intent tick(const glm::vec3& entityPos, const WorldView& world);

private:
	Path m_path;
	int  m_cursor   = 0;
	bool m_waitOpen = false;  // emitted Interact, awaiting door-open observation
};

} // namespace civcraft
