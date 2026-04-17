#pragma once

#include "logic/entity.h"
#include "logic/entity_physics.h"
#include "logic/physics.h"
#include "client/game_logger.h"
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

namespace civcraft {

// Owns client-side smoothing of remote entity movement between 20Hz server
// broadcasts. Pulled out of NetworkServer so that file stays focused on
// networking.
//
// Design: server is authoritative. For every NON-local entity the client
// mirrors server state directly — position = server.pos + server.vel * age.
// No client-side gravity, no client-side collision. Any local physics here
// would eventually drift (gravity vs. slightly-different collision grid)
// and was the cause of the persistent ~1m Y-gap.
// For the LOCAL PLAYER, client-side prediction in gameplay runs
// moveAndCollide and typed input drives position; we then gently pull that
// client-predicted position back to the server's authoritative value.
class EntityReconciler {
public:
	void onEntityCreate(EntityId id, glm::vec3 pos, glm::vec3 vel, float yaw,
	                    glm::vec3 moveTarget, float moveSpeed) {
		m_targets[id] = {pos, vel, yaw, 0.0f, moveTarget, moveSpeed};
	}

	void onEntityUpdate(EntityId id, glm::vec3 pos, glm::vec3 vel, float yaw,
	                    glm::vec3 moveTarget, float moveSpeed) {
		auto& t = m_targets[id];
		t.prevServerPos   = t.position;
		t.prevInitialized = true;
		t.position   = pos;
		t.velocity   = vel;
		t.yaw        = yaw;
		t.age        = 0.0f;
		t.moveTarget = moveTarget;
		t.moveSpeed  = moveSpeed;
	}

	void onEntityRemove(EntityId id) { m_targets.erase(id); }

	// Server-authoritative position (from the latest broadcast) or fallback
	// entity position when no target exists yet.
	glm::vec3 getServerPosition(EntityId id, glm::vec3 fallback) const {
		auto it = m_targets.find(id);
		return (it != m_targets.end()) ? it->second.position : fallback;
	}

	// Run physics + reconciliation for all tracked entities.
	// serverSilent=true means NetworkServer hasn't received ANY message in
	// kStaleThreshold seconds — only then do we flag entities red.
	// (A single entity silently falling out of perception scope is normal and
	// shouldn't light up a red lightbulb: the server is still healthy, that
	// entity just stepped past the 64-block view range.)
	void tick(float dt, EntityId localPlayerId,
	          std::unordered_map<EntityId, std::unique_ptr<Entity>>& entities,
	          const BlockSolidFn& solidFn,
	          bool serverSilent) {
		for (auto& [id, target] : m_targets) {
			auto it = entities.find(id);
			if (it == entities.end()) continue;
			Entity& e = *it->second;

			bool stale = (target.age > kStaleThreshold);
			bool realError = stale && serverSilent;
			if (realError && !e.hasError) {
				e.hasError = true;
				e.goalText = "⚠ stale (server silent)";
				std::printf("[Reconciler] eid=%u stale+silent (age=%.2fs) — frozen+red\n",
				            id, target.age);
				std::fflush(stdout);
			} else if (!realError && e.hasError && (!stale || !serverSilent)) {
				e.hasError = false;
			}
			target.age += dt;
			if (stale) continue;  // freeze in place regardless — no fresh data

			if (id == localPlayerId) {
				// Client predicts own motion; reconcile closes any divergence.
				reconcileToServer(id, e, target, dt);
			} else {
				// Remote entity: mirror the server directly. Extrapolate by
				// target.velocity * age to hide 20Hz broadcast stagger.
				float extrapAge = std::min(target.age, kMaxExtrapAge);
				e.position = target.position + target.velocity * extrapAge;
				e.velocity = target.velocity;
				updateYaw(e, target.velocity, dt);
			}
			updateWalkDistance(e, dt);
		}
	}

private:
	// Per-entity interpolation + diagnostics state.
	struct InterpTarget {
		glm::vec3 position;
		glm::vec3 velocity;
		float yaw;
		float age;
		glm::vec3 moveTarget = {0, 0, 0};
		float moveSpeed = 0.0f;
		// Snapshots for the [PosDrift] log — let the user see whether the
		// gap is growing because the server moved or the client drifted.
		glm::vec3 prevServerPos = {0, 0, 0};
		glm::vec3 prevClientPos = {0, 0, 0};
		bool      prevInitialized = false;
	};

