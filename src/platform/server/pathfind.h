#pragma once

// Greedy steering for C_SET_GOAL consumers; walks straight, dodges 45° on stuck.
// RTS click-to-move runs client-side via rts_executor.h + GridPlanner — this is
// the fallback for scripted/legacy server-issued goals.

#include "server/server_tuning.h"
#include "logic/entity.h"
#include "logic/physics.h"
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace civcraft {

// Grid formation around goalPos.
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

// Per-tick. Sets velocity toward goal for active-nav Living entities.
inline void updateNavigation(float dt, EntityManager& entities) {
	entities.forEach([&](Entity& e) {
		if (!e.nav.active) return;
		if (!e.def().isLiving()) return;

		glm::vec3 pos = e.position;
		glm::vec3 goal = e.nav.longGoal;

		// Check arrival even when client drives.
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

		// Client-driven (skipPhysics) → don't override; resumes when client stops.
		if (e.skipPhysics) return;

		float walkSpeed = e.def().walk_speed;
		if (walkSpeed <= 0) walkSpeed = 2.0f;

		e.nav.stuckTimer += dt;
		if (e.nav.stuckTimer >= ServerTuning::navStuckTimeout) {
			float movedX = pos.x - e.nav.stuckCheckPos.x;
			float movedZ = pos.z - e.nav.stuckCheckPos.z;
			float moved = std::sqrt(movedX * movedX + movedZ * movedZ);

			if (moved < ServerTuning::navStuckMinMove) {
				// Start/flip dodge direction.
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
