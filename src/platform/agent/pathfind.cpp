// A* over Walk/Jump/Descend primitives. Refs: [Baritone], [Mineflayer], [HNR 1968].

#include "agent/pathfind.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace solarium {

namespace {

// 21 bits/axis → ±1,048,575 blocks.
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

// Storing g with f lets us detect stale pops without recomputing h.
struct OpenEntry {
	float   f;
	float   g;
	int64_t key;
	bool operator<(const OpenEntry& o) const { return f > o.f; }  // min-heap
};

} // namespace

// LOS for a 2-tall entity walking on the XZ plane at fixed y. Two
// independent rejections:
//   1) Body-clip: any solid cell at body y-level (y or y+1) within
//      `bodyRadius` of the line segment fails — the swept disk of the
//      entity's body would intersect that cell. Scans the bbox of the
//      segment expanded by bodyRadius and checks distance from line
//      segment to each solid cell's box. Catches "graze the wall
//      corner" cases that simple Bresenham would let through.
//   2) Floor support: every cell the Bresenham of the line segment
//      crosses must have a solid floor at y-1. The body's center
//      walks these cells; without floor it'd fall.
// bodyRadius=0 → degenerate point-particle: skip body-clip, only do
// the floor-support walk (used by tests on ideal point entities).
bool lineOfSightWalk(const WorldView& w, glm::ivec3 a, glm::ivec3 b,
                     float bodyRadius) {
	if (a.y != b.y) return false;
	const int y = a.y;

	// Endpoints in continuous coords (cell centers).
	const float ax = (float)a.x + 0.5f, az = (float)a.z + 0.5f;
	const float bx = (float)b.x + 0.5f, bz = (float)b.z + 0.5f;
	const float lx = bx - ax,            lz = bz - az;
	const float len2 = lx * lx + lz * lz;

	// 1) Body-clip rejection. Skip when bodyRadius<=0 (point particle).
	if (bodyRadius > 0.0f) {
		const int xMin = (int)std::floor(std::min(ax, bx) - bodyRadius);
		const int xMax = (int)std::floor(std::max(ax, bx) + bodyRadius);
		const int zMin = (int)std::floor(std::min(az, bz) - bodyRadius);
		const int zMax = (int)std::floor(std::max(az, bz) + bodyRadius);
		const float r2 = bodyRadius * bodyRadius;
		for (int x = xMin; x <= xMax; ++x) {
			for (int z = zMin; z <= zMax; ++z) {
				const bool bodyHit = w.isSolid({x, y,     z}) ||
				                     w.isSolid({x, y + 1, z});
				if (!bodyHit) continue;
				// Closest point on segment to cell center, then closest
				// point in cell box to that segment point.
				const float fcx = (float)x + 0.5f, fcz = (float)z + 0.5f;
				float t = (len2 > 1e-9f)
				    ? std::min(1.0f, std::max(0.0f,
				          ((fcx - ax) * lx + (fcz - az) * lz) / len2))
				    : 0.0f;
				const float px = ax + lx * t, pz = az + lz * t;
				const float qx = std::min(std::max(px, (float)x),
				                          (float)(x + 1));
				const float qz = std::min(std::max(pz, (float)z),
				                          (float)(z + 1));
				const float ddx = px - qx, ddz = pz - qz;
				if (ddx * ddx + ddz * ddz < r2) return false;
			}
		}
	}

	// 2) Floor support along the Bresenham line cells. The body walks
	// these — every one must be standable (floor solid, body+head air).
	int x0 = a.x, z0 = a.z, x1 = b.x, z1 = b.z;
	int dx = std::abs(x1 - x0), dz = std::abs(z1 - z0);
	int sx = x0 < x1 ? 1 : -1;
	int sz = z0 < z1 ? 1 : -1;
	int err = dx - dz;
	int x = x0, z = z0;
	if (!isStandable(w, {x, y, z})) return false;
	while (x != x1 || z != z1) {
		const int e2 = 2 * err;
		if (e2 > -dz) { err -= dz; x += sx; }
		if (e2 <  dx) { err += dx; z += sz; }
		if (!isStandable(w, {x, y, z})) return false;
	}
	return true;
}

