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
// networking — all per-entity physics, interpolation, and drift correction
// live here.
//
// Per tick, for every non-local entity, run the same moveAndCollide() that
// the server uses (via stepEntityPhysics). Then for every entity (local
// player included) nudge client position toward the server's last-known
// position. Small gaps smooth at a fixed rate; mid-size gaps close faster
// (rate scales with distance); huge gaps hard-snap so a teleport/respawn
// doesn't spend seconds crawling to the new location.
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
	void tick(float dt, EntityId localPlayerId,
	          std::unordered_map<EntityId, std::unique_ptr<Entity>>& entities,
	          const BlockSolidFn& solidFn) {
		for (auto& [id, target] : m_targets) {
			auto it = entities.find(id);
			if (it == entities.end()) continue;
			Entity& e = *it->second;

			if (id != localPlayerId)
				stepRemoteEntity(e, target, solidFn, dt);

			reconcileToServer(id, e, target, dt);
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

	// Tuning. kCorrectionRate is a floor — real rate scales with distance so
	// that a drift growing faster than the base rate still closes. kHardSnap
	// is the escape hatch: above this we teleport rather than smooth, because
	// the gap is almost certainly a respawn or large desync, not prediction
	// error worth hiding.
	static constexpr float kDriftTolerance  = 2.0f;   // start correcting past this gap (blocks)
	static constexpr float kCorrectionRate  = 5.0f;   // blocks/sec floor
	static constexpr float kHardSnapDistance = 16.0f; // gap > 1 chunk → teleport

	// Remote entity: run local physics using the server's last-known velocity
	// (x/z) plus our own y (so gravity integrates smoothly between broadcasts),
	// then apply TPS-style head-snap + body-catch-up yaw.
	void stepRemoteEntity(Entity& e, const InterpTarget& t,
	                      const BlockSolidFn& solidFn, float dt) {
		glm::vec3 localVel = {t.velocity.x, e.velocity.y, t.velocity.z};
		stepEntityPhysics(e, localVel, solidFn, dt);
		updateYaw(e, localVel, dt);
	}

	// TPS-style turn: head snaps to movement direction; body smoothly catches
	// up. Renderer caps the head→body offset at ±45°, so a sharp direction
	// change shows the head rotating instantly and the body rotating after.
	void updateYaw(Entity& e, glm::vec3 localVel, float dt) {
		float horizSq = localVel.x * localVel.x + localVel.z * localVel.z;
		if (horizSq > 0.0025f)
			e.lookYaw = glm::degrees(std::atan2(localVel.z, localVel.x));
		smoothYawTowardsVelocity(e.yaw, localVel, dt);
	}

	// Pull client pos toward server pos. The fixed 5u/s rate would lose
	// ground whenever the server moves faster than that, so scale the rate
	// with the gap — the farther behind we are, the harder we pull.
	void reconcileToServer(EntityId id, Entity& e, InterpTarget& target, float dt) {
		glm::vec3 diff = target.position - e.position;
		float dist = glm::length(diff);

		if (dist > kHardSnapDistance) {
			e.position = target.position;
			e.velocity = target.velocity;
			logDrift(id, e, target, dist);
		} else if (dist > kDriftTolerance) {
			float rate = kCorrectionRate + dist;  // 2 blocks → 7u/s, 10 blocks → 15u/s
			e.position += diff * std::min(dt * rate, 1.0f);
			logDrift(id, e, target, dist);
		}

		target.prevClientPos = e.position;
		target.age += dt;
	}

	// Walk distance for animation (tracks any horizontal motion).
	void updateWalkDistance(Entity& e, float dt) {
		float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
		if (hSpeed > 0.01f) {
			float wd = e.getProp<float>(Prop::WalkDistance, 0.0f);
			e.setProp(Prop::WalkDistance, wd + hSpeed * dt);
		}
	}

	void logDrift(EntityId id, const Entity& e, const InterpTarget& t, float dist) {
		static std::unordered_map<EntityId, int> s_tick;
		int& n = s_tick[id];
		if (++n % 30 != 0) return;  // ~2 lines/sec per entity
		glm::vec3 serverD = t.prevInitialized ? (t.position - t.prevServerPos) : glm::vec3(0.0f);
		glm::vec3 clientD = t.prevInitialized ? (e.position - t.prevClientPos) : glm::vec3(0.0f);
		std::printf("[PosDrift] eid=%u client=(%.2f,%.2f,%.2f) "
		            "server=(%.2f,%.2f,%.2f) dist=%.2f "
		            "serverD=(%.2f,%.2f,%.2f) clientD=(%.2f,%.2f,%.2f) "
		            "vel=(%.2f,%.2f,%.2f) (tol=%.1f, rateFloor=%.1fu/s, snap=%.1f)\n",
		            id, e.position.x, e.position.y, e.position.z,
		            t.position.x, t.position.y, t.position.z, dist,
		            serverD.x, serverD.y, serverD.z,
		            clientD.x, clientD.y, clientD.z,
		            e.velocity.x, e.velocity.y, e.velocity.z,
		            kDriftTolerance, kCorrectionRate, kHardSnapDistance);
		std::fflush(stdout);
	}

	std::unordered_map<EntityId, InterpTarget> m_targets;
};

} // namespace modcraft