	// Tuning. The deadband is narrow (0.15m) because anything wider creates a
	// floor the steady-state drift parks on — 2m is literally how far behind
	// client ran when tolerance was 2m. kCorrectionRate is a floor; the real
	// rate grows with distance so the pull beats any bounded server motion.
	// kHardSnap is the escape hatch for respawn/teleport/large desync.
	// kMaxExtrapAge caps forward prediction when broadcasts stop (stalled
	// NPC, death, server hiccup) so we don't fling an entity into the void.
	static constexpr float kDriftTolerance   = 0.15f; // anti-jitter only
	static constexpr float kCorrectionRate   = 5.0f;  // blocks/sec floor
	static constexpr float kHardSnapDistance = 16.0f; // gap > 1 chunk → teleport
	static constexpr float kMaxExtrapAge     = 0.2f;  // 4× broadcast interval
	// Entity is "stale" if we haven't received an S_ENTITY update for this many
	// seconds. Stale entities freeze in place and render with hasError=true so
	// the user sees a red lightbulb instead of an entity drifting arbitrarily
	// under whatever the last broadcast velocity happened to be.
	static constexpr float kStaleThreshold   = 2.0f;

	// TPS-style turn: head snaps to movement direction; body smoothly catches
	// up. Renderer caps the head→body offset at ±45°, so a sharp direction
	// change shows the head rotating instantly and the body rotating after.
	void updateYaw(Entity& e, glm::vec3 localVel, float dt) {
		float horizSq = localVel.x * localVel.x + localVel.z * localVel.z;
		if (horizSq > 0.0025f)
			e.lookYaw = glm::degrees(std::atan2(localVel.z, localVel.x));
		smoothYawTowardsVelocity(e.yaw, localVel, dt);
	}

