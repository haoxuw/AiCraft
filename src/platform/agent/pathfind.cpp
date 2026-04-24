// A* over Walk/Jump/Descend primitives. Refs: [Baritone], [Mineflayer], [HNR 1968].

#include "agent/pathfind.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace civcraft {

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

	// Entity is 2 cells tall: floor solid, body + head air.
	auto standable = [&](glm::ivec3 p) {
		if (!m_world.isSolid({p.x, p.y - 1, p.z})) return false;
		if ( m_world.isSolid(p))                   return false;
		if ( m_world.isSolid({p.x, p.y + 1, p.z})) return false;
		return true;
	};

	static const glm::ivec3 DIRS[4] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};

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

		// Wall-clearance cost bump: prefer routes with breathing room.
		auto wallPenalty = [&](glm::ivec3 c) {
			if (m_cfg.wallClearancePenalty <= 0) return 0.0f;
			int walls = 0;
			for (auto nd : DIRS) if (m_world.isSolid(c + nd)) walls++;
			return m_cfg.wallClearancePenalty * (float)walls;
		};

		const bool headroom = !m_world.isSolid({p.x, p.y + 2, p.z});
		for (auto d : DIRS) {
			glm::ivec3 w = p + d;
			if (standable(w)) relax(w, MoveKind::Walk, 1.0f + wallPenalty(w));

			if (headroom) {
				glm::ivec3 j = p + d + glm::ivec3(0, 1, 0);
				if (standable(j)) relax(j, MoveKind::Jump, m_cfg.jumpCost);
			}

			glm::ivec3 dsc = p + d + glm::ivec3(0, -1, 0);
			if (standable(dsc)) relax(dsc, MoveKind::Descend, m_cfg.descendCost);
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

	// Collinear-Walk compression. A 56-block straight corridor ships 1 waypoint
	// (the end), not 56. Rule: keep wp[i] iff it is the last, or its kind is
	// not Walk, or the next step's kind is not Walk (preserve the "takeoff"
	// anchor before a Jump/Descend), or the XZ step direction changes from the
	// preceding to the following step. PathExecutor now pops from the front
	// and drives straight to the next kept center — fewer cells ⇒ no cursor
	// jitter + readable F3 viz.
	out.steps.reserve(raw.size());
	for (size_t i = 0; i < raw.size(); ++i) {
		if (i + 1 == raw.size()) { out.steps.push_back(raw[i]); continue; }
		const Waypoint& w  = raw[i];
		const Waypoint& nx = raw[i + 1];
		if (w.kind  != MoveKind::Walk) { out.steps.push_back(w); continue; }
		if (nx.kind != MoveKind::Walk) { out.steps.push_back(w); continue; }
		// Compare incoming vs outgoing XZ direction. "Incoming" for i==0 uses
		// `start` as the anchor so the very first cell only survives when it
		// changes heading (usually it doesn't — drop it).
		glm::ivec3 pv = (i == 0) ? start : raw[i - 1].pos;
		int inX  = w.pos.x  - pv.x,  inZ  = w.pos.z  - pv.z;
		int outX = nx.pos.x - w.pos.x, outZ = nx.pos.z - w.pos.z;
		if (inX != outX || inZ != outZ) { out.steps.push_back(w); continue; }
		// collinear Walk run — drop this step
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
			if (!standable(pred)) return;
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
		float walkPenalty = 0.0f;
		if (m_cfg.wallClearancePenalty > 0) {
			int walls = 0;
			for (auto nd : DIRS) if (m_world.isSolid(c + nd)) walls++;
			walkPenalty = m_cfg.wallClearancePenalty * (float)walls;
		}

		// Forward moves landing on c: Walk(pred=c-d), Jump(pred=c-d-up), Descend(pred=c-d+up).
		for (auto d : DIRS) {
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

	auto standable = [&](glm::ivec3 p) {
		if (!m_world.isSolid({p.x, p.y - 1, p.z})) return false;
		if ( m_world.isSolid(p))                   return false;
		if ( m_world.isSolid({p.x, p.y + 1, p.z})) return false;
		return true;
	};

	static const glm::ivec3 DIRS[4] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};

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
			if (!standable(pred)) return;
			const int64_t kP = encode(pred);
			float tentative = gCur + fwdCost;
			auto it = gScore.find(kP);
			if (it == gScore.end() || tentative < it->second - 1e-6f) {
				gScore[kP]  = tentative;
				field.step[pred] = {c, fwdKind, tentative};
				open.push({tentative, tentative, kP});
			}
		};

		float walkPenalty = 0.0f;
		if (m_cfg.wallClearancePenalty > 0) {
			int walls = 0;
			for (auto nd : DIRS) if (m_world.isSolid(c + nd)) walls++;
			walkPenalty = m_cfg.wallClearancePenalty * (float)walls;
		}

		for (auto d : DIRS) {
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

} // namespace civcraft
