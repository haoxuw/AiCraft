/**
 * pathfind.cpp — Movement-primitive A* (Walk/Jump/Descend).
 *
 * Algorithm lifted from:
 *   [Baritone] baritone.api.pathing.movement.MovementType + A* over it.
 *   [Mineflayer] mineflayer-pathfinder Movement.getBlock walkability pattern.
 *   [HNR 1968] A* with admissible heuristic (octile, 2D horizontal + |dy|).
 *
 * Scope (current):
 *   - plan(): real A* producing a reconstructed Path, partial=true on budget exhaustion.
 *   - planBatch(), pathInvalidatedBy(), PathExecutor: still stubs (next iteration).
 */

#include "agent/pathfind.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

namespace civcraft {

// ─────────────────────────────────────────────────────────────────────────
// Helpers — internal linkage.
// ─────────────────────────────────────────────────────────────────────────
namespace {

// Pack an ivec3 into int64 so it can key unordered_map + be cheap to compare.
// 21 bits per axis → range [-2^20, 2^20-1] = ±1,048,575 blocks, enough for
// any plausible path query.
inline int64_t encode(glm::ivec3 p) {
	constexpr int64_t BIAS = 1 << 20;
	constexpr int64_t MASK = (1LL << 21) - 1;
	return ((int64_t)(p.x + BIAS) & MASK) << 42
	     | ((int64_t)(p.y + BIAS) & MASK) << 21
	     | ((int64_t)(p.z + BIAS) & MASK);
}
inline glm::ivec3 decode(int64_t k) {
	constexpr int64_t BIAS = 1 << 20;
	constexpr int64_t MASK = (1LL << 21) - 1;
	int x = (int)((k >> 42) & MASK) - BIAS;
	int y = (int)((k >> 21) & MASK) - BIAS;
	int z = (int)( k        & MASK) - BIAS;
	return {x, y, z};
}

struct CameFrom {
	int64_t  prev;
	MoveKind kind;
};

// Min-heap entry. Storing g alongside f lets us detect stale pops without
// re-computing h() — standard A* optimisation.
struct OpenEntry {
	float   f;
	float   g;
	int64_t key;
	bool operator<(const OpenEntry& o) const { return f > o.f; }  // min-heap
};

} // namespace

// ═════════════════════════════════════════════════════════════════════════
// plan() — [Baritone]-style A* over Walk/Jump/Descend primitives.
// ═════════════════════════════════════════════════════════════════════════
Path GridPlanner::plan(glm::ivec3 start, glm::ivec3 goal) {
	// Octile horizontal + |dy| vertical. Admissible: never overestimates
	// since diagonals aren't allowed (4-cardinal expand) so dHoriz is a
	// lower bound on horizontal travel.
	auto H = [&](glm::ivec3 p) {
		int dx = std::abs(p.x - goal.x);
		int dy = std::abs(p.y - goal.y);
		int dz = std::abs(p.z - goal.z);
		float dHoriz = (float)(dx + dz);   // 4-cardinal: Manhattan, not octile.
		return dHoriz + (float)dy;
	};

	// [Mineflayer] Move.getBlock: entity occupies 2 cells vertically.
	// "standable" means p is a valid body cell — floor solid, body+head air.
	auto standable = [&](glm::ivec3 p) {
		if (!m_world.isSolid({p.x, p.y - 1, p.z})) return false;  // no floor
		if ( m_world.isSolid(p))                   return false;  // body blocked
		if ( m_world.isSolid({p.x, p.y + 1, p.z})) return false;  // head blocked
		return true;
	};

	static const glm::ivec3 DIRS[4] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};

	// Initial sanity: if start isn't standable, A* would fail immediately.
	// Seed gScore anyway — caller may have placed entity mid-air one frame.

	std::priority_queue<OpenEntry>      open;
	std::unordered_map<int64_t, float>  gScore;
	std::unordered_map<int64_t, CameFrom> cameFrom;

	const int64_t kStart = encode(start);
	const int64_t kGoal  = encode(goal);

	gScore[kStart] = 0.0f;
	open.push({H(start), 0.0f, kStart});

	int64_t bestSeen = kStart;
	float   bestH    = H(start);
	int     expanded = 0;
	bool    reached  = (kStart == kGoal);

