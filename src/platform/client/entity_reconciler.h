#pragma once

#include "logic/entity.h"
#include "logic/entity_physics.h"
#include "logic/physics.h"
#include "client/game_logger.h"
#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

namespace civcraft {

// Client-side smoothing for remote entities between 20Hz server broadcasts.
// Remote: mirror server directly (pos + vel*age), no local physics.
// Local player: client-predicted; reconcile toward authoritative server pos.
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

	glm::vec3 getServerPosition(EntityId id, glm::vec3 fallback) const {
		auto it = m_targets.find(id);
		return (it != m_targets.end()) ? it->second.position : fallback;
	}

	// serverSilent=true → no message in kStaleThreshold seconds; only then
	// flag entities red. (Entity leaving perception scope isn't a server fault.)
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
			if (stale) continue;  // freeze in place

			if (id == localPlayerId) {
				reconcileToServer(id, e, target, dt);
			} else {
				// Extrapolate to hide 20Hz broadcast stagger.
				float extrapAge = std::min(target.age, kMaxExtrapAge);
				e.position = target.position + target.velocity * extrapAge;
				e.velocity = target.velocity;
				updateYaw(e, target.velocity, dt);
			}
			updateWalkDistance(e, dt);
		}
	}

private:
	struct InterpTarget {
		glm::vec3 position;
		glm::vec3 velocity;
		float yaw;
		float age;
		glm::vec3 moveTarget = {0, 0, 0};
		float moveSpeed = 0.0f;
		glm::vec3 prevServerPos = {0, 0, 0};
		glm::vec3 prevClientPos = {0, 0, 0};
		bool      prevInitialized = false;
	};

	// Narrow deadband prevents steady-state lag park. kHardSnap is escape hatch
	// for respawn/teleport. kMaxExtrapAge caps extrapolation when broadcasts stop.
	static constexpr float kDriftTolerance   = 0.15f;
	static constexpr float kCorrectionRate   = 5.0f;  // blocks/sec floor
	static constexpr float kHardSnapDistance = 16.0f; // > 1 chunk
	static constexpr float kMaxExtrapAge     = 0.2f;  // 4× broadcast interval
	// Stale entities freeze + render hasError=true (red lightbulb).
	static constexpr float kStaleThreshold   = 2.0f;

	// Head snaps to movement direction; body catches up (renderer caps offset ±45°).
	void updateYaw(Entity& e, glm::vec3 localVel, float dt) {
		float horizSq = localVel.x * localVel.x + localVel.z * localVel.z;
		if (horizSq > 0.0025f)
			e.lookYaw = glm::degrees(std::atan2(localVel.z, localVel.x));
		smoothYawTowardsVelocity(e.yaw, localVel, dt);
	}

	// Extrapolate target by velocity*age to remove 20Hz-vs-60Hz stair-step bias,
	// then distance-scaled pull. Age is clamped to avoid flinging on stalls.
	void reconcileToServer(EntityId id, Entity& e, InterpTarget& target, float dt) {
		float extrapAge = std::min(target.age, kMaxExtrapAge);
		glm::vec3 predicted = target.position + target.velocity * extrapAge;
		glm::vec3 diff = predicted - e.position;
		float dist = glm::length(diff);

		target.prevClientPos = e.position;
	}

	void updateWalkDistance(Entity& e, float dt) {
		float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
		if (hSpeed > 0.01f) {
			float wd = e.getProp<float>(Prop::WalkDistance, 0.0f);
			e.setProp(Prop::WalkDistance, wd + hSpeed * dt);
		}
	}

	// Unthrottled: singleplayer localhost should never trip this.
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