	// Pull client pos toward server pos. Two fixes stacked on top of the old
	// "fixed deadband + fixed rate" approach, which parked every entity at
	// exactly `kDriftTolerance` blocks of lag:
	//
	// 1. Extrapolate target forward by target.velocity * age. The server
	//    broadcasts at 20Hz but client physics runs at 60Hz, so between
	//    broadcasts `target.position` is frozen while the client keeps
	//    moving at `target.velocity`. That stair-step produced a constant
	//    ~half-broadcast lead for the client that the old deadband-gated
	//    correction never erased. Extrapolating the target forward removes
	//    the bias: now `diff` is real prediction error, not stair-step lag.
	// 2. Scale the pull with distance (`rate = floor + dist`). Keeps up
	//    with any bounded server velocity; still gentle when the gap is
	//    small.
	//
	// Age is clamped because if broadcasts stop (stalled mob, death, server
	// hiccup) we don't want to extrapolate into the void.
	void reconcileToServer(EntityId id, Entity& e, InterpTarget& target, float dt) {
		float extrapAge = std::min(target.age, kMaxExtrapAge);
		glm::vec3 predicted = target.position + target.velocity * extrapAge;
		glm::vec3 diff = predicted - e.position;
		float dist = glm::length(diff);

		// Proof-of-life log: confirms reconcileToServer is actually running in
		// the current backend (GL vs VK). First call + every 120th after
		// (~2s at 60Hz), PLUS every tick where dist exceeds the snap threshold
		// so if we're right on the edge we see every decision. `willSnap`
		// prints the exact branch condition so a red HUD + willSnap=0 makes
		// the threshold mismatch (HUD red @ 2m, snap @ 16m) visible.
		{
			static std::unordered_map<EntityId, uint32_t> s_tick;
			uint32_t n = ++s_tick[id];
			bool willSnap = (dist > kHardSnapDistance);
			if (n == 1 || n % 120 == 0 || willSnap) {
				std::fprintf(stderr, "[Reconcile] entity=%u tick=%u dist=%.2f "
				             "willSnap=%d (dist>%.1f) "
				             "client=(%.2f,%.2f,%.2f) server=(%.2f,%.2f,%.2f)\n",
				             id, n, dist,
				             willSnap ? 1 : 0, kHardSnapDistance,
				             e.position.x, e.position.y, e.position.z,
				             target.position.x, target.position.y, target.position.z);
				std::fflush(stderr);
			}
		}

		// // Owned player: client prediction is authoritative for smooth
		// // motion. We only intervene on genuine teleport-scale gaps
		// // (respawn, forced relocation, major desync). No soft-pull —
		// // it fights forward motion every frame and shakes the camera
		// // in RPG/TPS views. The server-side reject cooldown handles
		// // stale clientPos recovery; anything under kHardSnapDistance
		// // converges naturally as client and server physics run the same
		// // moveAndCollide on the same block data.
		// if (dist > kHardSnapDistance) {
		// 	// ── CLIENT SNAP ── the one and only place we teleport the
		// 	// local player back to the server's authoritative position.
		// 	// Logged unconditionally on every occurrence (no throttle) so
		// 	// the user sees proof the snap actually happened. Teed to
		// 	// stderr, stdout AND GameLogger (/tmp/civcraft_game.log +
		// 	// in-game Main Menu → Game Log viewer) so there is no stream
		// 	// the user might be watching where this is invisible.
		// 	glm::vec3 before = e.position;
		// 	e.position = target.position;
		// 	e.velocity = target.velocity;
		// 	logSnap(id, before, target, dist);
		// }

		target.prevClientPos = e.position;
	}

	// Walk distance for animation (tracks any horizontal motion).
	void updateWalkDistance(Entity& e, float dt) {
		float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
		if (hSpeed > 0.01f) {
			float wd = e.getProp<float>(Prop::WalkDistance, 0.0f);
			e.setProp(Prop::WalkDistance, wd + hSpeed * dt);
		}
	}

	// Emits one line per snap to every stream the user might be watching:
	//   - stderr with "[!!][Client][Snap]" (mirrors server-side "[!!][Server][Reject]")
	//   - stdout (same text) so `--log-only` / redirected stdout captures it
	//   - GameLogger so /tmp/civcraft_game.log and the in-game viewer show it
	// NOT throttled: every snap prints. Singleplayer localhost should never
	// trip this at all; each line is a real desync worth investigating.
	void logSnap(EntityId id, glm::vec3 before, const InterpTarget& t, float dist) {
		static std::unordered_map<EntityId, uint32_t> s_count;
		uint32_t n = ++s_count[id];
		char line[384];
		std::snprintf(line, sizeof(line),
		             "[!!][Client][Snap] PosErrorTooHigh entity=%u reason=\"dist>%.1f\" count=%u "
		             "dist=%.2f before=(%.2f,%.2f,%.2f) server=(%.2f,%.2f,%.2f) "
		             "vel=(%.2f,%.2f,%.2f) tol=%.2f",
		             id, kHardSnapDistance, n, dist,
		             before.x, before.y, before.z,
		             t.position.x, t.position.y, t.position.z,
		             t.velocity.x, t.velocity.y, t.velocity.z,
		             kDriftTolerance);
		std::fprintf(stderr, "%s\n", line); std::fflush(stderr);
		std::fprintf(stdout, "%s\n", line); std::fflush(stdout);
		GameLogger::instance().emit("SNAP", "%s", line);
	}

	std::unordered_map<EntityId, InterpTarget> m_targets;
};

} // namespace civcraft
