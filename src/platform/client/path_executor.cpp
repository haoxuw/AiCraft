// Unified PathExecutor — per-entity pop-front consumer + RTS group commands.
// See client/path_executor.h for the design rationale.

#include "client/path_executor.h"

#include "debug/perf_registry.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>

namespace solarium {

std::atomic<int> g_pathConvergenceFailures{0};

// ─── Per-entity primitive ──────────────────────────────────────────────────

void PathExecutor::setPath(EntityId eid, Path p) {
	Unit& u   = m_units[eid];
	u.path    = std::move(p);
	u.waitOpen     = false;
	u.arrived      = false;
	u.stallTicks   = 0;
	u.stallLastPos = glm::vec3(-1e9f);   // sentinel — first tick treats as "moved"
	u.slideLockDir = glm::ivec3(0);
	u.interactCooldown = 0;
	u.passedDoors.clear();
	u.hasPrevPos   = false;              // lazy-init prevPos on first tick
	u.lastFrontWp        = glm::ivec3(INT_MIN, INT_MIN, INT_MIN);
	u.minDistToFront     = 1e30f;
	u.nonConvergeTicks   = 0;
	u.hasLastConvergePos = false;
	u.hasLastPopPos      = false;
	u.ticksSinceLastPop  = 0;
	u.hasLastTickPos     = false;
	u.arcLengthInSeg     = 0.0f;
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

// Segment-crossing pop: a waypoint retires when the entity's last-tick XZ
// movement carried it past the perpendicular plane through the cell center
// (within a corridor slack), OR it's already standing on the center within
// kSnapRadius. Multi-tick snap-back / push-out / teleport: a single long
// segment can cross several waypoints in a row — the while-loop in tick()
// pops them one at a time using this same predicate, no full-path scan.
//
// Why this replaces magnet-ring: with the old kMagnetRadius=0.15 a fast
// turn-rate-capped entity could orbit the cell at radius v/ω > 0.15 forever
// (the magnet was smaller than the minimum turning radius). Crossing a plane
// doesn't require entering any ring — overshoot still counts as reached.
bool PathExecutor::passedThisTick(const glm::vec3& prev,
                                  const glm::vec3& pos,
                                  const Waypoint& w) {
	if (std::abs(pos.y - (float)w.pos.y) >= kArriveY) return false;
	const float wx = (float)w.pos.x + 0.5f;
	const float wz = (float)w.pos.z + 0.5f;
	// Standing-on-it case (also catches first tick where seg=0).
	const float pdx = pos.x - wx, pdz = pos.z - wz;
	if (pdx * pdx + pdz * pdz < kSnapRadius * kSnapRadius) return true;
	// Project this tick's segment (prev → pos) onto the line from prev to wp.
	// Reached iff projection covers the full distance to wp AND perpendicular
	// offset stays inside the corridor slack.
	const float toWpX = wx - prev.x;
	const float toWpZ = wz - prev.z;
	const float toWpLen2 = toWpX * toWpX + toWpZ * toWpZ;
	if (toWpLen2 < 1e-6f) return true;       // prev was already on wp
	const float toWpLen  = std::sqrt(toWpLen2);
	const float segX = pos.x - prev.x;
	const float segZ = pos.z - prev.z;
	const float seg2 = segX * segX + segZ * segZ;
	if (seg2 < 1e-8f) return false;          // no movement, no crossing
	const float proj = (segX * toWpX + segZ * toWpZ) / toWpLen;   // along (prev→wp)
	if (proj < toWpLen) return false;        // didn't reach the perpendicular plane
	const float perp2 = seg2 - proj * proj;  // squared perpendicular offset
	return perp2 < kCorridorSlack * kCorridorSlack;
}

// If a straight line to the waypoint would clip a solid ±X/±Z neighbour,
// deflect toward the open cardinal cell whose offset best projects onto
// the desired heading. Hysteresis keeps the chosen side sticky so we
// don't flicker. All 4 blocked → leave target alone; stall pop handles it.
glm::vec3 PathExecutor::slideAroundObstacle(const glm::vec3& pos,
                                            glm::vec3 target,
                                            Unit& u) const {
	if (!m_world) return target;

	float dx = target.x - pos.x;
	float dz = target.z - pos.z;
	float len2 = dx * dx + dz * dz;
	if (len2 < 1e-6f) return target;

	const glm::ivec3 feet{
		(int)std::floor(pos.x),
		(int)std::floor(pos.y),
		(int)std::floor(pos.z)};
	auto wallCol = [&](glm::ivec3 c) {
		return m_world->isSolid(c) ||
		       m_world->isSolid({c.x, c.y + 1, c.z});
	};

	// Obstructed = the AABB edge + lookahead has crossed the plane of an
	// adjacent solid cell along either axis of desired travel.
	bool obstructed = false;
	if (dx > 0 && pos.x + kSlideRadius + kSlideLookahead >= (float)(feet.x + 1)
	           && wallCol({feet.x + 1, feet.y, feet.z})) obstructed = true;
	else if (dx < 0 && pos.x - kSlideRadius - kSlideLookahead <= (float)feet.x
	           && wallCol({feet.x - 1, feet.y, feet.z})) obstructed = true;
	if (!obstructed) {
		if (dz > 0 && pos.z + kSlideRadius + kSlideLookahead >= (float)(feet.z + 1)
		           && wallCol({feet.x, feet.y, feet.z + 1})) obstructed = true;
		else if (dz < 0 && pos.z - kSlideRadius - kSlideLookahead <= (float)feet.z
		           && wallCol({feet.x, feet.y, feet.z - 1})) obstructed = true;
	}
	if (!obstructed) { u.slideLockDir = glm::ivec3(0); return target; }

	// Score each standable cardinal neighbour by its projection onto the
	// desired direction; add the hysteresis bonus to whichever side we
	// committed to last tick so we stick with it unless another side is a
	// significantly better alignment with the new heading.
	float len = std::sqrt(len2);
	float idx = dx / len, idz = dz / len;
	glm::ivec3 bestOff{0, 0, 0};
	float      bestScore = -2.0f;
	for (auto o : CARDINAL_DIRS) {
		if (!isStandable(*m_world, feet + o)) continue;
		float score = (float)o.x * idx + (float)o.z * idz;
		if (o == u.slideLockDir) score += kSlideHysteresis;
		if (score > bestScore) { bestScore = score; bestOff = o; }
	}
	if (bestOff == glm::ivec3(0)) return target;   // boxed in — stall pop handles it
	u.slideLockDir = bestOff;
	return { pos.x + (float)bestOff.x * len,
	         target.y,
	         pos.z + (float)bestOff.z * len };
}

// BFS the connected door cluster horizontally from `seed`. Same Y plane,
// 4-cardinal neighbours; the server's resolveInteractAction fans the
// resulting toggle vertically through the door pillar (dy ±8). Cap
// keeps the search bounded.
//
// `wantClosed=true`: walks the closed-door cluster (used when emitting an
// open Interact — caller knows seed is closed).
// `wantClosed=false`: walks the open-door cluster (used when popping a
// passed waypoint that's currently isOpenDoor — caller wants the full
// cluster recorded into passedDoors).
std::vector<glm::ivec3> PathExecutor::findConnectedDoorSlabs(
		glm::ivec3 seed, bool wantClosed) const {
	std::vector<glm::ivec3> out;
	if (!m_doors) return out;
	auto isPart = [&](glm::ivec3 c) {
		return wantClosed ? m_doors->isClosedDoor(c) : m_doors->isOpenDoor(c);
	};
	if (!isPart(seed)) return out;            // seed must be of the right type
	out.push_back(seed);
	size_t head = 0;
	while (head < out.size() && (int)out.size() < kDoorClusterMaxCells) {
		glm::ivec3 c = out[head++];
		const glm::ivec3 nbrs[4] = {
			{c.x + 1, c.y, c.z}, {c.x - 1, c.y, c.z},
			{c.x, c.y, c.z + 1}, {c.x, c.y, c.z - 1}};
		for (auto n : nbrs) {
			if (!isPart(n)) continue;
			bool seen = false;
			for (auto p : out) if (p == n) { seen = true; break; }
			if (!seen) out.push_back(n);
			if ((int)out.size() >= kDoorClusterMaxCells) break;
		}
	}
	return out;
}

// Append `cluster` to passedDoors with dedup + FIFO cap. Used by the pop
// loop when retiring a waypoint that's currently an open door cell.
void PathExecutor::recordPassedDoors(
		Unit& u, const std::vector<glm::ivec3>& cluster) const {
	for (auto c : cluster) {
		bool seen = false;
		for (auto p : u.passedDoors) if (p == c) { seen = true; break; }
		if (seen) continue;
		if ((int)u.passedDoors.size() >= kMaxPassedDoors) {
			u.passedDoors.erase(u.passedDoors.begin());   // FIFO drop oldest
		}
		u.passedDoors.push_back(c);
	}
}

// ─── tick() phase helpers ──────────────────────────────────────────────────
// Each helper either short-circuits with an Intent (auto-close door, door
// scan probe, front-door handshake) or mutates Unit state and returns void/
// bool. tick() is now a flat orchestration. PERF_SCOPE per helper surfaces
// in `make perf_fps` under the path.executor.* prefix.

// Auto-close: once we're ≥ kDoorCloseDistance from every slab we walked
// through, toggle them shut. Re-check isOpenDoor so a second entity walking
// through doesn't get its door slammed by stale state. Politeness gate: if
// another entity is within kDoorPolitenessRadius of the doorway, defer —
// keep passedDoors set so we retry next tick once they've moved.
std::optional<PathExecutor::Intent> PathExecutor::stepAutoCloseDoors(
		EntityId eid, Unit& u, const glm::vec3& entityPos) {
	PERF_SCOPE("path.executor.auto_close_doors_ms");
	if (u.passedDoors.empty() || u.interactCooldown != 0) return std::nullopt;

	float minDist2 = 1e30f;
	for (auto& d : u.passedDoors) {
		float dx = entityPos.x - ((float)d.x + 0.5f);
		float dz = entityPos.z - ((float)d.z + 0.5f);
		float r2 = dx * dx + dz * dz;
		if (r2 < minDist2) minDist2 = r2;
	}
	if (minDist2 <= kDoorCloseDistance * kDoorCloseDistance) return std::nullopt;

	bool blocked = m_entities && m_entities->entityNearAny(
		u.passedDoors, kDoorPolitenessRadius, eid);
	if (blocked) return std::nullopt;

	std::vector<glm::ivec3> stillOpen;
	if (m_doors) {
		for (auto& d : u.passedDoors)
			if (m_doors->isOpenDoor(d)) stillOpen.push_back(d);
	}
	u.passedDoors.clear();
	if (stillOpen.empty()) return std::nullopt;

	u.interactCooldown = kInteractCooldownTicks;
	Intent i;
	i.kind        = Intent::Interact;
	i.target      = {(float)stillOpen[0].x + 0.5f,
	                 (float)stillOpen[0].y,
	                 (float)stillOpen[0].z + 0.5f};
	i.interactPos = std::move(stillOpen);
	return i;
}

// Stall detector — two thresholds. kDoorScanTicks signals "probe forward for
// a hidden closed door"; kStallTicks pops the front waypoint outright.
// Returns true iff stall has reached the door-scan threshold this tick (the
// caller invokes stepDoorScanProbe in that case).
bool PathExecutor::stepDetectStall(Unit& u, const glm::vec3& entityPos) {
	PERF_SCOPE("path.executor.detect_stall_ms");
	if (u.path.steps.empty()) return false;

	float sdx = entityPos.x - u.stallLastPos.x;
	float sdz = entityPos.z - u.stallLastPos.z;
	if (sdx * sdx + sdz * sdz > kStallRadius * kStallRadius) {
		u.stallTicks   = 0;
		u.stallLastPos = entityPos;
		return false;
	}
	if (++u.stallTicks >= kStallTicks) {
		u.path.steps.erase(u.path.steps.begin());
		u.stallTicks   = 0;
		u.stallLastPos = entityPos;
		u.waitOpen     = false;
		u.slideLockDir = glm::ivec3(0);
		return false;
	}
	return u.stallTicks >= kDoorScanTicks;
}

// Doors are 2-block columns — probe feet and head height of the dominant-
// axis forward cell. Collinear-Walk path compression can drop closed-door
// waypoints inside straight corridors; this fires when we've stalled and
// the front waypoint check can't see the wall in our face.
std::optional<PathExecutor::Intent> PathExecutor::stepDoorScanProbe(
		Unit& u, const glm::vec3& entityPos) {
	PERF_SCOPE("path.executor.door_scan_probe_ms");
	if (!m_doors || u.interactCooldown != 0 || u.path.steps.empty())
		return std::nullopt;

	glm::vec3 toward = centerOf(u.path.steps.front()) - entityPos;
	toward.y = 0;
	glm::ivec3 step{0, 0, 0};
	if (std::abs(toward.x) >= std::abs(toward.z))
		step.x = toward.x >= 0 ? 1 : -1;
	else
		step.z = toward.z >= 0 ? 1 : -1;
	glm::ivec3 feet{
		(int)std::floor(entityPos.x),
		(int)std::floor(entityPos.y),
		(int)std::floor(entityPos.z)};
	for (int dy = 0; dy <= 1; ++dy) {
		glm::ivec3 probe = feet + step + glm::ivec3(0, dy, 0);
		if (m_doors->isClosedDoor(probe)) {
			u.waitOpen         = true;
			u.interactCooldown = kInteractCooldownTicks;
			Intent i;
			i.kind        = Intent::Interact;
			i.target      = {probe.x + 0.5f, (float)probe.y, probe.z + 0.5f};
			i.interactPos = findConnectedDoorSlabs(probe, /*wantClosed=*/true);
			// We just opened these — record so we close them behind us.
			recordPassedDoors(u, i.interactPos);
			return i;
		}
	}
	return std::nullopt;
}

// Convergence watchdog — see Unit::nonConvergeTicks. Runs once per real
// tick (deduped: tick() is called twice per frame on the NPC path because
// driveTick → driveOne → steerTargetFor → tick. The second call has the
// same entityPos and is skipped.)
//
// Invariant: distance to centerOf(front) must reach a new minimum within
// kConvergeStallTicks of the front becoming the front. Orbits, sweep-past
// curves, and overshoot-without-recovery all violate this and are flagged
// even though stall-pop (position-stagnation) wouldn't catch them — those
// pathologies have the entity moving plenty, just not toward the target.
//
// Test hook: every flag bumps g_pathConvergenceFailures (always-on atomic).
// Headless tests assert it stays at 0 over a known-good scenario; if it
// climbs, the orbit/overshoot bug is back.
void PathExecutor::stepConvergenceWatchdog(EntityId eid, Unit& u,
                                           const glm::vec3& entityPos) {
	PERF_SCOPE("path.executor.convergence_watchdog_ms");

	// Dedupe: tick() runs twice per frame on the NPC path. Same entityPos
	// on both calls — skip the second.
	if (u.hasLastConvergePos
	    && u.lastConvergePos.x == entityPos.x
	    && u.lastConvergePos.z == entityPos.z) return;
	u.lastConvergePos    = entityPos;
	u.hasLastConvergePos = true;

	if (u.path.steps.empty()) {
		u.lastFrontWp      = glm::ivec3(INT_MIN, INT_MIN, INT_MIN);
		u.minDistToFront   = 1e30f;
		u.nonConvergeTicks = 0;
		return;
	}

	const glm::ivec3 frontPos = u.path.steps.front().pos;
	if (frontPos != u.lastFrontWp) {
		u.lastFrontWp      = frontPos;
		u.minDistToFront   = 1e30f;
		u.nonConvergeTicks = 0;
	}
	const glm::vec3 frontC = centerOf(u.path.steps.front());
	const float fdx = entityPos.x - frontC.x;
	const float fdz = entityPos.z - frontC.z;
	const float fdist = std::sqrt(fdx * fdx + fdz * fdz);
	if (fdist + 1e-3f < u.minDistToFront) {
		u.minDistToFront   = fdist;
		u.nonConvergeTicks = 0;
	} else {
		u.nonConvergeTicks++;
	}

#ifdef SOLARIUM_PATHFINDING_DEBUG
	if (u.nonConvergeTicks > kConvergeStallTicks) {
		std::fprintf(stderr,
			"[CONV-FAIL] eid=%llu front=(%d,%d,%d) dist=%.2f minDist=%.2f "
			"stag=%d ticks (orbit/overshoot — distance not improving)\n",
			(unsigned long long)eid, frontPos.x, frontPos.y, frontPos.z,
			fdist, u.minDistToFront, u.nonConvergeTicks);
		g_pathConvergenceFailures.fetch_add(1, std::memory_order_relaxed);
		// Reset to avoid log spam — caller decides whether the increment is fatal.
		u.minDistToFront   = fdist;
		u.nonConvergeTicks = 0;
	}
#else
	(void)eid;
	if (u.nonConvergeTicks > kConvergeStallTicks) {
		g_pathConvergenceFailures.fetch_add(1, std::memory_order_relaxed);
		u.minDistToFront   = fdist;
		u.nonConvergeTicks = 0;
	}
#endif
}

// Pop-front consumer. While-loop on the front using passedThisTick(prev,pos):
// pops one waypoint per iteration, stops on the first one that hasn't been
// crossed. Multi-cell teleport / snap-back drains many in one tick. Closed
// doors halt the loop — entry is gated by the Interact handshake. When a
// popped cell is currently isOpenDoor, BFS the open cluster around it and
// record into passedDoors so anyone (not just the opener) closes behind.
void PathExecutor::stepPopReached(EntityId eid, Unit& u,
                                  const glm::vec3& entityPos) {
	PERF_SCOPE("path.executor.pop_reached_ms");

	// Per-segment timing + arc accumulator — increment once per real frame
	// (deduped against intra-frame double-tick via lastConvergePos, which
	// the watchdog sets in the SAME tick AFTER us, so on the second call
	// this frame it'll already match entityPos).
	const bool freshFrame = !u.hasLastConvergePos
	    || u.lastConvergePos.x != entityPos.x
	    || u.lastConvergePos.z != entityPos.z;
	if (freshFrame) {
		if (!u.hasLastPopPos) {
			u.lastPopPos    = entityPos;
			u.hasLastPopPos = true;
		}
		// Arc length: sum of |Δpos| across ticks within this segment.
		// arc/direct ratio reveals orbits — straight Walk = ~1, orbit > 3.
		if (u.hasLastTickPos) {
			const float adx = entityPos.x - u.lastTickPos.x;
			const float adz = entityPos.z - u.lastTickPos.z;
			u.arcLengthInSeg += std::sqrt(adx * adx + adz * adz);
		}
		u.lastTickPos    = entityPos;
		u.hasLastTickPos = true;
		u.ticksSinceLastPop++;
	}

	const glm::vec3 prev = u.hasPrevPos ? u.prevPos : entityPos;
	int popsLeft = kMaxPopsPerTick;
	while (popsLeft-- > 0 && !u.path.steps.empty()) {
		const Waypoint& front = u.path.steps.front();
		// Don't auto-pop through a closed door — handshake gates it.
		if (m_doors && m_doors->isClosedDoor(front.pos)) break;

		const bool crossed = passedThisTick(prev, entityPos, front);
		// Predict-arrival pop: retire the front while still ε short of its
		// center. Without this, a curved approach can miss the segment-cross
		// predicate entirely and orbit until stall-pop fires (~0.5s).
		// Y must be within kArriveY (Jump/Descend slack) so we don't pop
		// mid-air on a Jump.
		bool predictPop = false;
		if (!crossed && std::abs(entityPos.y - (float)front.pos.y) < kArriveY) {
			const float pdx = entityPos.x - ((float)front.pos.x + 0.5f);
			const float pdz = entityPos.z - ((float)front.pos.z + 0.5f);
			predictPop =
				(pdx * pdx + pdz * pdz) <
				kPredictArrivalRadius * kPredictArrivalRadius;
		}
		if (!crossed && !predictPop) break;
		// Record passed open-door cluster before erasing.
		if (m_doors && m_doors->isOpenDoor(front.pos)) {
			recordPassedDoors(u,
				findConnectedDoorSlabs(front.pos, /*wantClosed=*/false));
		}
		// Per-segment receipt before erasing the wp from the front.
		// `dist` = direct displacement from last pop (or path start).
		// `arc`  = total |Δpos| accumulated tick-by-tick across the segment.
		// `ratio` = arc / dist; ~1 = straight, >3 = orbit / heavy deflection.
		const float dx = entityPos.x - u.lastPopPos.x;
		const float dz = entityPos.z - u.lastPopPos.z;
		const float dist = std::sqrt(dx * dx + dz * dz);
		const float secs = u.ticksSinceLastPop * (1.0f / 60.0f);
		const float speed = secs > 0.0f ? dist / secs : 0.0f;
		const float ratio = dist > 0.01f ? (u.arcLengthInSeg / dist) : 0.0f;
		PATHLOG(eid,
			"seg: wp=(%d,%d,%d) %s dist=%.2fm arc=%.2fm ratio=%.2f "
			"ticks=%d speed=%.2fm/s%s",
			front.pos.x, front.pos.y, front.pos.z, toString(front.kind),
			dist, u.arcLengthInSeg, ratio,
			u.ticksSinceLastPop, speed,
			predictPop ? " [snap]" : "");

		u.lastPopPos        = entityPos;
		u.ticksSinceLastPop = 0;
		u.arcLengthInSeg    = 0.0f;

		u.path.steps.erase(u.path.steps.begin());
		u.waitOpen = false;
	}
}

// Closed door at the front: approach → Interact inside reach → wait for open
// → next tick's pop loop retires the cell once the door flips. Cooldown
// guards against re-toggling across the BlockChange broadcast lag.
std::optional<PathExecutor::Intent> PathExecutor::stepFrontDoorHandshake(
		Unit& u, const glm::vec3& entityPos) {
	PERF_SCOPE("path.executor.front_door_handshake_ms");
	if (u.path.steps.empty()) return std::nullopt;
	const Waypoint& front = u.path.steps.front();
	if (!m_doors || !m_doors->isClosedDoor(front.pos)) return std::nullopt;

	glm::vec3 center = centerOf(front);
	float cdx = entityPos.x - center.x;
	float cdz = entityPos.z - center.z;
	float curDistXZ = std::sqrt(cdx * cdx + cdz * cdz);
	if (curDistXZ < kDoorReachXZ) {
		u.waitOpen = true;
		if (u.interactCooldown == 0) {
			u.interactCooldown = kInteractCooldownTicks;
			Intent i;
			i.kind        = Intent::Interact;
			i.target      = center;
			i.interactPos = findConnectedDoorSlabs(front.pos, /*wantClosed=*/true);
			recordPassedDoors(u, i.interactPos);
			return i;
		}
	}
	// Either inside reach but in cooldown, or still walking up to it —
	// either way emit a Move toward center, no slide deflection.
	return Intent{Intent::Move, center, {}};
}

// Final move-target computation for the front waypoint. Wall-slide only on
// Walk hops; Jump/Descend need precise y-alignment and cross air cells the
// cardinal-neighbour probes would misread as obstacles.
glm::vec3 PathExecutor::stepComputeMoveTarget(Unit& u, const glm::vec3& entityPos) {
	PERF_SCOPE("path.executor.slide_obstacle_ms");
	const Waypoint& front = u.path.steps.front();
	glm::vec3 center = centerOf(front);
	if (front.kind == MoveKind::Walk)
		center = slideAroundObstacle(entityPos, center, u);
	else
		u.slideLockDir = glm::ivec3(0);
	return center;
}

// Per-tick orchestration. Order matters:
//   1. Cooldown decrement.
//   2. Auto-close passed doors (may emit Interact-close).
//   3. Stall detect → door-scan probe (may emit Interact-open for hidden door).
//   4. Pop reached waypoints (segment-crossing predicate, while-loop).
//   5. Front-door handshake if the new front is a closed door.
//   6. Wait-open guard if we already triggered an open and the door is still shut.
//   7. Compute Move target with slide deflection.
// prevPos is updated unconditionally at the end so next tick's pop predicate
// always sees exactly one tick of motion (even when this tick short-circuits
// to an Interact).
PathExecutor::Intent PathExecutor::tick(EntityId eid,
                                        const glm::vec3& entityPos) {
	PERF_SCOPE("path.executor.tick_ms");
	auto it = m_units.find(eid);
	if (it == m_units.end()) return Intent{};
	Unit& u = it->second;

	if (u.interactCooldown > 0) --u.interactCooldown;

	auto finish = [&](Intent out) -> Intent {
		u.prevPos    = entityPos;
		u.hasPrevPos = true;
		return out;
	};

	if (auto i = stepAutoCloseDoors(eid, u, entityPos)) return finish(*i);

	bool stallTripped = stepDetectStall(u, entityPos);
	if (stallTripped) {
		if (auto i = stepDoorScanProbe(u, entityPos)) return finish(*i);
	}

	stepPopReached(eid, u, entityPos);
	stepConvergenceWatchdog(eid, u, entityPos);
	if (u.path.steps.empty()) return finish(Intent{});

	if (auto i = stepFrontDoorHandshake(u, entityPos)) return finish(*i);

	if (u.waitOpen) {
		const Waypoint& front = u.path.steps.front();
		if (m_doors && m_doors->isOpenDoor(front.pos)) u.waitOpen = false;
		else return finish(Intent{Intent::Move, centerOf(front), {}});
	}

	return finish(Intent{Intent::Move, stepComputeMoveTarget(u, entityPos), {}});
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

	ChunkWorldView view(chunks, blocks);
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
			ChunkWorldView v(chunks, blocks);
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

// Single per-entity-per-tick path-execution routine. The ONE place all
// pathed-movement emission happens. Calls tick() once, dispatches Move /
// Interact / Stop based on the returned Intent. Returns the Intent so
// callers can read the kind for status tracking.
//
// Adding a new caller? Don't reinvent the predicate, clamp, or emission;
// just call driveOne and check the returned Intent.kind.
PathExecutor::Intent PathExecutor::driveOne(EntityId eid,
                                            ServerInterface& server,
                                            float walkSpeedFallback,
                                            float jumpVelocity) {
	auto it = m_units.find(eid);
	if (it == m_units.end()) return Intent{};
	Unit& u = it->second;

	Entity* e = server.getEntity(eid);
	if (!e) return Intent{};

	float walkSpeed = e->def().walk_speed;
	if (walkSpeed <= 0) walkSpeed = walkSpeedFallback;

	auto sendStop = [&]() {
		ActionProposal stop;
		stop.type         = ActionProposal::Move;
		stop.actorId      = eid;
		stop.desiredVel   = {0, 0, 0};
		stop.lookPitch    = e->lookPitch;
		stop.lookYaw      = e->lookYaw;
		stop.goalText     = u.moveCtx.goalText;
		server.sendAction(stop);
	};

	// Run the predicate / pop / convergence-watchdog / segment-timing pass.
	Intent intent = tick(eid, e->position);

	// ── Slot-fallback (RTS async-plan-pending / builder naive drive) ──
	// Path is empty but the unit has a formation slot — steer naively toward
	// the slot until A* lands or builder phase 2 takes over. Skips when
	// already arrived (kArriveRadius round the slot).
	if (intent.kind == Intent::None
	    && u.formationSlot.x != INT_MIN && !u.arrived) {
		const glm::vec3 slotC{u.formationSlot.x + 0.5f,
		                      (float)u.formationSlot.y,
		                      u.formationSlot.z + 0.5f};
		const float sdx = slotC.x - e->position.x;
		const float sdz = slotC.z - e->position.z;
		const float sdist = std::sqrt(sdx * sdx + sdz * sdz);
		if (sdist < kArriveRadius) {
			u.arrived = true;
			sendStop();
			return Intent{};
		}
		const glm::vec3 dir = {sdx / sdist, 0.0f, sdz / sdist};
		bool wantsJump = false;
		if (m_builderMode) {
			ChunkWorldView view(server.chunks(), server.blockRegistry());
			wantsJump = builderTickFor(eid, e->position, dir, view);
		}
		const glm::vec3 vel = {dir.x * walkSpeed, 0.0f, dir.z * walkSpeed};
		emitMoveAction(eid, *e, vel, server, u.moveCtx, "drive-slot",
		               wantsJump, jumpVelocity);
		return Intent{Intent::Move, slotC, {}};
	}

	if (intent.kind == Intent::Move) {
		// Velocity is the RAW direction toward the (slide-deflected) target.
		// Body yaw is decoupled — server's smoothYawTowardsVelocity (20 rad/s,
		// logic/physics.h) lerps Entity::yaw toward vel direction, so a 90°
		// corner produces a "strafe" (entity walks sideways while the body
		// model catches up). Same mechanism player RPG-mode uses.
		//
		// Speed clamp: hard one-tick backstop. Without smoothing there's no
		// curve to fit inside any slack — just don't overshoot in one tick.
		// Skipped on Jump/Descend (ballistic arc needs full XZ velocity).
		glm::vec3 d = intent.target - e->position;
		d.y = 0;
		const float len = std::sqrt(d.x * d.x + d.z * d.z);
		if (len < 0.001f) return intent;
		d /= len;
		const bool isJumpOrDescend = !u.path.steps.empty()
		    && u.path.steps.front().kind != MoveKind::Walk;
		float clamped = walkSpeed;
		if (!isJumpOrDescend) {
			constexpr float kServerDt = 1.0f / 60.0f;
			clamped = std::min(walkSpeed, len / kServerDt);
		}
		bool wantsJump = false;
		if (m_builderMode) {
			ChunkWorldView view(server.chunks(), server.blockRegistry());
			wantsJump = builderTickFor(eid, e->position, d, view);
		}
		const glm::vec3 vel = {d.x * clamped, 0.0f, d.z * clamped};
		emitMoveAction(eid, *e, vel, server, u.moveCtx, "drive",
		               wantsJump, jumpVelocity);
		return intent;
	}

	if (intent.kind == Intent::Interact) {
		// One ActionProposal::Interact per connected door slab — server fans
		// the toggle vertically through the door pillar; we cover the
		// horizontal cluster.
		for (auto pos : intent.interactPos) {
			ActionProposal p;
			p.type          = ActionProposal::Interact;
			p.actorId       = eid;
			p.blockPos      = pos;
			p.appearanceIdx = -1;     // legacy toggle (door)
			server.sendAction(p);
		}
		// Halt while the door swings — next tick's pop loop retires the
		// door cell once it flips open.
		sendStop();
		return intent;
	}

	// Intent::None — path complete or no path/slot. Send a stop so the
	// server doesn't keep applying any prior desired velocity.
	sendStop();
	return intent;
}

void PathExecutor::driveRemote(ServerInterface& server, EntityId excludedId,
                               float walkSpeedFallback, float jumpVelocity) {
	poll();
	std::vector<EntityId> done;
	for (auto& [eid, u] : m_units) {
		(void)u;
		if (eid == excludedId) continue;
		Intent intent = driveOne(eid, server, walkSpeedFallback, jumpVelocity);
		// None means: no path AND no slot (or arrived at slot). Drop tracking.
		if (intent.kind == Intent::None) done.push_back(eid);
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

	auto snapY = [&](glm::ivec3 p) -> glm::ivec3 {
		for (int dy = 0; dy <= 4; dy++) {
			glm::ivec3 up{p.x, p.y + dy, p.z};
			if (isStandable(view, up)) return up;
			glm::ivec3 dn{p.x, p.y - dy, p.z};
			if (isStandable(view, dn)) return dn;
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

Navigator::Navigator(EntityId eid, const WorldView& world, const DoorOracle* doors)
	: m_eid(eid), m_world(world), m_doors(doors), m_planner(world), m_exec(doors) {
	m_exec.setWorldView(&world);
}

void Navigator::clear() {
	m_exec.cancel(m_eid);
	m_hasGoal = false;
	m_status  = Status::Idle;
	m_failureReason.clear();
}

// Plan-cache policy: one plan per goal cell. Setting an identical goal is a
// no-op so behaviors can re-assert the destination every decide() tick
// without paying A* cost. A Failed goal clears hasGoal, so callers that
// re-assert the same coord after failure go through a fresh plan.
bool Navigator::setGoal(glm::ivec3 g) {
	if (m_hasGoal && g == m_goal && !m_exec.done(m_eid)) return true;
	m_goal    = g;
	m_hasGoal = true;
	m_status  = Status::Planning;
	m_failureReason.clear();

	// Defer the plan to the first tick where entityPos is known. tick()
	// computes start cell then calls planner.plan() synchronously.
	m_path = {};
	m_exec.cancel(m_eid);
	return true;
}

namespace {
std::string fmt_coord(glm::ivec3 p) {
	return "(" + std::to_string(p.x) + ", "
	           + std::to_string(p.y) + ", "
	           + std::to_string(p.z) + ")";
}
}  // namespace

// Lazy plan with sub-cell-edge correction. Called by driveTick on the first
// tick after setGoal so the start cell is fresh against the actual position.
// Returns true on success (path installed in m_exec); false on planner give-
// up (caller sets m_status=Failed and the failure reason).
bool Navigator::lazyPlanIfNeeded(const glm::vec3& entityPos) {
	if (!m_path.steps.empty()) return true;
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
			if (isStandable(m_world, c))        { start = c; haveAir = true; break; }
			if (!haveAir && !m_world.isSolid(c)){ bestAir = c; haveAir = true; }
		}
		if (m_world.isSolid(start) && haveAir) start = bestAir;
	}
	m_path = m_planner.plan(start, m_goal);
	if (m_path.steps.empty()) {
		m_failureReason = "no path from " + fmt_coord(start)
		                + " to " + fmt_coord(m_goal);
		return false;
	}
	m_exec.setPath(m_eid, m_path);
	return true;
}

// Single per-frame entry for NPC pathed movement. ALL emission (Move,
// Interact, Stop) lives in PathExecutor::driveOne — Navigator just owns
// the lazy-plan + status-from-Intent translation layer. RTS (driveRemote)
// and NPC (this) hit driveOne identically; same predicate, same clamp,
// same emit.
Navigator::Step Navigator::driveTick(Entity& e, ServerInterface& server,
                                     const std::string& goalText,
                                     float walkSpeedFallback,
                                     float jumpVelocity) {
	Step out;
	if (!m_hasGoal) { m_status = Status::Idle; return out; }

	if (!lazyPlanIfNeeded(e.position)) {
		m_status  = Status::Failed;
		m_hasGoal = false;
		return out;
	}

	// Detect "executor drained the path" before we drive — that's an arrival
	// (or Failed if the original plan was partial / goal in solid block).
	if (m_exec.done(m_eid)) {
		if (m_path.partial) {
			m_status        = Status::Failed;
			m_failureReason = "unreachable " + fmt_coord(m_goal);
		} else {
			m_status = Status::Arrived;
		}
		m_hasGoal = false;
		return out;
	}

	// Forward goalText to the Unit's MoveContext so emitMoveAction puts it
	// on the ActionProposal. Same field Agent::sendMove sets via its own
	// MoveContext for non-pathed callers — single source of truth.
	if (m_exec.has(m_eid)) {
		m_exec.unitMoveCtx(m_eid).goalText = goalText;
	}

	// Drive: tick + emit (Move / Interact / Stop) all happen inside driveOne.
	PathExecutor::Intent intent =
		m_exec.driveOne(m_eid, server, walkSpeedFallback, jumpVelocity);

	// Translate Intent → Navigator::Status + Step for the agent's layer.
	if (intent.kind == PathExecutor::Intent::Move) {
		m_status       = Status::Walking;
		out.kind       = Step::Move;
		out.moveTarget = intent.target;
	} else if (intent.kind == PathExecutor::Intent::Interact) {
		m_status        = Status::OpeningDoor;
		out.kind        = Step::Interact;
		out.moveTarget  = intent.target;
		out.interactPos = std::move(intent.interactPos);
	} else {
		// Path drained mid-driveOne (final waypoint just popped).
		m_status  = m_path.partial ? Status::Failed : Status::Arrived;
		m_hasGoal = false;
	}
	return out;
}

}  // namespace solarium
