#pragma once

#include "shared/entity.h"
#include "shared/entity_physics.h"
#include "shared/physics.h"
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

namespace modcraft {

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

		// Owned player: client prediction is authoritative for smooth
		// motion. We only intervene on genuine teleport-scale gaps
		// (respawn, forced relocation, major desync). No soft-pull —
		// it fights forward motion every frame and shakes the camera
		// in RPG/TPS views. The server-side reject cooldown handles
		// stale clientPos recovery; anything under kHardSnapDistance
		// converges naturally as client and server physics run the same
		// moveAndCollide on the same block data.
		if (dist > kHardSnapDistance) {
			e.position = target.position;
			e.velocity = target.velocity;
			logDrift(id, e, target, dist);
		}

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

	// Prints the FIRST 10 drift events per entity immediately so the start of
	// a desync is visible, then throttles to ~1 line/sec. Singleplayer on
	// localhost should never trip this — every line is a real divergence
	// between client prediction and server physics worth investigating.
	void logDrift(EntityId id, const Entity& e, const InterpTarget& t, float dist) {
		static std::unordered_map<EntityId, int> s_tick;
		int& n = s_tick[id];
		n++;
		if (n > 10 && n % 60 != 0) return;
		glm::vec3 serverD = t.prevInitialized ? (t.position - t.prevServerPos) : glm::vec3(0.0f);
		glm::vec3 clientD = t.prevInitialized ? (e.position - t.prevClientPos) : glm::vec3(0.0f);
		std::fprintf(stderr, "[!!][PosDrift] eid=%u n=%d client=(%.2f,%.2f,%.2f) "
		             "server=(%.2f,%.2f,%.2f) dist=%.2f "
		             "serverD=(%.2f,%.2f,%.2f) clientD=(%.2f,%.2f,%.2f) "
		             "vel=(%.2f,%.2f,%.2f) (tol=%.2f snap=%.1f)\n",
		             id, n, e.position.x, e.position.y, e.position.z,
		             t.position.x, t.position.y, t.position.z, dist,
		             serverD.x, serverD.y, serverD.z,
		             clientD.x, clientD.y, clientD.z,
		             e.velocity.x, e.velocity.y, e.velocity.z,
		             kDriftTolerance, kHardSnapDistance);
		std::fflush(stderr);
	}

	std::unordered_map<EntityId, InterpTarget> m_targets;
};

} // namespace modcraft
