#pragma once

// Unified physics: one moveAndCollide() for players, mobs, items.
// Takes a BlockSolidFn to avoid circular includes.

#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
#include <functional>

namespace civcraft {

struct MoveResult {
	glm::vec3 position;
	glm::vec3 velocity;
	bool onGround = false;
	bool stepped = false;
};

struct MoveParams {
	float halfWidth = 0.375f;
	float height = 2.5f;
	float gravity = 28.0f;
	float maxFallSpeed = 50.0f;
	float stepHeight = 1.0f;
	// Ground-snap pull distance for grounded entities walking over ledges.
	// Large = RTS-style terrain glue (creatures). Small (≤0.5) = cliff-drop gravity (players). 0 = disabled.
	float maxGroundSnap = 2.0f;
	bool canFly = false;
	bool smoothStep = false; // jump arc vs instant step-up (creatures use arc)
};

// Shared by client (gameplay_movement, network_server) and server (entity_manager).
inline MoveParams makeMoveParams(glm::vec3 boxMin, glm::vec3 boxMax,
                                  float gravityScale, bool isLiving, bool canFly) {
	MoveParams mp;
	mp.halfWidth  = (boxMax.x - boxMin.x) * 0.5f;
	mp.height     = boxMax.y - boxMin.y;
	mp.gravity    = 32.0f * gravityScale;
	mp.stepHeight = isLiving ? 1.0f : 0.0f;
	// Default = stepHeight+1; players override to get cliff-drop gravity.
	mp.maxGroundSnap = isLiving ? (mp.stepHeight + 1.0f) : 0.0f;
	mp.canFly     = canFly;
	mp.smoothStep = false;
	return mp;
}

// Shared turn rate for server tick + client prediction; MUST match game_playing.cpp lerp.
// No-op below minSpeedSq to avoid chasing zero vector.
inline void smoothYawTowardsVelocity(float& yaw, glm::vec3 vel, float dt,
                                     float rate = 20.0f,
                                     float minSpeedSq = 0.0025f) {
	float horizSpeedSq = vel.x * vel.x + vel.z * vel.z;
	if (horizSpeedSq <= minSpeedSq) return;
	float targetYaw = glm::degrees(std::atan2(vel.z, vel.x));
	float diff = targetYaw - yaw;
	while (diff >  180.0f) diff -= 360.0f;
	while (diff < -180.0f) diff += 360.0f;
	yaw += diff * std::min(dt * rate, 1.0f);
}

// Returns block collision height: 0=air, 1=full, 0.5=slab. Block occupies [y, y+bh].
using BlockSolidFn = std::function<float(int, int, int)>;

// AABB-vs-world test. Same predicate as moveAndCollide's internal `blocked`,
// exposed for client pre-send check + server clientPos acceptance.
inline bool isPositionBlocked(const BlockSolidFn& isSolid, glm::vec3 p,
                              float halfWidth, float height) {
	int x0 = (int)std::floor(p.x - halfWidth);
	int x1 = (int)std::floor(p.x + halfWidth);
	int y0 = (int)std::floor(p.y);
	int y1 = (int)std::floor(p.y + height);
	int z0 = (int)std::floor(p.z - halfWidth);
	int z1 = (int)std::floor(p.z + halfWidth);
	for (int y = y0; y <= y1; y++)
		for (int z = z0; z <= z1; z++)
			for (int x = x0; x <= x1; x++) {
				float bh = isSolid(x, y, z);
				if (bh <= 0.0f) continue;
				if (p.y < (float)y + bh && p.y + height > (float)y)
					return true;
			}
	return false;
}

// Full-collision move — the single physics implementation (Rule 6).
inline MoveResult moveAndCollide(const BlockSolidFn& isSolid,
                                  glm::vec3 pos, glm::vec3 vel,
                                  float dt, const MoveParams& params,
                                  bool wasOnGround) {
	auto blocked = [&](glm::vec3 p) -> bool {
		int x0 = (int)std::floor(p.x - params.halfWidth);
		int x1 = (int)std::floor(p.x + params.halfWidth);
		int y0 = (int)std::floor(p.y);
		int y1 = (int)std::floor(p.y + params.height);
		int z0 = (int)std::floor(p.z - params.halfWidth);
		int z1 = (int)std::floor(p.z + params.halfWidth);
		for (int y = y0; y <= y1; y++)
			for (int z = z0; z <= z1; z++)
				for (int x = x0; x <= x1; x++) {
					float bh = isSolid(x, y, z);
					if (bh <= 0.0f) continue;
					// AABB overlap: block [y,y+bh] vs body [p.y,p.y+height]
					if (p.y < (float)y + bh && p.y + params.height > (float)y)
						return true;
				}
		return false;
	};

	MoveResult result;
	result.velocity = vel;

	if (!params.canFly) {
		result.velocity.y -= params.gravity * dt;
		result.velocity.y = std::max(result.velocity.y, -params.maxFallSpeed);
	}

	glm::vec3 delta = result.velocity * dt;
	glm::vec3 r = pos;
	bool didStep = false;

	// Find min step height (0.1 .. stepHeight) to climb slabs/stairs/ledges.
	auto tryStepUp = [&](float newX, float baseY, float newZ) -> float {
		for (float sh = 0.1f; sh <= params.stepHeight + 0.01f; sh += 0.1f) {
			if (!blocked({newX, baseY + sh, newZ}))
				return sh;
		}
		return -1.0f; // can't step
	};

	// X
	if (!blocked({pos.x + delta.x, pos.y, pos.z})) {
		r.x += delta.x;
	} else if (wasOnGround && params.stepHeight > 0 && delta.x != 0.0f) {
		float sh = tryStepUp(pos.x + delta.x, pos.y, pos.z);
		if (sh > 0) {
			if (params.smoothStep) {
				// Creatures: jump impulse v = sqrt(2·g·h); horizontal resolves next frame.
				result.velocity.y = std::sqrt(2.0f * params.gravity * sh * 1.3f);
			} else {
				r.x += delta.x;
				r.y = pos.y + sh;
				didStep = true;
			}
		}
	}

	// Y (gravity / jump — skip if we just stepped)
	if (!didStep) {
		if (!blocked({r.x, pos.y + delta.y, pos.z}))
			r.y += delta.y;
	}

	// Z
	if (!blocked({r.x, r.y, pos.z + delta.z})) {
		r.z += delta.z;
	} else if (wasOnGround && !didStep && params.stepHeight > 0 && delta.z != 0.0f) {
		float sh = tryStepUp(r.x, r.y, pos.z + delta.z);
		if (sh > 0) {
			if (params.smoothStep) {
				result.velocity.y = std::sqrt(2.0f * params.gravity * sh * 1.3f);
			} else {
				r.z += delta.z;
				r.y += sh;
				didStep = true;
			}
		}
	}

	// Ground snap: glue grounded entities to terrain when walking off ledges (RTS-style).
	// Skip when jumping (positive Y = intentional upward).
	bool jumping = result.velocity.y > 0.5f;
	if (wasOnGround && !didStep && !params.canFly && !jumping
	    && params.maxGroundSnap > 0.0f) {
		bool movedHorizontally = (r.x != pos.x || r.z != pos.z);
		if (movedHorizontally) {
			float searchDepth = params.maxGroundSnap;
			for (float dy = 0; dy >= -searchDepth; dy -= 0.5f) {
				float testY = pos.y + dy;
				if (testY < 0) break;
				int bx = (int)std::floor(r.x);
				int by = (int)std::floor(testY - 0.01f);
				int bz = (int)std::floor(r.z);
				float bh = isSolid(bx, by, bz);
				if (bh > 0.0f) {
					float groundY = (float)by + bh;
					if (r.y > groundY && r.y - groundY <= searchDepth) {
						if (!blocked({r.x, groundY, r.z})) {
							r.y = groundY;
							result.velocity.y = 0;
						}
					}
					break;
				}
			}
		}
	}

	result.position = r;
	result.stepped = didStep;

	if (didStep) {
		result.velocity.y = 0;
		result.onGround = true;
	} else if (std::abs(r.y - pos.y) < 0.001f && result.velocity.y <= 0) {
		result.velocity.y = 0;
		result.onGround = true;
	} else if (r.y < pos.y + delta.y && result.velocity.y > 0) {
		result.velocity.y = 0;
		result.onGround = false;
	} else {
		result.onGround = false;
	}

	return result;
}

} // namespace civcraft
