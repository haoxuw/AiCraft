#pragma once

// Waypoint planner for agent-client NPCs.
// Refs: [Baritone] A* over primitives; [Mineflayer] observe-and-interact;
//       [SupCom2 GDC 2011] reverse-Dijkstra; [HNR 1968] A*.
// Doors: planner treats as walkable, executor emits TYPE_INTERACT on closed door.

#include "logic/entity.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace civcraft {

// [Baritone] IPlayerContext — planner is chunk/server agnostic.
struct WorldView {
	virtual ~WorldView() = default;
	// Solid = blocks body (air/water/passable → false).
	virtual bool isSolid(glm::ivec3 p) const = 0;
};

// 4-cardinal XZ neighbours. Shared by A* expansion (pathfind.cpp) and
// executor-side neighbour scoring (path_executor.cpp). Single source of truth
// so Walk/Jump/Descend primitives stay aligned across planner and executor.
static constexpr glm::ivec3 CARDINAL_DIRS[4] = {
	{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}
};

// Entity is 2 cells tall: floor solid, body + head air. Every planner
// expansion, executor side-step, and start-cell correction uses this exact
// three-cell check — do not reimplement it inline.
inline bool isStandable(const WorldView& w, glm::ivec3 p) {
	return  w.isSolid({p.x, p.y - 1, p.z}) &&
	       !w.isSolid(p) &&
	       !w.isSolid({p.x, p.y + 1, p.z});
}

// Adjacent-cell primitives (subset of [Baritone] MovementType).
enum class MoveKind : uint8_t {
	Walk,
	Jump,
	Descend,
};

inline const char* toString(MoveKind k) {
	switch (k) {
		case MoveKind::Walk:    return "Walk";
		case MoveKind::Jump:    return "Jump";
		case MoveKind::Descend: return "Descend";
	}
	return "?";
}

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
	// Count CARDINAL_DIRS neighbours of `cell` that are solid, scaled by the
	// config weight. Zero when wallClearancePenalty≤0 (disabled). Used
	// identically in plan() forward and planBatch()/planFlowField() reverse —
	// keep it as a member so the three A* variants stay aligned.
	float wallClearancePenalty(glm::ivec3 cell) const {
		if (m_cfg.wallClearancePenalty <= 0.0f) return 0.0f;
		int walls = 0;
		for (auto d : CARDINAL_DIRS) if (m_world.isSolid(cell + d)) walls++;
		return m_cfg.wallClearancePenalty * (float)walls;
	}

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

// PathExecutor and Navigator moved to client/path_executor.h — one unified
// class serves both single-entity NPC nav and multi-entity RTS group commands.
// This header keeps only the planner-side types (WorldView, GridPlanner, Path,
// DoorOracle, FlowField, MoveKind, Waypoint) that have no client-side deps.

} // namespace civcraft