	while (!open.empty() && expanded < m_cfg.maxNodes) {
		OpenEntry cur = open.top();
		open.pop();

		auto itG = gScore.find(cur.key);
		if (itG == gScore.end() || cur.g > itG->second + 1e-6f) continue;  // stale

		expanded++;

		if (cur.key == kGoal) { bestSeen = cur.key; reached = true; break; }

		glm::ivec3 p  = decode(cur.key);
		float      hp = H(p);
		if (hp < bestH) { bestH = hp; bestSeen = cur.key; }

		float gCur = itG->second;   // copy before gScore writes invalidate the iterator

		auto relax = [&](glm::ivec3 next, MoveKind kind, float stepCost) {
			const int64_t kN = encode(next);
			float tentative = gCur + stepCost;
			auto it = gScore.find(kN);
			if (it == gScore.end() || tentative < it->second - 1e-6f) {
				gScore[kN]  = tentative;
				cameFrom[kN] = {cur.key, kind};
				open.push({tentative + H(next), tentative, kN});
			}
		};

		// [Baritone] MovementTraverse × 4 cardinals (Walk).
		// [Baritone] MovementAscend           (Jump: +1 Y, requires takeoff headroom).
		// [Baritone] MovementDescend          (Descend: -1 Y, requires safe landing).
		const bool headroom = !m_world.isSolid({p.x, p.y + 2, p.z});
		for (auto d : DIRS) {
			glm::ivec3 w = p + d;
			if (standable(w)) relax(w, MoveKind::Walk, 1.0f);

			if (headroom) {
				glm::ivec3 j = p + d + glm::ivec3(0, 1, 0);
				if (standable(j)) relax(j, MoveKind::Jump, m_cfg.jumpCost);
			}

			glm::ivec3 dsc = p + d + glm::ivec3(0, -1, 0);
			if (standable(dsc)) relax(dsc, MoveKind::Descend, m_cfg.descendCost);
		}
	}

	// Reconstruct from bestSeen (= goal if reached, else closest-by-H).
	// [Baritone] PathExecutor treats partial=true as "arrive and replan".
	Path out;
	out.partial = !reached;

	std::vector<Waypoint> rev;
	int64_t cur = bestSeen;
	while (cur != kStart) {
		auto it = cameFrom.find(cur);
		if (it == cameFrom.end()) break;
		rev.push_back({decode(cur), it->second.kind});
		cur = it->second.prev;
	}
	out.steps.assign(rev.rbegin(), rev.rend());
	if (auto it = gScore.find(bestSeen); it != gScore.end()) out.cost = it->second;
	return out;
}

// ─────────────────────────────────────────────────────────────────────────
// Remaining stubs — next iterations.
// ─────────────────────────────────────────────────────────────────────────

// ═════════════════════════════════════════════════════════════════════════
// planBatch() — [SupCom2 GDC 2011]-style reverse Dijkstra from one shared
// goal to many starts. Grows a single closed set outward from the goal;
// each start reads its own path from the resulting cameFrom tree. Cost is
// one traversal regardless of the number of starts, which is the whole
// point when an RTS user box-selects 20 units and right-clicks once.
//
// Asymmetric primitives: in the *forward* graph Jump is `p → p+d+up` at
// cost 1.4 and Descend is `p → p+d-up` at cost 1.1. Running Dijkstra
// *backwards* from the goal means that when we pop cell C we have to
// enumerate every *predecessor* P from which some forward move lands on
// C. For each such (P, fwdCost) we relax g(P) = g(C) + fwdCost.
// ═════════════════════════════════════════════════════════════════════════
std::vector<Path> GridPlanner::planBatch(const std::vector<glm::ivec3>& starts,
                                         glm::ivec3 goal) {
	std::vector<Path> out(starts.size());
	if (starts.empty()) return out;

	auto standable = [&](glm::ivec3 p) {
		if (!m_world.isSolid({p.x, p.y - 1, p.z})) return false;
		if ( m_world.isSolid(p))                   return false;
		if ( m_world.isSolid({p.x, p.y + 1, p.z})) return false;
		return true;
	};

	static const glm::ivec3 DIRS[4] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};

	std::priority_queue<OpenEntry>        open;
	std::unordered_map<int64_t, float>    gScore;
	std::unordered_map<int64_t, CameFrom> cameFrom;

	const int64_t kGoal = encode(goal);
	gScore[kGoal] = 0.0f;
	open.push({0.0f, 0.0f, kGoal});   // pure Dijkstra: f = g (no heuristic)

	// Early-termination: as soon as every start has been settled, stop. Track
	// which starts are still pending via an encoded set.
	std::unordered_map<int64_t, size_t> pendingStarts;
	for (size_t i = 0; i < starts.size(); i++) {
		pendingStarts.emplace(encode(starts[i]), i);
	}

	// Budget grows with batch size so a 20-unit group gets 20× the A* budget.
	const int totalBudget = m_cfg.maxNodes * (int)starts.size();
	int expanded = 0;

	while (!open.empty() && expanded < totalBudget && !pendingStarts.empty()) {
		OpenEntry cur = open.top();
		open.pop();
		auto itG = gScore.find(cur.key);
		if (itG == gScore.end() || cur.g > itG->second + 1e-6f) continue;  // stale
		expanded++;

		// Mark this cell as settled if it's a start.
		pendingStarts.erase(cur.key);

		glm::ivec3 c    = decode(cur.key);
		float      gCur = itG->second;

		auto relax = [&](glm::ivec3 pred, MoveKind fwdKind, float fwdCost) {
			// The predecessor must itself be standable — i.e., a cell an
			// entity could actually be in when the move originated.
			if (!standable(pred)) return;
			const int64_t kP = encode(pred);
			float tentative = gCur + fwdCost;
			auto it = gScore.find(kP);
			if (it == gScore.end() || tentative < it->second - 1e-6f) {
				gScore[kP]  = tentative;
				// cameFrom[pred] points FORWARD toward goal: next step from
				// `pred` is cell `c` via `fwdKind`. Reconstruction just walks
				// this chain from each start down to goal.
				cameFrom[kP] = {cur.key, fwdKind};
				open.push({tentative, tentative, kP});
			}
		};

		// Enumerate every forward move whose endpoint is `c`.
		// Walk: predecessor = c - d (same Y).
		// Jump: fwd is p+d+up = c → p = c - d - up; cost = cfg.jumpCost.
		//   Requires headroom at p+up*2 in the forward move, i.e. c+up passable,
		//   which is already implied by standable(c) (head clearance above c).
		// Descend: fwd is p+d-up = c → p = c - d + up; cost = cfg.descendCost.
		for (auto d : DIRS) {
			relax(c - d,                            MoveKind::Walk,    1.0f);
			relax(c - d - glm::ivec3(0, 1, 0),      MoveKind::Jump,    m_cfg.jumpCost);
			relax(c - d + glm::ivec3(0, 1, 0),      MoveKind::Descend, m_cfg.descendCost);
		}
	}

	// Reconstruct one path per start by walking cameFrom forward.
	for (size_t i = 0; i < starts.size(); i++) {
		const int64_t kS = encode(starts[i]);
		auto itG = gScore.find(kS);
		if (itG == gScore.end()) {
			out[i].partial = true;   // unreachable within budget
			continue;
		}
		Path& p = out[i];
		p.cost = itG->second;
		int64_t cur = kS;
		// Safety: bound the reconstruction by the number of explored nodes
		// so a corrupt cameFrom cycle can't hang the test.
		int stepBudget = (int)gScore.size() + 1;
		while (cur != kGoal && stepBudget-- > 0) {
			auto it = cameFrom.find(cur);
			if (it == cameFrom.end()) break;
			// The forward move from `cur` goes to `it->second.prev` via kind.
			p.steps.push_back({decode(it->second.prev), it->second.kind});
			cur = it->second.prev;
		}
		// If we didn't land exactly on goal, mark partial.
		if (cur != kGoal) p.partial = true;
	}
	return out;
}

