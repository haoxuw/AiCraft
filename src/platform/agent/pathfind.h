#pragma once

// Waypoint planner for agent-client NPCs.
// Refs: [Baritone] A* over primitives; [Mineflayer] observe-and-interact;
//       [SupCom2 GDC 2011] reverse-Dijkstra; [HNR 1968] A*.
// Doors: planner treats as walkable, executor emits TYPE_INTERACT on closed door.

#include "logic/entity.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace civcraft {

// [Baritone] IPlayerContext — planner is chunk/server agnostic.
struct WorldView {
	virtual ~WorldView() = default;
	// Solid = blocks body (air/water/passable → false).
	virtual bool isSolid(glm::ivec3 p) const = 0;
};

// Adjacent-cell primitives (subset of [Baritone] MovementType).
enum class MoveKind : uint8_t {
	Walk,
	Jump,
	Descend,
};

struct Waypoint {
	glm::ivec3 pos;
	MoveKind   kind = MoveKind::Walk;
};

struct Path {
	std::vector<Waypoint> steps;
	float cost    = 0.0f;
	bool  partial = false;   // budget exhausted before reaching goal
};

// FlowCell prefix avoids collision with IVec3Hash/IVec3Eq elsewhere.
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

// [SupCom2 2007] reverse-Dijkstra: one sweep from goal; each cell stores next step.
// Units do O(1) lookup per tick.
struct FlowField {
	struct Entry {
		glm::ivec3 next;
		MoveKind   kind = MoveKind::Walk;
		float      g    = 0.0f;       // cost to goal
	};
	glm::ivec3 goal{};
	std::unordered_map<glm::ivec3, Entry, FlowCellHash, FlowCellEq> step;

	bool   empty() const { return step.empty(); }
	size_t size()  const { return step.size(); }
	bool   reaches(glm::ivec3 c) const { return step.count(c) > 0; }
};

// [Baritone] A* over movement primitives.
class GridPlanner {
public:
	struct Config {
		int   maxNodes       = 4096;
		float jumpCost       = 1.4f;   // [Baritone] ASCEND_COST ≈ sqrt(2)
		float descendCost    = 1.1f;
		int   corridorRadius = 4;      // invalidate if block change within N of path
		// 0=hug walls; 0.15=soft clearance; 0.3+=strong detour. Walk only —
		// Y constraints on Jump/Descend are physical, not cosmetic.
		float wallClearancePenalty = 0.25f;
	};

	explicit GridPlanner(const WorldView& world) : m_world(world) {}
	GridPlanner(const WorldView& world, Config cfg) : m_world(world), m_cfg(cfg) {}

	const Config& config() const { return m_cfg; }  // tests only

	// [HNR 1968] single-unit A*. Over-budget → best-seen partial. Doors passable; executor toggles.
	Path plan(glm::ivec3 start, glm::ivec3 goal);

	// [SupCom2 GDC 2011] reverse-Dijkstra batch — shared closed set, O(1)/start reconstruction.
	std::vector<Path> planBatch(const std::vector<glm::ivec3>& starts,
	                            glm::ivec3 goal);

	// One sweep, shared across units. startsHint empty = full sweep; else early-exit when all settled.
	FlowField planFlowField(glm::ivec3 goal,
	                        const std::vector<glm::ivec3>& startsHint = {});

	// [Baritone] PathExecutor#onTick: triggers replan if change within corridorRadius.
	bool pathInvalidatedBy(const Path& path, glm::ivec3 changedBlock) const;

private:
	const WorldView& m_world;
	Config            m_cfg;
};

// Optional door oracle. Executor queries these only when the next waypoint
// cell is solid — planner already marked it passable, so a solid hit means
// it's a door (or similar interactive). Returning true for `isClosedDoor`
// makes the executor emit a one-shot Interact; `isOpenDoor` lets it fall
// through as walkable without opening anything.
struct DoorOracle {
	virtual ~DoorOracle() = default;
	virtual bool isClosedDoor(glm::ivec3 p) const = 0;
	virtual bool isOpenDoor  (glm::ivec3 p) const = 0;
};

// Per-agent cursor driver. Doors: [Mineflayer] observe-and-interact.
class PathExecutor {
public:
	struct Intent {
		enum Kind { None, Move, Interact } kind = None;
		glm::vec3  target      = {0,0,0};
		glm::ivec3 interactPos = {0,0,0};
	};

	void setPath(Path p) { m_path = std::move(p); m_cursor = 0; m_waitOpen = false; }
	void clear()         { m_path = {}; m_cursor = 0; m_waitOpen = false; }
	bool done() const    { return m_cursor >= (int)m_path.steps.size(); }

	// [Mineflayer] approach → observe door → interact adjacent → wait open → step.
	// Pass a DoorOracle to enable auto-open behavior; nullptr = legacy (no doors).
	Intent tick(const glm::vec3& entityPos, const WorldView& world,
	            const DoorOracle* doors = nullptr);

private:
	Path m_path;
	int  m_cursor   = 0;
	bool m_waitOpen = false;  // emitted Interact, awaiting door-open observation
};

// Scripting-friendly facade: wraps GridPlanner + PathExecutor behind a single
// stateful object. Python behaviors use this; native code can still reach the
// lower-level primitives directly.
class Navigator {
public:
	enum class Status : uint8_t { Idle, Planning, Walking, OpeningDoor, Arrived, Blocked };

	struct Step {
		enum Kind { None, Move, Interact } kind = None;
		glm::vec3  moveTarget  = {0,0,0};     // world-space cell center
		glm::ivec3 interactPos = {0,0,0};     // door cell to toggle
	};

	Navigator(const WorldView& world, const DoorOracle* doors = nullptr)
		: m_world(world), m_doors(doors), m_planner(world) {}

	// Start (or re-plan) to the block at `goal`. Re-planning only fires if the
	// goal cell differs from the active plan's goal — cheap call-every-tick.
	// Returns false if planner gave up (partial or empty path).
	bool setGoal(glm::ivec3 goal);

	// Advance one tick. Returns the step to take (Move/Interact/None). When
	// status() == Arrived or Blocked, the caller should pick a new goal.
	Step tick(const glm::vec3& entityPos);

	void   clear()           { m_exec.clear(); m_hasGoal = false; m_status = Status::Idle; }
	bool   hasGoal() const   { return m_hasGoal; }
	Status status() const    { return m_status; }
	glm::ivec3 goal() const  { return m_goal; }

	// Replan trigger — drop the cached plan on block changes in the corridor.
	bool invalidatedBy(glm::ivec3 changedBlock) const {
		return m_hasGoal && m_planner.pathInvalidatedBy(m_path, changedBlock);
	}

private:
	const WorldView&  m_world;
	const DoorOracle* m_doors;
	GridPlanner       m_planner;
	PathExecutor      m_exec;
	Path              m_path;
	glm::ivec3        m_goal{0};
	bool              m_hasGoal = false;
	Status            m_status  = Status::Idle;
};

} // namespace civcraft
