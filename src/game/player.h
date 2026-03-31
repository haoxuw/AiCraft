#pragma once

#include "common/types.h"
#include "common/world.h"
#include "common/inventory.h"
#include "common/constants.h"
#include "common/block_registry.h"
#include "game/types.h"
#include <algorithm>
#include <cmath>

namespace aicraft {

class Player {
public:
	Inventory inventory;
	int selectedSlot = 0;
	glm::vec3 velocity = {0, 0, 0};
	bool onGround = false;
	float breakCD = 0.0f;
	float placeCD = 0.0f;
	int hp = 20;
	int maxHP = 20;
	float hunger = 20.0f;

	Player() : inventory(HOTBAR_SIZE) {}

	// --------------------------------------------------------
	// Inventory helpers
	// --------------------------------------------------------

	void fillCreativeInventory(const BlockRegistry& blocks) {
		const char* blockTypes[] = {
			BlockType::Stone, BlockType::Dirt, BlockType::Grass, BlockType::Sand,
			BlockType::Wood, BlockType::Leaves, BlockType::Snow, BlockType::TNT,
			BlockType::Cobblestone,
		};
		for (int i = 0; i < HOTBAR_SIZE && i < 9; i++) {
			inventory.slot(i).type = blockTypes[i];
			inventory.slot(i).count = 64;
		}
	}

	void fillSurvivalInventory() {
		inventory.slot(0).type = BlockType::Stone;
		inventory.slot(0).count = 10;
		inventory.slot(1).type = BlockType::Wood;
		inventory.slot(1).count = 10;
	}

	// --------------------------------------------------------
	// Movement
	// --------------------------------------------------------

	void applySurvivalMovement(glm::vec3 horizontalMove, float speed, float dt,
	                           bool jump, World& world, glm::vec3& feetPos) {
		velocity.x = horizontalMove.x * speed;
		velocity.z = horizontalMove.z * speed;
		velocity.y -= 28.0f * dt;
		velocity.y = std::max(velocity.y, -50.0f);

		if (jump && onGround) {
			velocity.y = 8.5f;
			onGround = false;
		}

		glm::vec3 oldPos = feetPos;
		bool stepped = false;
		feetPos = collideAndSlide(world, oldPos, velocity * dt, 0.375f, 2.5f, onGround, &stepped);

		if (stepped) {
			velocity.y = 0;
			onGround = true;
		} else if (std::abs(feetPos.y - oldPos.y) < 0.001f && velocity.y <= 0) {
			velocity.y = 0;
			onGround = true;
		} else if (feetPos.y < oldPos.y + velocity.y * dt && velocity.y > 0) {
			velocity.y = 0;
		} else {
			onGround = false;
		}

		// Hunger drains slowly
		hunger -= 0.005f * dt;
		if (hunger < 0) hunger = 0;
	}

	void applyCreativeMovement(glm::vec3 moveDir, float speed, float dt,
	                           bool ascend, bool descend, glm::vec3& feetPos) {
		feetPos += moveDir * speed * dt;
		if (ascend)
			feetPos.y += speed * dt;
		if (descend)
			feetPos.y -= speed * dt;
	}

	// --------------------------------------------------------
	// Unstuck
	// --------------------------------------------------------

	void unstuck(World& world, glm::vec3& feetPos) {
		int bx = (int)std::floor(feetPos.x);
		int by = (int)std::floor(feetPos.y);
		int bz = (int)std::floor(feetPos.z);

		// Quick check: are feet or head inside solid? (player is 2.5 blocks tall)
		bool stuck = world.blocks.get(world.getBlock(bx, by, bz)).solid ||
		             world.blocks.get(world.getBlock(bx, by + 1, bz)).solid ||
		             world.blocks.get(world.getBlock(bx, by + 2, bz)).solid;

		if (!stuck) return;

		// Search upward for valid position (max 64 blocks)
		for (int dy = 1; dy <= 64; dy++) {
			int ty = by + dy;
			bool ground = world.blocks.get(world.getBlock(bx, ty - 1, bz)).solid;
			bool bodyFree =
				!world.blocks.get(world.getBlock(bx, ty, bz)).solid &&
				!world.blocks.get(world.getBlock(bx, ty + 1, bz)).solid;
			bool sidesOk =
				!world.blocks.get(world.getBlock(bx + 1, ty, bz)).solid &&
				!world.blocks.get(world.getBlock(bx - 1, ty, bz)).solid &&
				!world.blocks.get(world.getBlock(bx, ty, bz + 1)).solid &&
				!world.blocks.get(world.getBlock(bx, ty, bz - 1)).solid;

			if (ground && bodyFree && sidesOk) {
				feetPos.y = (float)ty;
				velocity = {0, 0, 0};
				onGround = true;
				return;
			}
		}
	}