// [Baritone]-style A* over Walk/Jump/Descend primitives.
Path GridPlanner::plan(glm::ivec3 start, glm::ivec3 goal) {
	// Manhattan + |dy|. Admissible under 4-cardinal expansion.
	auto H = [&](glm::ivec3 p) {
		int dx = std::abs(p.x - goal.x);
		int dy = std::abs(p.y - goal.y);
		int dz = std::abs(p.z - goal.z);
		float dHoriz = (float)(dx + dz);
		return dHoriz + (float)dy;
	};

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

		float gCur = itG->second;   // copy before gScore writes invalidate iterator

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

		const bool headroom = !m_world.isSolid({p.x, p.y + 2, p.z});
		for (auto d : CARDINAL_DIRS) {
			glm::ivec3 w = p + d;
			if (isStandable(m_world, w))
				relax(w, MoveKind::Walk, 1.0f + wallClearancePenalty(w));

			if (headroom) {
				glm::ivec3 j = p + d + glm::ivec3(0, 1, 0);
				if (isStandable(m_world, j)) relax(j, MoveKind::Jump, m_cfg.jumpCost);
			}

			glm::ivec3 dsc = p + d + glm::ivec3(0, -1, 0);
			if (isStandable(m_world, dsc)) relax(dsc, MoveKind::Descend, m_cfg.descendCost);
		}
	}

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
	std::vector<Waypoint> raw(rev.rbegin(), rev.rend());

	// LOS-Walk smoothing (any-angle string-pulling). Replaces the older
	// Collinear-Walk compression — that one only collapsed *identical*
	// XZ-direction runs, leaving L-shapes and staircases for diagonal
	// goals on open ground. This walks each maximal run of consecutive
	// Walk waypoints and keeps only the FURTHEST waypoint in the run
	// the anchor has line-of-sight to, then advances the anchor and
	// repeats. Result: a 10x10 open square produces 1 wp at the goal,
	// not a 5-corner staircase. Jump/Descend anchors are kept verbatim
	// (LOS doesn't model the jump arc — those are transitions, not
	// straight walks). Body radius corner-cut handled in lineOfSightWalk.
	out.steps.reserve(raw.size());
	const auto isWalk = [](const Waypoint& w) { return w.kind == MoveKind::Walk; };
	size_t i = 0;
	glm::ivec3 anchor = start;
	while (i < raw.size()) {
		if (!isWalk(raw[i])) {
			// Non-Walk wp: emit and re-anchor on it (next Walk run starts here).
			out.steps.push_back(raw[i]);
			anchor = raw[i].pos;
			++i;
			continue;
		}
		// Walk run [i .. j) where raw[k].kind == Walk for all i ≤ k < j.
		size_t j = i;
		while (j < raw.size() && isWalk(raw[j])) ++j;
		// Any-angle smoothing within [i, j). Greedy farthest-LOS.
		size_t cur = i;
		while (cur < j) {
			size_t best = cur;
			for (size_t k = cur + 1; k < j; ++k) {
				if (lineOfSightWalk(m_world, anchor, raw[k].pos,
				                    m_cfg.bodyRadius)) best = k;
				else break;   // LOS is monotone over a continuous Walk run on
				              // a uniform plane — once it drops, further wps
				              // along the same run are also blocked by the
				              // same obstacle, so we can early-out.
			}
			if (best == cur) {
				// LOS only to raw[cur] (the immediate next wp). Emit and step.
				out.steps.push_back(raw[cur]);
				anchor = raw[cur].pos;
				cur = cur + 1;
			} else {
				out.steps.push_back(raw[best]);
				anchor = raw[best].pos;
				cur = best + 1;
			}
		}
		i = j;
	}
	if (auto it = gScore.find(bestSeen); it != gScore.end()) out.cost = it->second;
	return out;
}

