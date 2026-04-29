#pragma once

// Single source of truth for the "send a Move action with services" pipeline.
// Both pathed callers (PathExecutor::driveOne) and non-pathed callers
// (Agent::sendMove from chase / attack / direct steer) route through here so
// every entity that moves gets the same separation, stuck-watchdog telemetry,
// e.velocity local prediction, and ActionProposal::Move build/send.
//
// Per CLAUDE.md Rule 7: this used to be inlined in two places. The clamp
// drifted, villagers orbited corners while tests stayed green, and we
// reorganized to make that class of bug structurally impossible.

#include "agent/separation.h"
#include "agent/pathlog.h"
#include "debug/move_stuck_log.h"
#include "logic/action.h"
#include "logic/entity.h"
#include "logic/physics.h"            // makeMoveParams, BlockSolidFn
#include "net/server_interface.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <string>

namespace solarium {

// Forward-speed guarantee — predicts next-tick movement direction after
// applySeparation and clamps it so the entity always makes meaningful
// forward progress, with bounded lateral wobble.
//
// Decomposes `velAfterSep` into (forward, lateral) along `intentDir`:
//   • forward ≥ kMinForwardFrac · intentMag (no stall, no reversal).
//   • |lateral| ≤ kMaxLateralRatio · forward  (no per-tick zigzag — even
//     if the next tick's separation pushes the OTHER way, the angle is
//     bounded so trajectory doesn't oscillate).
// Combined cap on |vel| ≤ intentMag (no speed-up).
//
// Without the lateral cap, separation can flip ±lateral every tick →
// entity zigzags around the goal at the right average speed but visibly
// circles. With it, max angle off-axis = atan(kMaxLateralRatio).
//
// Tuning (resilience over speed): kMinForwardFrac=0.85 → 3.4 m/s at
// walkSpeed=4. kMaxLateralRatio=0.4 → max ~22° off-axis per tick.
inline glm::vec3 applyForwardGuarantee(glm::vec3 velAfterSep,
                                       glm::vec2 intentDir,
                                       float intentMag,
                                       float minForwardFrac  = 0.85f,
                                       float maxLateralRatio = 0.40f) {
	float forwardC = velAfterSep.x * intentDir.x +
	                 velAfterSep.z * intentDir.y;
	const float minFwd = intentMag * minForwardFrac;
	if (forwardC < minFwd) forwardC = minFwd;

	// Lateral component (vel minus forward projection on intentDir).
	glm::vec2 lateral{
		velAfterSep.x - intentDir.x * forwardC,
		velAfterSep.z - intentDir.y * forwardC};
	const float latMag = std::sqrt(lateral.x * lateral.x +
	                               lateral.y * lateral.y);

	// Cap lateral by two constraints, take the tighter:
	//   1) |lateral| ≤ ratio · forward (no per-tick zigzag).
	//   2) |vel|² ≤ intentMag² → |lateral|² ≤ intentMag² − forward².
	const float maxLatA  = forwardC * maxLateralRatio;
	const float maxLatB2 = intentMag * intentMag - forwardC * forwardC;
	const float maxLatB  = maxLatB2 > 0.0f ? std::sqrt(maxLatB2) : 0.0f;
	const float maxLat   = std::min(maxLatA, maxLatB);
	const float latScale = (latMag > 1e-6f && latMag > maxLat)
	                       ? (maxLat / latMag) : 1.0f;

	return glm::vec3{
		intentDir.x * forwardC + lateral.x * latScale,
		velAfterSep.y,
		intentDir.y * forwardC + lateral.y * latScale};
}

// Per-entity move-emission state. One copy lives on Agent (for non-pathed
// callers), one on PathExecutor::Unit (for pathed callers). Each entity has
// only ONE active code path at a time, so the two copies don't compete —
// pathed entities have stale values on their Agent::MoveContext and vice
// versa, but neither is read in the inactive mode.
struct MoveContext {
	glm::vec2   sepDvPrev{0.0f, 0.0f};   // applySeparation LPF state
	float       stuckAccum  = 0.0f;       // seconds with intent but no displacement
	bool        stuckLogged = false;      // emit Agent-Stuck once per stuck window
	glm::vec3   stuckLastSampledPos{0, 0, 0};
	std::string goalText;                  // forwarded to ActionProposal.goalText
};

// Build + send one Move action with separation modulation + stuck telemetry.
// Mutates `vel` in place via separation, mutates `e.velocity` (local
// prediction), updates `ctx`, and emits the proposal.
//
// `jump` / `jumpVelocity` come from PathExecutor's builder-mode (jump-to-
// climb-ledges); non-pathed callers leave them at defaults. lookYaw and
// lookPitch always pass through from the entity — server's body-yaw smoother
// (smoothYawTowardsVelocity) handles visual rotation independently of the
// camera-look channel, so this is just preserving whatever the entity owner
// set elsewhere.
inline void emitMoveAction(EntityId eid, Entity& e, glm::vec3 vel,
                           ServerInterface& server, MoveContext& ctx,
                           const char* source,
                           bool  jump         = false,
                           float jumpVelocity = 0.0f) {
	// Soft separation — bias `vel` away from nearby Living and hard-stop at
	// walls. Skip the O(N) neighbor gather when the requested vel is below
	// idle; applySeparation is a no-op for idle self anyway (and clears the
	// LPF state on its own when called from this branch).
	// docs/29_ENTITY_SEPARATION.md.
	const float intentSq = vel.x * vel.x + vel.z * vel.z;
	if (intentSq > 0.04f) {
		// Capture intent direction BEFORE separation so we can guarantee a
		// minimum forward-speed component afterwards. Without this, a cluster
		// of separation pushes can cancel intent entirely (P20 in
		// test_pathfinding — N=4 stalls at 0.25 m/s without this guard).
		const float intentMag = std::sqrt(intentSq);
		const glm::vec2 intentDir{vel.x / intentMag, vel.z / intentMag};

		BlockSolidFn isSolid = server.chunks().solidFn();
		auto neighbors = gatherSepNeighbors(server, e, /*queryRadius=*/8.0f);
		SepStats stats;
		MoveParams mp = makeMoveParams(
			e.def().collision_box_min, e.def().collision_box_max,
			e.def().gravity_scale, e.def().isLiving(), /*canFly=*/false);
		vel = applySeparation(
			eid, e.position, vel,
			sepRadiusOf(e.def()),
			e.def().walk_speed > 0 ? e.def().walk_speed : 4.0f,
			sepHeightOf(e.def()),
			mp.stepHeight,
			neighbors, isSolid, ctx.sepDvPrev, SepConfig{}, &stats);
		recordSepPerf(stats);

		// Forward-speed guarantee — separation can slow you and nudge sideways,
		// but it must not stall forward progress. See applyForwardGuarantee.
		vel = applyForwardGuarantee(vel, intentDir, intentMag);
	} else {
		ctx.sepDvPrev = {0.0f, 0.0f};
	}

	PATHLOG(eid,
		"steer: source=%s vel=(%.2f,%.2f,%.2f) pos=(%.2f,%.2f,%.2f) "
		"entVel=(%.2f,%.2f,%.2f)",
		source ? source : "?",
		vel.x, vel.y, vel.z,
		e.position.x, e.position.y, e.position.z,
		e.velocity.x, e.velocity.y, e.velocity.z);

	e.velocity.x = vel.x;
	e.velocity.z = vel.z;

	// Stuck-watchdog: agent held a non-zero intent over kStuckWindow seconds
	// but the entity didn't actually displace. Logs Agent-Stuck once per
	// stuck episode, Agent-Unstuck once on recovery.
	const float intent = std::sqrt(vel.x * vel.x + vel.z * vel.z);
	const float moved  = glm::length(glm::vec2(e.position.x, e.position.z) -
	                                 glm::vec2(ctx.stuckLastSampledPos.x,
	                                           ctx.stuckLastSampledPos.z));
	constexpr float kIntentThresh = 0.2f;
	constexpr float kMoveThresh   = 0.05f;
	constexpr float kStuckWindow  = 1.5f;
	constexpr float kDt           = 1.0f / 60.0f;
	if (intent > kIntentThresh && moved < kMoveThresh) {
		ctx.stuckAccum += kDt;
		if (ctx.stuckAccum >= kStuckWindow && !ctx.stuckLogged) {
			char detail[192];
			std::snprintf(detail, sizeof(detail),
				"pos=(%.2f,%.2f,%.2f) intent=(%.2f,%.2f) goal=\"%s\" "
				"held=%.1fs",
				e.position.x, e.position.y, e.position.z,
				vel.x, vel.z, ctx.goalText.c_str(), ctx.stuckAccum);
			logMoveStuck(eid, "Agent-Stuck",
				"agent held non-zero velocity but entity failed to "
				"displace (likely server collision clamp or "
				"client/server pos delta)",
				detail);
			ctx.stuckLogged = true;
		}
	} else {
		if (ctx.stuckLogged) {
			char detail[96];
			std::snprintf(detail, sizeof(detail),
				"pos=(%.2f,%.2f,%.2f)",
				e.position.x, e.position.y, e.position.z);
			logMoveStuck(eid, "Agent-Unstuck",
				"entity resumed displacement after prior Agent-Stuck",
				detail);
		}
		ctx.stuckAccum  = 0.0f;
		ctx.stuckLogged = false;
	}
	ctx.stuckLastSampledPos = e.position;

	ActionProposal p;
	p.type         = ActionProposal::Move;
	p.actorId      = eid;
	p.desiredVel   = vel;
	p.hasClientPos = false;
	p.lookPitch    = e.lookPitch;
	p.lookYaw      = e.lookYaw;
	p.jump         = jump;
	p.jumpVelocity = jumpVelocity;
	p.goalText     = ctx.goalText;
	server.sendAction(p);
}

}  // namespace solarium