	// --------------------------------------------------------
	// Cooldowns
	// --------------------------------------------------------

	void tickCooldowns(float dt) {
		breakCD -= dt;
		placeCD -= dt;
	}

	// --------------------------------------------------------
	// Reset
	// --------------------------------------------------------

	void reset(glm::vec3 spawnPos, glm::vec3& feetPos) {
		feetPos = spawnPos;
		velocity = {0, 0, 0};
		onGround = false;
		hp = maxHP;
		hunger = 20.0f;
	}

private:
	// --------------------------------------------------------
	// Collision
	// --------------------------------------------------------

	// Step-up height: automatically climb ledges up to this height.
	// 0.6 matches Luanti/Minecraft -- enough for slabs and stairs,
	// small enough that the visual transition is gentle.
	static constexpr float STEP_HEIGHT = 0.6f;

	// Smooth step: instead of teleporting up, we give a vertical
	// velocity boost that carries the player up over several frames.
	static constexpr float STEP_BOOST_VEL = 9.0f;

	static glm::vec3 collideAndSlide(World& world, glm::vec3 pos, glm::vec3 vel,
	                                  float halfW, float height, bool onGround,
	                                  bool* stepped = nullptr) {
		auto blocked = [&](glm::vec3 p) -> bool {
			int x0 = (int)std::floor(p.x - halfW), x1 = (int)std::floor(p.x + halfW);
			int y0 = (int)std::floor(p.y),          y1 = (int)std::floor(p.y + height);
			int z0 = (int)std::floor(p.z - halfW), z1 = (int)std::floor(p.z + halfW);
			for (int y = y0; y <= y1; y++)
				for (int z = z0; z <= z1; z++)
					for (int x = x0; x <= x1; x++)
						if (world.blocks.get(world.getBlock(x, y, z)).solid)
							return true;
			return false;
		};

		glm::vec3 r = pos;
		bool didStep = false;

		// X: try direct move; if blocked and on ground, check if steppable
		if (!blocked({pos.x + vel.x, pos.y, pos.z})) {
			r.x += vel.x;
		} else if (onGround && vel.x != 0.0f) {
			// Find exact step height needed (check 0.1 increments up to STEP_HEIGHT)
			for (float sh = 0.1f; sh <= STEP_HEIGHT + 0.01f; sh += 0.1f) {
				if (!blocked({pos.x + vel.x, pos.y + sh, pos.z})) {
					r.x += vel.x;
					r.y = pos.y + sh; // raise to minimum needed height
					didStep = true;
					break;
				}
			}
		}

		// Y: gravity / jump — skip if we just stepped (velocity will be set outside)
		if (!didStep) {
			if (!blocked({r.x, pos.y + vel.y, pos.z})) r.y += vel.y;
		}

		// Z: try direct move; if blocked and on ground, try step
		if (!blocked({r.x, r.y, pos.z + vel.z})) {
			r.z += vel.z;
		} else if (onGround && !didStep && vel.z != 0.0f) {
			for (float sh = 0.1f; sh <= STEP_HEIGHT + 0.01f; sh += 0.1f) {
				if (!blocked({r.x, r.y + sh, pos.z + vel.z})) {
					r.z += vel.z;
					r.y += sh;
					didStep = true;
					break;
				}
			}
		}

		if (stepped) *stepped = didStep;
		return r;
	}
};

} // namespace aicraft