// [SupCom2 GDC 2011] reverse Dijkstra. On pop C, enumerate predecessors P whose
// forward move lands on C and relax g(P) = g(C) + fwdCost (asymmetric primitives).
std::vector<Path> GridPlanner::planBatch(const std::vector<glm::ivec3>& starts,
                                         glm::ivec3 goal) {
	std::vector<Path> out(starts.size());
	if (starts.empty()) return out;

	std::priority_queue<OpenEntry>        open;
	std::unordered_map<int64_t, float>    gScore;
	std::unordered_map<int64_t, CameFrom> cameFrom;

	const int64_t kGoal = encode(goal);
	gScore[kGoal] = 0.0f;
	open.push({0.0f, 0.0f, kGoal});   // pure Dijkstra: f = g

	std::unordered_map<int64_t, size_t> pendingStarts;
	for (size_t i = 0; i < starts.size(); i++) {
		pendingStarts.emplace(encode(starts[i]), i);
	}

	// Budget grows with batch size (20 units → 20× single-plan budget).
	const int totalBudget = m_cfg.maxNodes * (int)starts.size();
	int expanded = 0;

	while (!open.empty() && expanded < totalBudget && !pendingStarts.empty()) {
		OpenEntry cur = open.top();
		open.pop();
		auto itG = gScore.find(cur.key);
		if (itG == gScore.end() || cur.g > itG->second + 1e-6f) continue;
		expanded++;

		pendingStarts.erase(cur.key);

		glm::ivec3 c    = decode(cur.key);
		float      gCur = itG->second;

		auto relax = [&](glm::ivec3 pred, MoveKind fwdKind, float fwdCost) {
			if (!isStandable(m_world, pred)) return;
			const int64_t kP = encode(pred);
			float tentative = gCur + fwdCost;
			auto it = gScore.find(kP);
			if (it == gScore.end() || tentative < it->second - 1e-6f) {
				gScore[kP]  = tentative;
				// cameFrom[pred].prev = c (forward next step via fwdKind).
				cameFrom[kP] = {cur.key, fwdKind};
				open.push({tentative, tentative, kP});
			}
		};

		// Penalty applied to destination c (matches plan()'s forward behaviour).
		const float walkPenalty = wallClearancePenalty(c);

		// Forward moves landing on c: Walk(pred=c-d), Jump(pred=c-d-up), Descend(pred=c-d+up).
		for (auto d : CARDINAL_DIRS) {
			relax(c - d,                            MoveKind::Walk,    1.0f + walkPenalty);
			relax(c - d - glm::ivec3(0, 1, 0),      MoveKind::Jump,    m_cfg.jumpCost);
			relax(c - d + glm::ivec3(0, 1, 0),      MoveKind::Descend, m_cfg.descendCost);
		}
	}

	for (size_t i = 0; i < starts.size(); i++) {
		const int64_t kS = encode(starts[i]);
		auto itG = gScore.find(kS);
		if (itG == gScore.end()) {
			out[i].partial = true;
			continue;
		}
		Path& p = out[i];
		p.cost = itG->second;
		int64_t cur = kS;
		// Bounded reconstruction in case of a corrupt cameFrom cycle.
		int stepBudget = (int)gScore.size() + 1;
		while (cur != kGoal && stepBudget-- > 0) {
			auto it = cameFrom.find(cur);
			if (it == cameFrom.end()) break;
			p.steps.push_back({decode(it->second.prev), it->second.kind});
			cur = it->second.prev;
		}
		if (cur != kGoal) p.partial = true;
	}
	return out;
}

// Shared reverse-Dijkstra field. Avoids per-unit reconstruction at scale.
// [SupCom2 GDC 2011, Factorio, PA]
FlowField GridPlanner::planFlowField(glm::ivec3 goal,
                                     const std::vector<glm::ivec3>& startsHint) {
	FlowField field;
	field.goal = goal;

	std::priority_queue<OpenEntry>     open;
	std::unordered_map<int64_t, float> gScore;

	const int64_t kGoal = encode(goal);
	gScore[kGoal] = 0.0f;
	open.push({0.0f, 0.0f, kGoal});
	// Goal's next=goal so arrival lookups near goal can detect arrival.
	field.step[goal] = {goal, MoveKind::Walk, 0.0f};

	std::unordered_map<int64_t, size_t> pendingStarts;
	for (size_t i = 0; i < startsHint.size(); i++)
		pendingStarts.emplace(encode(startsHint[i]), i);

	// Default (no hints) covers ~50-block radius on flat ground.
	const int totalBudget = m_cfg.maxNodes * std::max((int)startsHint.size(), 4);
	int expanded = 0;
	const bool earlyExit = !startsHint.empty();

	while (!open.empty() && expanded < totalBudget) {
		if (earlyExit && pendingStarts.empty()) break;
		OpenEntry cur = open.top();
		open.pop();
		auto itG = gScore.find(cur.key);
		if (itG == gScore.end() || cur.g > itG->second + 1e-6f) continue;
		expanded++;

		pendingStarts.erase(cur.key);

		glm::ivec3 c    = decode(cur.key);
		float      gCur = itG->second;

		auto relax = [&](glm::ivec3 pred, MoveKind fwdKind, float fwdCost) {
			if (!isStandable(m_world, pred)) return;
			const int64_t kP = encode(pred);
			float tentative = gCur + fwdCost;
			auto it = gScore.find(kP);
			if (it == gScore.end() || tentative < it->second - 1e-6f) {
				gScore[kP]  = tentative;
				field.step[pred] = {c, fwdKind, tentative};
				open.push({tentative, tentative, kP});
			}
		};

		const float walkPenalty = wallClearancePenalty(c);

		for (auto d : CARDINAL_DIRS) {
			relax(c - d,                            MoveKind::Walk,    1.0f + walkPenalty);
			relax(c - d - glm::ivec3(0, 1, 0),      MoveKind::Jump,    m_cfg.jumpCost);
			relax(c - d + glm::ivec3(0, 1, 0),      MoveKind::Descend, m_cfg.descendCost);
		}
	}

	return field;
}

// Chebyshev is the smallest ball covering every cell a move-check touches.
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

// PathExecutor::tick and Navigator impls moved to client/path_executor.cpp.
// Only the planner-side (GridPlanner, FlowField, pathInvalidatedBy) lives
// here now.

} // namespace solarium
