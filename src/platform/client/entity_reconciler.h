#pragma once

#include "logic/entity.h"
#include "logic/entity_physics.h"
#include "logic/physics.h"
#include "client/game_logger.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <deque>
#include <memory>
#include <unordered_map>

namespace civcraft {

// Snapshot interpolation for remote entities.
//
// S_ENTITY broadcasts land at 20 Hz. Instead of snapping to the latest
// snapshot (jitter) or extrapolating with pos+vel*age (overshoot when the
// entity changes direction), we buffer a few snapshots per entity and
// render at renderTime = clientTime - kInterpDelay, lerping between the
// two snapshots that bracket renderTime. The client is deliberately
// ~100ms behind the server — the tradeoff for smooth motion that
// survives 20Hz stagger and moderate network jitter.
//
// Local player is client-predicted elsewhere; this class skips position
// updates for it but still maintains stale-entity detection.
class EntityReconciler {
public:
	// Timings chosen for 20Hz server broadcasts:
	//   kInterpDelay   = 100ms — 2× broadcast gap. Anything less and a
	//                    single late packet empties the bracket buffer.
	//   kMaxExtrapAge  = 200ms — if broadcasts pause beyond this the
	//                    entity holds position rather than flying off.
	//   kStaleThreshold= 2.0s  — red-lightbulb flag (unchanged).
	//   kMaxSnapshots  = 8     — ~400ms of history at 20Hz.
	static constexpr float  kInterpDelay    = 0.10f;
	static constexpr float  kMaxExtrapAge   = 0.20f;
	static constexpr float  kStaleThreshold = 2.0f;
	static constexpr size_t kMaxSnapshots   = 8;

	void onEntityCreate(EntityId id, glm::vec3 pos, glm::vec3 vel, float yaw,
	                    glm::vec3 moveTarget, float moveSpeed) {
		auto& t = m_tracks[id];
		t.snaps.clear();
		t.snaps.push_back({m_clientTime, pos, vel, yaw});
		t.moveTarget = moveTarget;
		t.moveSpeed  = moveSpeed;
		t.lastUpdate = m_clientTime;
	}

	void onEntityUpdate(EntityId id, glm::vec3 pos, glm::vec3 vel, float yaw,
	                    glm::vec3 moveTarget, float moveSpeed) {
		auto& t = m_tracks[id];
		t.snaps.push_back({m_clientTime, pos, vel, yaw});
		while (t.snaps.size() > kMaxSnapshots) t.snaps.pop_front();
		t.moveTarget = moveTarget;
		t.moveSpeed  = moveSpeed;
		t.lastUpdate = m_clientTime;
	}

	void onEntityRemove(EntityId id) { m_tracks.erase(id); }

	// Latest known server position (for the "was my click-to-move target
	// reached?" query in the renderer). Not the interpolated render position.
	glm::vec3 getServerPosition(EntityId id, glm::vec3 fallback) const {
		auto it = m_tracks.find(id);
		if (it == m_tracks.end() || it->second.snaps.empty()) return fallback;
		return it->second.snaps.back().position;
	}

	// serverSilent=true → no message in kStaleThreshold seconds; only then
	// flag entities red. (Entity leaving perception scope isn't a server fault.)
	void tick(float dt, EntityId localPlayerId,
	          std::unordered_map<EntityId, std::unique_ptr<Entity>>& entities,
	          const BlockSolidFn& /*solidFn*/,
	          bool serverSilent) {
		m_clientTime += dt;
		const float renderTime = m_clientTime - kInterpDelay;

		for (auto& [id, track] : m_tracks) {
			auto it = entities.find(id);
			if (it == entities.end()) continue;
			Entity& e = *it->second;

			float ageSinceLast = m_clientTime - track.lastUpdate;
			bool stale = (ageSinceLast > kStaleThreshold);
			bool realError = stale && serverSilent;
			if (realError && !e.hasError) {
				e.hasError = true;
				e.goalText = "⚠ stale (server silent)";
				std::printf("[Reconciler] eid=%u stale+silent (age=%.2fs) — frozen+red\n",
				            id, ageSinceLast);
				std::fflush(stdout);
			} else if (!realError && e.hasError && (!stale || !serverSilent)) {
				e.hasError = false;
			}
			if (stale) continue;  // freeze in place

			// Local player position is owned by client prediction; skip.
			if (id == localPlayerId) continue;

			sampleAt(track, renderTime, e, dt);
			updateWalkDistance(e, dt);
		}
	}

private:
	struct Snapshot {
		float     time;      // m_clientTime at receive
		glm::vec3 position;
		glm::vec3 velocity;
		float     yaw;
	};
	struct Track {
		std::deque<Snapshot> snaps;       // oldest at front, newest at back
		glm::vec3            moveTarget = {0, 0, 0};
		float                moveSpeed  = 0.0f;
		float                lastUpdate = 0.0f;
	};

	// Lerp track between the two snapshots bracketing renderTime.
	// Before we have two snapshots: snap to first. Past the newest:
	// extrapolate with newest velocity, capped at kMaxExtrapAge.
	void sampleAt(Track& track, float renderTime, Entity& e, float dt) {
		auto& s = track.snaps;
		if (s.empty()) return;

		if (s.size() == 1 || renderTime <= s.front().time) {
			apply(e, s.front().position, s.front().velocity, dt);
			return;
		}

		if (renderTime >= s.back().time) {
			float extrap = std::min(renderTime - s.back().time, kMaxExtrapAge);
			glm::vec3 p = s.back().position + s.back().velocity * extrap;
			apply(e, p, s.back().velocity, dt);
			return;
		}

		// Find first snap with time > renderTime; bracket is (i-1, i).
		size_t i = 1;
		while (i < s.size() && s[i].time < renderTime) i++;
		const Snapshot& a = s[i - 1];
		const Snapshot& b = s[i];
		float span = b.time - a.time;
		float alpha = span > 1e-4f ? (renderTime - a.time) / span : 0.0f;
		alpha = std::clamp(alpha, 0.0f, 1.0f);
		glm::vec3 p = glm::mix(a.position, b.position, alpha);
		glm::vec3 v = glm::mix(a.velocity, b.velocity, alpha);
		apply(e, p, v, dt);

		// Keep one snap before renderTime for lerp; drop anything older.
		while (s.size() > 2 && s[1].time < renderTime) s.pop_front();
	}

	// Head snaps to movement direction; body yaw catches up (renderer caps
	// body-vs-head offset ±45°). Matches the original reconciler behavior.
	void apply(Entity& e, glm::vec3 pos, glm::vec3 vel, float dt) {
		e.position = pos;
		e.velocity = vel;
		float horizSq = vel.x * vel.x + vel.z * vel.z;
		if (horizSq > 0.0025f)
			e.lookYaw = glm::degrees(std::atan2(vel.z, vel.x));
		smoothYawTowardsVelocity(e.yaw, vel, dt);
	}

	void updateWalkDistance(Entity& e, float dt) {
		float hSpeed = std::sqrt(e.velocity.x * e.velocity.x + e.velocity.z * e.velocity.z);
		if (hSpeed > 0.01f) {
			float wd = e.getProp<float>(Prop::WalkDistance, 0.0f);
			e.setProp(Prop::WalkDistance, wd + hSpeed * dt);
		}
	}

	std::unordered_map<EntityId, Track> m_tracks;
	float m_clientTime = 0.0f;
};

} // namespace civcraft
