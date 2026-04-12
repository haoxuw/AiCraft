#pragma once

/**
 * Unified physics: collision detection + step-up for all objects.
 *
 * One function handles players, mobs, and items. Takes a
 * block-solid query function to avoid circular includes.
 */

#include <glm/glm.hpp>
#include <cmath>
#include <algorithm>
#include <functional>

namespace modcraft {

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
	bool canFly = false;
	bool smoothStep = false; // true = jump arc instead of instant step-up (for creatures)
};

// Build MoveParams from entity collision box + traits.
// Used by both client (gameplay_movement, network_server) and server (entity_manager).
inline MoveParams makeMoveParams(glm::vec3 boxMin, glm::vec3 boxMax,
                                  float gravityScale, bool isLiving, bool canFly) {
	MoveParams mp;
	mp.halfWidth  = (boxMax.x - boxMin.x) * 0.5f;
	mp.height     = boxMax.y - boxMin.y;
	mp.gravity    = 32.0f * gravityScale;
	mp.stepHeight = isLiving ? 1.0f : 0.0f;
	mp.canFly     = canFly;
	mp.smoothStep = false;
	return mp;
}

// Smoothly rotate `yaw` (degrees) toward the direction of horizontal velocity.
// Shared between server tick (entity_manager) and client prediction
// (network_server) so both paths produce the same turn rate. Matches the
// player-movement lerp at game_playing.cpp. No-op when speed is below
// minSpeedSq to avoid chasing a zero vector.
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

// Block query: returns the collision height of the block at (x,y,z).
// 0.0 = not solid (air/transparent), 1.0 = full block, 0.5 = half-height (stairs/slabs).
// The block physically occupies [y, y + return_value] in world space.
using BlockSolidFn = std::function<float(int, int, int)>;

/**
 * Move an object with full collision. Used by players and entities.
 */
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
					// Block at cell y occupies [y, y+bh].
					// Player body occupies [p.y, p.y+height].
					// Overlap: player.bottom < block.top AND player.top > block.bottom
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

	// Step-up helper: try incremental heights (0.1 .. stepHeight) to find
	// the minimum step needed. This lets entities climb partial blocks
	// (slabs, stairs) as well as full 1-block ledges.
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
				// Creatures: apply jump impulse, don't teleport
				// Jump velocity = sqrt(2 * gravity * stepHeight) to reach the ledge
				result.velocity.y = std::sqrt(2.0f * params.gravity * sh * 1.3f);
				// Don't move horizontally this frame — next frame we'll be airborne and clear it
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

	// Ground snap: when a grounded entity moves horizontally over a ledge,
	// snap it down to the surface immediately instead of slowly falling.
	// This prevents the "walking in air" gliding effect and makes movement
	// follow terrain like in RTS games (Warcraft-style).
	// Only applies when entity WAS on ground, didn't step up, isn't flying,
	// and isn't jumping (positive Y velocity = intentional upward movement).
	bool jumping = result.velocity.y > 0.5f;
	if (wasOnGround && !didStep && !params.canFly && !jumping) {
		// Check if we've moved horizontally but are now floating
		bool movedHorizontally = (r.x != pos.x || r.z != pos.z);
		if (movedHorizontally) {
			// Look for ground below (up to stepHeight + 1 blocks down)
			float searchDepth = params.stepHeight + 1.0f;
			for (float dy = 0; dy >= -searchDepth; dy -= 0.5f) {
				float testY = pos.y + dy;
				if (testY < 0) break;
				// Check if there's solid ground at this level
				int bx = (int)std::floor(r.x);
				int by = (int)std::floor(testY - 0.01f); // just below feet
				int bz = (int)std::floor(r.z);
				float bh = isSolid(bx, by, bz);
				if (bh > 0.0f) {
					float groundY = (float)by + bh;  // top surface of block
					// Snap down if we're above this ground
					if (r.y > groundY && r.y - groundY <= searchDepth) {
						// Verify body fits at the snapped position
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

} // namespace modcraft
