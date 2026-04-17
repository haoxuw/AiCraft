#pragma once

/**
 * Server-side greedy navigation — simple local steering for click-to-move.
 *
 * No A* or long-range planning. Entities walk straight toward their goal.
 * If stuck (physics collision blocks progress), they dodge 45 degrees left
 * or right and keep walking. Entities never stop — they always keep pushing
 * toward the goal until they arrive or the player cancels.
 *
 * RTS click-to-move pathfinding is now handled on the client
 * (src/platform/client/rts_executor.h) — the client plans with GridPlanner,
 * stores waypoints, and drives each owned unit via per-tick Move proposals.
 * This file is what runs for any remaining *server-issued* nav goal, e.g.
 * scripted NPC movement that still uses C_SET_GOAL.
 *
 * Two functions:
 *   planGroupFormation() — assign formation-offset goals for a group of entities
 *   updateNavigation()   — called each server tick, sets entity velocities
 */

#include "server/server_tuning.h"
#include "logic/entity.h"
#include "logic/physics.h"
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace civcraft {

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
					e.nav.dodgeSign = (std::rand() % 2 == 0) ? 1 : -1;
				} else {
					e.nav.dodgeSign = -e.nav.dodgeSign;
				}
				e.nav.dodgeTimer = ServerTuning::navDodgeDuration;
			} else {
				if (e.nav.dodgeTimer <= 0) {
					e.nav.dodgeSign = 0;
				}
			}
			e.nav.stuckCheckPos = pos;
			e.nav.stuckTimer = 0;
		}

		if (e.nav.stuckCheckPos.x == 0 && e.nav.stuckCheckPos.y == 0 && e.nav.stuckCheckPos.z == 0
			&& (pos.x != 0 || pos.y != 0 || pos.z != 0)) {
			e.nav.stuckCheckPos = pos;
		}

		float dirX = goal.x - pos.x;
		float dirZ = goal.z - pos.z;
		float len = std::sqrt(dirX * dirX + dirZ * dirZ);
		if (len > 0.001f) { dirX /= len; dirZ /= len; }

		if (distXZ < ServerTuning::navArriveDistance * 3.0f) {
			e.nav.dodgeTimer = 0;
			e.nav.dodgeSign = 0;
		}

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

		e.velocity.x = dirX * walkSpeed;
		e.velocity.z = dirZ * walkSpeed;

		e.moveTarget = goal;
		e.moveSpeed = walkSpeed;
	});
}

} // namespace civcraft
