// A* over Walk/Jump/Descend primitives. Refs: [Baritone], [Mineflayer], [HNR 1968].

#include "agent/pathfind.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
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
	out.steps.assign(rev.rbegin(), rev.rend());
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

PathExecutor::Intent PathExecutor::tick(const glm::vec3& entityPos,
                                        const WorldView& /*world*/,
                                        const DoorOracle* doors) {
	// Waypoint arrive tolerance. Block-center snap means we never need sub-block
	// precision; 1.3 keeps us loose enough to skip through diagonal seams without
	// ping-ponging between adjacent cells.
	constexpr float kArriveXZ = 1.3f;
	constexpr float kArriveY  = 1.0f;
	// Fire the Interact once the door cell is within this ring, so a large-gait
	// entity doesn't overshoot and wedge against the closed leaf.
	constexpr float kDoorReachXZ = 2.0f;
	// How far ahead along a straight corridor we aim. Agent::kArriveEps=1.5
	// triggers a 30s idle-hold when a Move target lands within 1.5 blocks,
	// so returning the immediate next waypoint (distXZ 1.3–~2.3) stalls the
	// entity for 30s between every 1-block hop. Aiming at the far end of a
	// straight, door-free, flat run keeps Move.dist well outside kArriveEps
	// and lets the server's collide-and-slide physics walk continuously
	// between decide()s.
	constexpr int kLookAhead = 6;

	// Advance cursor past any already-arrived waypoints.
	while (m_cursor < (int)m_path.steps.size()) {
		const Waypoint& wp = m_path.steps[m_cursor];
		glm::vec3 center{wp.pos.x + 0.5f, (float)wp.pos.y, wp.pos.z + 0.5f};
		float dx = entityPos.x - center.x;
		float dz = entityPos.z - center.z;
		float dy = entityPos.y - center.y;
		if (std::sqrt(dx*dx + dz*dz) < kArriveXZ && std::abs(dy) < kArriveY) {
			m_cursor++;
			continue;
		}
		break;
	}
	if (m_cursor >= (int)m_path.steps.size()) return Intent{};

	const Waypoint& cur = m_path.steps[m_cursor];
	glm::vec3 curCenter{cur.pos.x + 0.5f, (float)cur.pos.y, cur.pos.z + 0.5f};
	float cdx = entityPos.x - curCenter.x;
	float cdz = entityPos.z - curCenter.z;
	float curDistXZ = std::sqrt(cdx*cdx + cdz*cdz);

	// Door gating at the *current* cursor cell (never look past a door).
	if (doors && doors->isClosedDoor(cur.pos)) {
		if (curDistXZ < kDoorReachXZ) {
			m_waitOpen = true;
			Intent i;
			i.kind = Intent::Interact;
			i.target = curCenter;
			i.interactPos = cur.pos;
			return i;
		}
		return Intent{Intent::Move, curCenter, {}};
	}
	if (m_waitOpen) {
		if (doors && doors->isOpenDoor(cur.pos)) {
			m_waitOpen = false;
		} else {
			return Intent{Intent::Move, curCenter, {}};
		}
	}

	// Look-ahead along a straight, flat, door-free run. Stop at any direction
	// change, Y change, or door (open or closed) so the server's straight-line
	// velocity doesn't cut through walls or skip a door-open handshake.
	int targetIdx = m_cursor;
	glm::ivec2 runDir{0, 0};
	for (int k = 1; k < kLookAhead; ++k) {
		int next = m_cursor + k;
		if (next >= (int)m_path.steps.size()) break;
		const Waypoint& np = m_path.steps[next];
		const Waypoint& pv = m_path.steps[next - 1];
		if (np.pos.y != pv.pos.y) break;
		if (doors && (doors->isClosedDoor(np.pos) || doors->isOpenDoor(np.pos))) break;
		glm::ivec2 step{np.pos.x - pv.pos.x, np.pos.z - pv.pos.z};
		if (runDir.x == 0 && runDir.y == 0) runDir = step;
		else if (step != runDir) break;
		targetIdx = next;
	}

	const Waypoint& target = m_path.steps[targetIdx];
	glm::vec3 center{target.pos.x + 0.5f, (float)target.pos.y, target.pos.z + 0.5f};
	return Intent{Intent::Move, center, {}};
}

// ── Navigator ──────────────────────────────────────────────────────────────
// Plan-cache policy: one plan per goal cell. Setting an identical goal is a
// no-op so behaviors can re-assert the destination every decide() tick
// without paying A* cost.
bool Navigator::setGoal(glm::ivec3 g) {
	if (m_hasGoal && g == m_goal && !m_exec.done()) return m_status != Status::Blocked;
	m_goal = g;
	m_hasGoal = true;
	m_status = Status::Planning;

	// Feet cell of the entity at plan time is unknown here — caller passes
	// start via setGoal is awkward. We defer planning to the first tick where
	// we know entityPos. Store only the goal; tick() computes start cell.
	m_path = {};
	m_exec.clear();
	return true;
}

Navigator::Step Navigator::tick(const glm::vec3& entityPos) {
	Step out;
	if (!m_hasGoal) { m_status = Status::Idle; return out; }

	// Lazy plan: first tick after setGoal, or after Blocked/invalidation.
	// A partial path is still executed — for goals inside solid blocks (chests,
	// monuments, any "interact-with" target) the planner can never stand on
	// the goal cell, so partial=true is the norm. Walking to the best-seen
	// standable neighbor is what the caller actually wants; they check range
	// on arrival and issue Interact/Store/etc. Only empty paths block.
	if (m_path.steps.empty()) {
		glm::ivec3 start{
			(int)std::floor(entityPos.x),
			(int)std::floor(entityPos.y),
			(int)std::floor(entityPos.z)};
		m_path = m_planner.plan(start, m_goal);
		if (m_path.steps.empty()) {
			m_status = Status::Blocked;
			return out;
		}
		m_exec.setPath(m_path);
	}

	if (m_exec.done()) {
		m_status = Status::Arrived;
		m_hasGoal = false;
		return out;
	}

	auto intent = m_exec.tick(entityPos, m_world, m_doors);
	if (intent.kind == PathExecutor::Intent::Move) {
		m_status = Status::Walking;
		out.kind = Step::Move;
		out.moveTarget = intent.target;
	} else if (intent.kind == PathExecutor::Intent::Interact) {
		m_status = Status::OpeningDoor;
		out.kind = Step::Interact;
		out.moveTarget  = intent.target;
		out.interactPos = intent.interactPos;
	} else {
		m_status = Status::Arrived;
		m_hasGoal = false;
	}
	return out;
}

} // namespace civcraft
