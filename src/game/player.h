#pragma once

#include "common/types.h"
#include "server/world.h"
#include "shared/inventory.h"
#include "shared/constants.h"
#include "shared/block_registry.h"
#include "shared/physics.h"
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
	float jumpCD = 0.0f;
	bool jetpackActive = false;     // true while jetpack is thrusting (for HUD/particles)
	static constexpr float JUMP_COOLDOWN = 0.3f;

	Player() {}

	// --------------------------------------------------------
	// Inventory helpers
	// --------------------------------------------------------

	void fillCreativeInventory() {
		inventory.clear();
		inventory.add(BlockType::Stone, 999);
		inventory.add(BlockType::Dirt, 999);
		inventory.add(BlockType::Grass, 999);
		inventory.add(BlockType::Sand, 999);
		inventory.add(BlockType::Wood, 999);
		inventory.add(BlockType::Leaves, 999);
		inventory.add(BlockType::Snow, 999);
		inventory.add(BlockType::TNT, 999);
		inventory.add(BlockType::Cobblestone, 999);
		inventory.add(ItemId::Jetpack, 1);

		// Default hotbar: 1-9 = common blocks, 0 = TNT
		inventory.setHotbar(0, BlockType::Stone);
		inventory.setHotbar(1, BlockType::Dirt);
		inventory.setHotbar(2, BlockType::Grass);
		inventory.setHotbar(3, BlockType::Sand);
		inventory.setHotbar(4, BlockType::Wood);
		inventory.setHotbar(5, BlockType::Leaves);
		inventory.setHotbar(6, BlockType::Snow);
		inventory.setHotbar(7, BlockType::Cobblestone);
		inventory.setHotbar(8, ItemId::Jetpack);
		inventory.setHotbar(9, BlockType::TNT);
	}

	void fillSurvivalInventory() {
		inventory.clear();
		inventory.add(BlockType::Stone, 10);
		inventory.add(BlockType::Wood, 10);
		inventory.add(ItemId::Jetpack, 1);

		inventory.setHotbar(0, BlockType::Stone);
		inventory.setHotbar(1, BlockType::Wood);
		inventory.setHotbar(2, ItemId::Jetpack);
	}

	// --------------------------------------------------------
	// Movement
	// --------------------------------------------------------

	// Unified movement for ALL modes. Uses shared physics.
	// Creative mode: fly=true, no gravity, ascend/descend with Space/Ctrl
	// Survival mode: fly=false, gravity + jump + step-up
	void applyMovement(glm::vec3 horizontalMove, float speed, float dt,
	                   bool jump, bool fly, bool ascend, bool descend,
	                   float jumpVelocity,
	                   World& world, glm::vec3& feetPos) {
		velocity.x = horizontalMove.x * speed;
		velocity.z = horizontalMove.z * speed;

		jumpCD -= dt;
		jetpackActive = false;

		if (fly) {
			velocity.y = 0;
			if (ascend)  velocity.y = speed;
			if (descend) velocity.y = -speed;
		} else {
			bool hasJetpack = inventory.has(ItemId::Jetpack);
			if (jump && jumpCD <= 0 && onGround) {
				// Normal ground jump
				velocity.y = jumpVelocity;
				onGround = false;
				jumpCD = JUMP_COOLDOWN;
			} else if (jump && hasJetpack && !onGround) {
				// Jetpack: continuous upward thrust while holding space in air.
				// Counteracts gravity and adds lift. Feels like a real jetpack.
				float thrust = 35.0f; // must exceed gravity (28) to gain altitude
				velocity.y += thrust * dt;
				velocity.y = std::min(velocity.y, jumpVelocity * 0.8f); // cap upward speed
				jetpackActive = true;
			}
		}

		MoveParams params;
		params.halfWidth = 0.375f;
		params.height = 2.5f;
		params.gravity = 28.0f;
		params.stepHeight = 1.0f;
		params.canFly = fly;

		BlockSolidFn solidFn = [&](int x, int y, int z) {
			return world.blocks.get(world.getBlock(x, y, z)).solid;
		};
		auto result = moveAndCollide(solidFn, feetPos, velocity, dt, params, onGround);

		feetPos = result.position;
		velocity = result.velocity;
		onGround = result.onGround;

		if (!fly) {
			hunger -= 0.005f * dt;
			if (hunger < 0) hunger = 0;
		}
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
};

} // namespace aicraft
