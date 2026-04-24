// Unified PathExecutor — per-entity pop-front consumer + RTS group commands.
// See client/path_executor.h for the design rationale.

#include "client/path_executor.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>

namespace civcraft {

// ─── Per-entity primitive ──────────────────────────────────────────────────

void PathExecutor::setPath(EntityId eid, Path p) {
	Unit& u   = m_units[eid];
	u.path    = std::move(p);
	u.waitOpen = false;
	u.arrived  = false;
}

void PathExecutor::cancel(EntityId eid) {
	m_units.erase(eid);
	if (m_units.empty()) {
		m_pending.reset();
		m_builderMode = false;
	}
}

bool PathExecutor::done(EntityId eid) const {
	auto it = m_units.find(eid);
	if (it == m_units.end()) return true;
	return it->second.path.steps.empty();
}

const Path& PathExecutor::path(EntityId eid) const {
	static const Path kEmpty{};
	auto it = m_units.find(eid);
	if (it == m_units.end()) return kEmpty;
	return it->second.path;
}

bool PathExecutor::reached(const Waypoint& w, const glm::vec3& pos) const {
	glm::vec3 c = centerOf(w);
	float dx = pos.x - c.x;
	float dy = pos.y - c.y;
	float dz = pos.z - c.z;
	if (std::abs(dy) >= kArriveY) return false;
	return (dx * dx + dz * dz) < (kArriveRadius * kArriveRadius);
}

// Pop-front cell-consumer. Each tick, retire any waypoint the entity has
// either (a) entered the arrive ring of, or (b) projected past along the
// segment to the next cell. Condition (b) — the half-plane retire — rescues
// the executor from a stall when physics overshoot or an out-of-band shove
// (collision push-out, clientPos snap-back, teleport) lands the entity
// outside every remaining cell's arrive ring. Without (b), the front cell
// ends up behind the entity and the next Move target points backward.
//
// Reached-check is intentionally simple and symmetric: any cell the entity
// is currently inside (or past) is "done" and gets popped. Unlike the old
// cursor model, a stale front can't strand the drive target on a cell that's
// behind us — as soon as we've passed it, it's gone.
//
// Door cells block the pop: we need to emit Interact and let the server
// open it before stepping through. The wait-open flag is cleared when the
// cell behind us finally pops (door handshake complete).
PathExecutor::Intent PathExecutor::tick(EntityId eid,
                                        const glm::vec3& entityPos) {
	auto it = m_units.find(eid);
	if (it == m_units.end()) return Intent{};
	Unit& u = it->second;

	while (!u.path.steps.empty()) {
		const Waypoint& front = u.path.steps.front();
		if (m_doors && m_doors->isClosedDoor(front.pos)) break;

		bool pop = reached(front, entityPos);
		if (!pop && u.path.steps.size() >= 2) {
			// Half-plane retire: have we projected past `front` along the
			// segment `front → next`? Only tests when both cells share a y-
			// plane — a y-change (Jump/Descend) needs feet alignment, not
			// projection, so stick to the arrive-ring check there.
			const Waypoint& next = u.path.steps[1];
			if (next.pos.y == front.pos.y) {
				glm::vec3 fc = centerOf(front);
				glm::vec3 nc = centerOf(next);
				float sx = nc.x - fc.x;
				float sz = nc.z - fc.z;
				float rx = entityPos.x - fc.x;
				float rz = entityPos.z - fc.z;
				// Strict > 0: exactly on the plane is still "at the cell,"
				// let the arrive-ring handle it next tick.
				if (sx * rx + sz * rz > 0.0f) pop = true;
			}
		}
		if (!pop) break;

		u.path.steps.erase(u.path.steps.begin());
		u.waitOpen = false;
	}

	if (u.path.steps.empty()) return Intent{};

	const Waypoint& front = u.path.steps.front();
	glm::vec3 center = centerOf(front);

	// Door at the front: approach → Interact inside reach → wait for open →
	// step through (the pop loop above will retire the cell next tick once
	// the door flips to open).
	if (m_doors && m_doors->isClosedDoor(front.pos)) {
		float cdx = entityPos.x - center.x;
		float cdz = entityPos.z - center.z;
		float curDistXZ = std::sqrt(cdx * cdx + cdz * cdz);
		if (curDistXZ < kDoorReachXZ) {
			u.waitOpen = true;
			Intent i;
			i.kind        = Intent::Interact;
			i.target      = center;
			i.interactPos = front.pos;
			return i;
		}
		return Intent{Intent::Move, center, {}};
	}
	if (u.waitOpen) {
		if (m_doors && m_doors->isOpenDoor(front.pos)) u.waitOpen = false;
		else return Intent{Intent::Move, center, {}};
	}

	return Intent{Intent::Move, center, {}};
}

// ─── Group commands (RTS) ──────────────────────────────────────────────────

void PathExecutor::planGroup(const std::vector<EntityId>& entityIds,
                             const std::vector<glm::ivec3>& starts,
                             glm::ivec3 goal,
                             ChunkSource& chunks, const BlockRegistry& blocks,
                             CommandKind kind) {
	if (entityIds.empty()) return;
	using clk = std::chrono::steady_clock;
	auto t0 = clk::now();

	ClientChunkWorldView view(chunks, blocks);
	auto slots = buildFormationGoals(goal, (int)entityIds.size(), view);
	auto t1 = clk::now();

	// Reset bookkeeping for the new command. Existing units not in this
	// group keep their state; units in the group get a fresh Unit with the
	// formation slot filled in.
	for (size_t i = 0; i < entityIds.size(); ++i) {
		Unit& u = m_units[entityIds[i]];
		u.path           = {};
		u.waitOpen       = false;
		u.arrived        = false;
		u.formationSlot  = slots[i];
		u.builder        = {};
	}
	m_kind        = kind;
	m_builderMode = false;

	// packaged_task + detached thread: ~future() must not block on new
	// commands mid-plan (std::async(launch::async) would).
	auto state          = std::make_shared<PendingPlan>();
	state->eids         = entityIds;
	state->starts       = starts;
	state->slots        = slots;
	state->goal         = goal;
	state->t0           = t0;
	state->setupMs      = std::chrono::duration<double, std::milli>(t1 - t0).count();
	state->kind         = kind;

	std::packaged_task<std::vector<Path>()> task(
		[&chunks, &blocks, slots, starts]() {
			ClientChunkWorldView v(chunks, blocks);
			GridPlanner planner(v);
			// planBatch: one A*-over-reverse-Dijkstra sweep, O(1) reconstruction
			// per start. N=1 is fine; still runs on the worker thread.
			std::vector<Path> out;
			out.reserve(starts.size());
			for (size_t i = 0; i < starts.size(); ++i) {
				glm::ivec3 perUnitGoal = i < slots.size() ? slots[i] : slots.back();
				out.push_back(planner.plan(starts[i], perUnitGoal));
			}
			return out;
		});
	state->future = task.get_future();
	std::thread(std::move(task)).detach();
	m_pending = std::move(state);

	std::printf("[RTS-CLIENT] plan-kickoff N=%zu goal=(%d,%d,%d) kind=%s "
	            "setup=%.2fms (paths running async)\n",
	            entityIds.size(), goal.x, goal.y, goal.z,
	            kind == CommandKind::Build ? "BUILD" : "walk",
	            m_pending->setupMs);
}

void PathExecutor::poll() {
	if (!m_pending) return;
	if (m_pending->future.wait_for(std::chrono::milliseconds(0))
	    != std::future_status::ready) return;

	auto paths = m_pending->future.get();
	auto t2    = std::chrono::steady_clock::now();
	double totalMs = std::chrono::duration<double, std::milli>(
		t2 - m_pending->t0).count();

	int reachable = 0;
	for (const auto& p : paths) if (!p.steps.empty() && !p.partial) reachable++;

	if (reachable == 0) {
		bool enterBuilder = kExperimentalPathBuilder
		                    && m_pending->kind == CommandKind::Build;
		std::printf("[RTS-CLIENT] NO PATH to (%d,%d,%d) — %zu units unreachable "
		            "(async=%.2fms) → %s\n",
		            m_pending->goal.x, m_pending->goal.y, m_pending->goal.z,
		            m_pending->eids.size(), totalMs,
		            enterBuilder ? "BUILDER MODE" : "cancel");
		if (enterBuilder) {
			// Naive steering straight at the slot; builder stuck-analysis
			// fires per-unit when it can't progress.
			m_builderMode = true;
			for (size_t i = 0; i < m_pending->eids.size(); ++i) {
				Unit& u = m_units[m_pending->eids[i]];
				u.path           = {};  // no A* path; drive toward slot directly
				u.formationSlot  = m_pending->slots[i];
			}
			m_pending.reset();
			return;
		}
		cancelAll();
		return;
	}

	// Install each reachable unit's path. Unreachable units are cancelled
	// (dropped from m_units) so driveRemote stops emitting Moves for them.
	std::vector<EntityId> drop;
	for (size_t i = 0; i < m_pending->eids.size(); ++i) {
		EntityId eid = m_pending->eids[i];
		const Path& p = paths[i];
		if (p.steps.empty()) { drop.push_back(eid); continue; }
		Unit& u = m_units[eid];
		u.path     = p;
		u.waitOpen = false;
	}
	for (auto eid : drop) m_units.erase(eid);

	std::printf("[RTS-CLIENT] plan-ready N=%zu goal=(%d,%d,%d) "
	            "reach=%d/%zu | async-total=%.2fms\n",
	            m_pending->eids.size(), m_pending->goal.x, m_pending->goal.y,
	            m_pending->goal.z, reachable, m_pending->eids.size(), totalMs);

	m_pending.reset();
}

std::optional<glm::vec3> PathExecutor::steerTargetFor(EntityId eid,
                                                     const glm::vec3& pos) {
	auto it = m_units.find(eid);
	if (it == m_units.end()) return std::nullopt;
	Unit& u = it->second;
	if (u.arrived) return std::nullopt;

	// No path installed yet (async still running, or builder-mode naive drive).
	// Steer directly to the formation slot when known; otherwise nothing.
	if (u.path.steps.empty()) {
		if (u.formationSlot.x == INT_MIN) return std::nullopt;
		glm::vec3 slotC{u.formationSlot.x + 0.5f,
		                (float)u.formationSlot.y,
		                u.formationSlot.z + 0.5f};
		float dx = pos.x - slotC.x;
		float dz = pos.z - slotC.z;
		if (dx * dx + dz * dz < kArriveRadius * kArriveRadius) {
			u.arrived = true;
			return std::nullopt;
		}
		return slotC;
	}

	Intent intent = tick(eid, pos);
	if (intent.kind == Intent::None) {
		u.arrived = true;
		return std::nullopt;
	}
	return intent.target;
}

void PathExecutor::driveRemote(ServerInterface& server, EntityId excludedId,
                               float walkSpeedFallback, float jumpVelocity) {
	poll();

	ClientChunkWorldView view(server.chunks(), server.blockRegistry());
	std::vector<EntityId> done;
	for (auto& [eid, u] : m_units) {
		if (eid == excludedId) continue;
		Entity* e = server.getEntity(eid);
		if (!e) { done.push_back(eid); continue; }

		auto steer = steerTargetFor(eid, e->position);
		if (!steer) { done.push_back(eid); continue; }

		float walkSpeed = e->def().walk_speed;
		if (walkSpeed <= 0) walkSpeed = walkSpeedFallback;

		glm::vec3 d = *steer - e->position;
		d.y = 0;
		float len = std::sqrt(d.x * d.x + d.z * d.z);
		if (len < 0.001f) continue;
		d /= len;

		bool wantsJump = false;
		if (m_builderMode) wantsJump = builderTickFor(eid, e->position, d, view);

		ActionProposal move;
		move.type         = ActionProposal::Move;
		move.actorId      = eid;
		move.desiredVel   = {d.x * walkSpeed, 0, d.z * walkSpeed};
		move.hasClientPos = false;
		move.lookPitch    = e->lookPitch;
		move.lookYaw      = e->lookYaw;
		move.jump         = wantsJump;
		move.jumpVelocity = jumpVelocity;
		server.sendAction(move);
	}
	for (auto eid : done) cancel(eid);
}

void PathExecutor::cancelAll() {
	m_units.clear();
	m_pending.reset();
	m_builderMode = false;
}

std::optional<glm::ivec3> PathExecutor::formationSlot(EntityId eid) const {
	auto it = m_units.find(eid);
	if (it == m_units.end()) return std::nullopt;
	if (it->second.formationSlot.x == INT_MIN) return std::nullopt;
	return it->second.formationSlot;
}

// ─── Builder mode (Phase 1: climb 1-block ledges) ──────────────────────────

bool PathExecutor::builderTickFor(EntityId eid, const glm::vec3& pos,
                                  const glm::vec3& dirXZ,
                                  const WorldView& view) {
	Unit& u = m_units[eid];
	auto& bs = u.builder;

	glm::ivec3 cell{
		(int)std::floor(pos.x),
		(int)std::floor(pos.y),
		(int)std::floor(pos.z)};
	if (cell == bs.lastCell) bs.stuckTicks++;
	else { bs.stuckTicks = 0; bs.lastCell = cell; }

	glm::ivec3 step{0, 0, 0};
	if (std::abs(dirXZ.x) >= std::abs(dirXZ.z))
		step.x = dirXZ.x > 0 ? 1 : -1;
	else
		step.z = dirXZ.z > 0 ? 1 : -1;
	glm::ivec3 ahead      = cell + step;
	glm::ivec3 aheadHead  = ahead + glm::ivec3(0, 1, 0);
	glm::ivec3 aheadHead2 = ahead + glm::ivec3(0, 2, 0);

	bool ahead_solid      = view.isSolid(ahead);
	bool aheadHead_solid  = view.isSolid(aheadHead);
	bool aheadHead2_solid = view.isSolid(aheadHead2);

	bool jumpable = ahead_solid && !aheadHead_solid && !aheadHead2_solid;
	bool blocked  = ahead_solid || aheadHead_solid;
	if (!blocked) return false;

	if (jumpable) {
		if (bs.stuckTicks % 10 == 1) {   // ~1 Hz log throttle
			std::printf("[RTS-BUILDER] eid=%llu jump at (%d,%d,%d) → (%d,%d,%d)\n",
			            (unsigned long long)eid, cell.x, cell.y, cell.z,
			            ahead.x, ahead.y, ahead.z);
		}
		return true;
	}

	if (bs.stuckTicks >= m_builderCfg.stuckTicksThresh && !bs.phase2Logged) {
		std::printf("[RTS-BUILDER] eid=%llu STUCK at (%d,%d,%d) — "
		            "Phase 2 (gather+stack) not yet implemented\n",
		            (unsigned long long)eid, cell.x, cell.y, cell.z);
		bs.phase2Logged = true;
	}
	return false;
}

// ─── Formation ─────────────────────────────────────────────────────────────
// cols×rows grid, each cell snapped to nearest standable Y (±4) so slopes
// aren't unreachable. Copied verbatim from the retired RtsExecutor — the
// slot allocation is independent of how we drive units toward slots.

std::vector<glm::ivec3> PathExecutor::buildFormationGoals(
		glm::ivec3 clicked, int n, const WorldView& view) {
	std::vector<glm::ivec3> out;
	out.reserve(n);
	if (n == 1) { out.push_back(clicked); return out; }

	int cols    = (int)std::ceil(std::sqrt((float)n));
	const int spacing = 2;
	int offX    = (cols - 1) * spacing / 2;
	int rows    = (n + cols - 1) / cols;
	int offZ    = (rows - 1) * spacing / 2;

	auto standable = [&](glm::ivec3 p) {
		if (!view.isSolid({p.x, p.y - 1, p.z})) return false;
		if ( view.isSolid(p))                   return false;
		if ( view.isSolid({p.x, p.y + 1, p.z})) return false;
		return true;
	};
	auto snapY = [&](glm::ivec3 p) -> glm::ivec3 {
		for (int dy = 0; dy <= 4; dy++) {
			glm::ivec3 up{p.x, p.y + dy, p.z};
			if (standable(up)) return up;
			glm::ivec3 dn{p.x, p.y - dy, p.z};
			if (standable(dn)) return dn;
		}
		return p;
	};

	for (int i = 0; i < n; i++) {
		int gx = clicked.x + (i % cols) * spacing - offX;
		int gz = clicked.z + (i / cols) * spacing - offZ;
		out.push_back(snapY({gx, clicked.y, gz}));
	}
	return out;
}

// ─── Navigator ─────────────────────────────────────────────────────────────

Navigator::Navigator(const WorldView& world, const DoorOracle* doors)
	: m_world(world), m_doors(doors), m_planner(world), m_exec(doors) {}

void Navigator::clear() {
	m_exec.cancel(kSelfEid);
	m_hasGoal = false;
	m_status  = Status::Idle;
	m_failureReason.clear();
}

// Plan-cache policy: one plan per goal cell. Setting an identical goal is a
// no-op so behaviors can re-assert the destination every decide() tick
// without paying A* cost. A Failed goal clears hasGoal, so callers that
// re-assert the same coord after failure go through a fresh plan.
bool Navigator::setGoal(glm::ivec3 g) {
	if (m_hasGoal && g == m_goal && !m_exec.done(kSelfEid)) return true;
	m_goal    = g;
	m_hasGoal = true;
	m_status  = Status::Planning;
	m_failureReason.clear();

	// Defer the plan to the first tick where entityPos is known. tick()
	// computes start cell then calls planner.plan() synchronously.
	m_path = {};
	m_exec.cancel(kSelfEid);
	return true;
}

namespace {
std::string fmt_coord(glm::ivec3 p) {
	return "(" + std::to_string(p.x) + ", "
	           + std::to_string(p.y) + ", "
	           + std::to_string(p.z) + ")";
}
}  // namespace

Navigator::Step Navigator::tick(const glm::vec3& entityPos) {
	Step out;
	if (!m_hasGoal) { m_status = Status::Idle; return out; }

	// Lazy plan: first tick after setGoal. Plan once — if empty, or if the
	// executor drains a partial path, the caller sees Failed + reason and
	// handles it (go home to complain, drop item, idle). We don't replan.
	// A partial path is still executed for goals inside solid blocks
	// (chests, monuments, any "interact-with" target) — the planner can
	// never stand on the goal cell, so partial=true is the norm. Walking
	// to the best-seen standable neighbor is what the caller wants; they
	// check range on arrival and issue Interact/Store/etc.
	if (m_path.steps.empty()) {
		glm::ivec3 start{
			(int)std::floor(entityPos.x),
			(int)std::floor(entityPos.y),
			(int)std::floor(entityPos.z)};
		// Sub-cell-edge correction: physics permits an entity to straddle a
		// cell boundary — its AABB sits mostly in the adjacent air cell while
		// floor(pos) lands on the solid block it's brushing against. A* would
		// then plan *from inside the block* and find no standable neighbors.
		// If the computed feet cell is solid, snap to the adjacent cell whose
		// center is closest to the entity's actual XZ position, preferring a
		// *standable* cell (floor-solid + body/head air) so A* can immediately
		// expand neighbors. Falls back to any air cell if none stand, and to
		// the original start if every candidate is solid (let A* report Failed
		// with a coherent reason).
		if (m_world.isSolid(start)) {
			auto standable = [&](glm::ivec3 p) {
				return  m_world.isSolid({p.x, p.y - 1, p.z}) &&
				       !m_world.isSolid(p) &&
				       !m_world.isSolid({p.x, p.y + 1, p.z});
			};
			float fx = entityPos.x - (float)start.x;
			float fz = entityPos.z - (float)start.z;
			int   dx = (fx >= 0.5f) ? +1 : -1;
			int   dz = (fz >= 0.5f) ? +1 : -1;
			glm::ivec3 cands[3] = {
				{start.x + dx, start.y, start.z     },
				{start.x,      start.y, start.z + dz},
				{start.x + dx, start.y, start.z + dz},
			};
			glm::ivec3 bestAir = start;
			bool       haveAir = false;
			for (auto c : cands) {
				if (standable(c))                   { start = c; haveAir = true; break; }
				if (!haveAir && !m_world.isSolid(c)){ bestAir = c; haveAir = true; }
			}
			if (m_world.isSolid(start) && haveAir) start = bestAir;
		}
		m_path = m_planner.plan(start, m_goal);
		if (m_path.steps.empty()) {
			m_status        = Status::Failed;
			m_failureReason = "no path from " + fmt_coord(start)
			                + " to " + fmt_coord(m_goal);
			m_hasGoal = false;
			return out;
		}
		m_exec.setPath(kSelfEid, m_path);
	}

	if (m_exec.done(kSelfEid)) {
		if (m_path.partial) {
			m_status        = Status::Failed;
			m_failureReason = "unreachable " + fmt_coord(m_goal);
		} else {
			m_status = Status::Arrived;
		}
		m_hasGoal = false;
		return out;
	}

	auto intent = m_exec.tick(kSelfEid, entityPos);
	if (intent.kind == PathExecutor::Intent::Move) {
		m_status       = Status::Walking;
		out.kind       = Step::Move;
		out.moveTarget = intent.target;
	} else if (intent.kind == PathExecutor::Intent::Interact) {
		m_status        = Status::OpeningDoor;
		out.kind        = Step::Interact;
		out.moveTarget  = intent.target;
		out.interactPos = intent.interactPos;
	} else {
		m_status  = Status::Arrived;
		m_hasGoal = false;
	}
	return out;
}

}  // namespace civcraft