// ═════════════════════════════════════════════════════════════════════════
// pathInvalidatedBy() — [Baritone] PathExecutor#onTick: any block change
// within the planned corridor invalidates the plan. Chebyshev distance
// (L∞) is the right metric for a voxel grid with 4-cardinal moves plus
// vertical — it's the smallest ball that contains every cell that could
// affect any move-check touching a waypoint.
// ═════════════════════════════════════════════════════════════════════════
bool GridPlanner::pathInvalidatedBy(const Path& path,
                                    glm::ivec3 changedBlock) const {
	const int r = m_cfg.corridorRadius;
	for (const auto& wp : path.steps) {
		int dx = std::abs(wp.pos.x - changedBlock.x);
		int dy = std::abs(wp.pos.y - changedBlock.y);
		int dz = std::abs(wp.pos.z - changedBlock.z);
		int cheb = std::max(dx, std::max(dy, dz));
		if (cheb <= r) return true;
	}
	return false;
}

// ═════════════════════════════════════════════════════════════════════════
// PathExecutor::tick — [Baritone] PathExecutor#onTick arrival + advance.
// Doors not implemented yet (no doors in current test suite); see header
// pseudocode for the Mineflayer observe-and-interact pattern when added.
// ═════════════════════════════════════════════════════════════════════════
PathExecutor::Intent PathExecutor::tick(const glm::vec3& entityPos,
                                        const WorldView& /*world*/) {
	// Arrival threshold deliberately LARGER than ServerTuning::navArriveDistance
	// (1.2) so the executor advances to the next waypoint BEFORE the server's
	// greedy nav detects arrival and zeroes the velocity. If we waited past
	// 1.2, the entity would stall at every waypoint. Keep in perpetual motion.
	constexpr float kArriveXZ = 1.3f;
	constexpr float kArriveY  = 1.0f;

	// Advance past any waypoints we're already at (tail-recursive loop).
	while (m_cursor < (int)m_path.steps.size()) {
		const Waypoint& wp = m_path.steps[m_cursor];
		// Waypoint cell center (X,Z) + floor Y.
		glm::vec3 center{wp.pos.x + 0.5f, (float)wp.pos.y, wp.pos.z + 0.5f};
		float dx = entityPos.x - center.x;
		float dz = entityPos.z - center.z;
		float dy = entityPos.y - center.y;
		float distXZ = std::sqrt(dx*dx + dz*dz);
		if (distXZ < kArriveXZ && std::abs(dy) < kArriveY) {
			m_cursor++;
			continue;
		}
		return Intent{Intent::Move, center, {}};
	}
	return Intent{};
}

} // namespace civcraft
