#pragma once

/**
 * Server-side greedy navigation — simple local steering for click-to-move.
 *
 * No A* or long-range planning. Entities walk straight toward their goal.
 * If stuck (physics collision blocks progress), they dodge 45 degrees left
 * or right and keep walking. Entities never stop — they always keep pushing
 * toward the goal until they arrive or the player cancels.
 *
 * Two functions:
 *   planGroupFormation() — assign formation-offset goals for a group of entities
 *   updateNavigation()   — called each server tick, sets entity velocities
 */

#include "server/server_tuning.h"
#include "shared/entity.h"
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace modcraft {

// Assign formation-offset long-term goals for a group of entities.
// Entities spread into a grid centered on goalPos.
inline void planGroupFormation(glm::vec3 goalPos, std::vector<Entity*>& entities) {
	int n = (int)entities.size();
	if (n == 0) return;
	if (n == 1) {
		entities[0]->nav.setGoal(goalPos);
		return;
	}

	float spacing = ServerTuning::navFormationSpacing;
	int cols = (int)std::ceil(std::sqrt((float)n));
	int rows = (n + cols - 1) / cols;
	float offX = (cols - 1) * spacing * 0.5f;
	float offZ = (rows - 1) * spacing * 0.5f;

	for (int i = 0; i < n; i++) {
		float gx = goalPos.x + (i % cols) * spacing - offX;
		float gz = goalPos.z + (i / cols) * spacing - offZ;
		entities[i]->nav.setGoal({gx, goalPos.y, gz});
	}
}

// Update navigation for all entities with active nav goals.
// Called once per server tick. Sets entity velocity toward their goal.
// solidFn is not currently used (stuck detection is position-based),
// but kept in the signature for future obstacle probing.
inline void updateNavigation(float dt, EntityManager& entities) {
	entities.forEach([&](Entity& e) {
		if (!e.nav.active) return;
		if (!e.def().isLiving()) return;

		glm::vec3 pos = e.position;
		glm::vec3 goal = e.nav.longGoal;

		// --- Arrived? (always check, even when client is driving) ---
		float dx = pos.x - goal.x;
		float dz = pos.z - goal.z;
		float distXZ = std::sqrt(dx * dx + dz * dz);
		if (distXZ < ServerTuning::navArriveDistance) {
			e.nav.clear();
			e.velocity.x = 0;
			e.velocity.z = 0;
			e.moveTarget = e.position;
			e.moveSpeed = 0;
			return;
		}

		// If the client is actively driving this entity (sent a Move action
		// with clientPos this tick), don't override its velocity — the client's
		// local moveAndCollide provides responsive movement. Nav will take over
		// when the client stops sending Move actions.
		if (e.skipPhysics) return;

		float walkSpeed = e.def().walk_speed;
		if (walkSpeed <= 0) walkSpeed = 2.0f; // fallback

		// --- Stuck detection ---
		e.nav.stuckTimer += dt;
		if (e.nav.stuckTimer >= ServerTuning::navStuckTimeout) {
			float movedX = pos.x - e.nav.stuckCheckPos.x;
			float movedZ = pos.z - e.nav.stuckCheckPos.z;
			float moved = std::sqrt(movedX * movedX + movedZ * movedZ);

			if (moved < ServerTuning::navStuckMinMove) {
				// Stuck! Start or flip dodge
				if (e.nav.dodgeSign == 0) {
					// Pick random direction
					e.nav.dodgeSign = (std::rand() % 2 == 0) ? 1 : -1;
				} else {
					// Already dodging — try the other side
					e.nav.dodgeSign = -e.nav.dodgeSign;
				}
				e.nav.dodgeTimer = ServerTuning::navDodgeDuration;
			} else {
				// Making progress — clear dodge
				if (e.nav.dodgeTimer <= 0) {
					e.nav.dodgeSign = 0;
				}
			}
			e.nav.stuckCheckPos = pos;
			e.nav.stuckTimer = 0;
		}

		// Initialize stuck check position on first tick
		if (e.nav.stuckCheckPos.x == 0 && e.nav.stuckCheckPos.y == 0 && e.nav.stuckCheckPos.z == 0
			&& (pos.x != 0 || pos.y != 0 || pos.z != 0)) {
			e.nav.stuckCheckPos = pos;
		}

		// --- Compute steering direction ---
		// Base direction: toward long-term goal
		float dirX = goal.x - pos.x;
		float dirZ = goal.z - pos.z;
		float len = std::sqrt(dirX * dirX + dirZ * dirZ);
		if (len > 0.001f) {
			dirX /= len;
			dirZ /= len;
		}

		// When close to goal, cancel dodge and go straight to converge
		if (distXZ < ServerTuning::navArriveDistance * 3.0f) {
			e.nav.dodgeTimer = 0;
			e.nav.dodgeSign = 0;
		}

		// Apply dodge rotation if active
		if (e.nav.dodgeTimer > 0) {
			float angle = ServerTuning::navDodgeAngle * e.nav.dodgeSign;
			float cosA = std::cos(angle);
			float sinA = std::sin(angle);
			float newDirX = dirX * cosA - dirZ * sinA;
			float newDirZ = dirX * sinA + dirZ * cosA;
			dirX = newDirX;
			dirZ = newDirZ;
			e.nav.dodgeTimer -= dt;
		}

		// --- Set velocity (always — never stop walking) ---
		e.velocity.x = dirX * walkSpeed;
		e.velocity.z = dirZ * walkSpeed;

		// Broadcast move destination for client-side prediction
		e.moveTarget = goal;
		e.moveSpeed = walkSpeed;

		// yaw is smoothed per-tick in GameServer::tick from velocity.
	});
}

} // namespace modcraft
