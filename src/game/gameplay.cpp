#include "game/gameplay.h"
#include "game/player.h"
#include <cmath>
#include <algorithm>

namespace aicraft {

// ================================================================
// update -- called each gameplay frame
// ================================================================
void GameplayController::update(float dt, GameState state, World& world,
                                Player& player, Camera& camera,
                                ControlManager& controls, Renderer& renderer,
                                ParticleSystem& particles, Window& window)
{
	handleCameraInput(dt, controls, camera, window);
	processMovement(dt, state, controls, camera, player, world);
	processBlockInteraction(dt, state, world, player, camera, controls,
	                        renderer, particles);
	tickActiveBlocks(dt, world, renderer, particles);
	processItemPickup(dt, world, player, camera, particles);

	// Step entities
	world.entities.step(dt, [&](int x, int y, int z) {
		return world.blocks.get(world.getBlock(x, y, z)).solid;
	});

	// Particles
	particles.update(dt);
}

// ================================================================
// handleCameraInput -- cycle view, toggle cursor, mouse look
// ================================================================
void GameplayController::handleCameraInput(float dt, ControlManager& controls,
                                           Camera& camera, Window& window)
{
	// V = cycle camera view
	if (controls.pressed(Action::CycleView)) {
		camera.cycleMode();
		if (camera.mode == CameraMode::RTS)
			glfwSetInputMode(window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		else
			glfwSetInputMode(window.handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}

	// Camera look (mouse)
	camera.processInput(window.handle(), dt);
}

// ================================================================
// processMovement -- WASD / Sprint / Jump / Descend
// ================================================================
void GameplayController::processMovement(float dt, GameState state,
                                         ControlManager& controls,
                                         Camera& camera, Player& player,
                                         World& world)
{
	if (camera.mode == CameraMode::RTS)
		return;

	glm::vec3 pFwd = camera.playerForward();
	glm::vec3 pRight = camera.playerRight();

	float speed = camera.moveSpeed;
	if (controls.held(Action::Sprint)) speed *= 2.5f;

	if (state == GameState::CREATIVE) {
		glm::vec3 fwd = camera.front();
		glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
		glm::vec3 move = {0, 0, 0};
		if (controls.held(Action::MoveForward))  move += fwd;
		if (controls.held(Action::MoveBackward)) move -= fwd;
		if (controls.held(Action::MoveLeft))     move -= right;
		if (controls.held(Action::MoveRight))    move += right;
		if (glm::length(move) > 0.01f) move = glm::normalize(move);

		player.applyCreativeMovement(move, speed, dt,
		                             controls.held(Action::Jump),
		                             controls.held(Action::Descend),
		                             camera.player.feetPos);
	} else {
		glm::vec3 move = {0, 0, 0};
		if (controls.held(Action::MoveForward))  move += pFwd;
		if (controls.held(Action::MoveBackward)) move -= pFwd;
		if (controls.held(Action::MoveLeft))     move -= pRight;
		if (controls.held(Action::MoveRight))    move += pRight;
		if (glm::length(move) > 0.01f) move = glm::normalize(move);

		player.applySurvivalMovement(move, speed, dt,
		                             controls.held(Action::Jump),
		                             world, camera.player.feetPos);
	}

	// Unstuck check
	player.unstuck(world, camera.player.feetPos);
}

// ================================================================
// processBlockInteraction -- raycast, break, place
// ================================================================
void GameplayController::processBlockInteraction(float dt, GameState state,
                                                 World& world, Player& player,
                                                 Camera& camera,
                                                 ControlManager& controls,
                                                 Renderer& renderer,
                                                 ParticleSystem& particles)
{
	// Raycast
	glm::vec3 rayOrigin = camera.position;
	glm::vec3 rayDir = camera.front();
	if (camera.mode == CameraMode::FirstPerson)
		rayOrigin = camera.player.eyePos();

	m_hit = raycastBlocks(world, rayOrigin, rayDir, 6.0f);

	player.tickCooldowns(dt);

	// Block interaction
	bool blockHandled = false;
	if (m_hit && camera.mode != CameraMode::RTS) {
		// --- Break block ---
		if (controls.pressed(Action::BreakBlock) && player.breakCD <= 0) {
			auto& bp = m_hit->blockPos;
			ChunkPos cp = World::worldToChunk(bp.x, bp.y, bp.z);
			Chunk* c = world.getChunk(cp);
			if (c) {
				BlockId bid = world.getBlock(bp.x, bp.y, bp.z);
				const BlockDef& bdef = world.blocks.get(bid);

				// TNT: left-click lights it instead of breaking
				if (bdef.string_id == BlockType::TNT) {
					auto* tntState = world.getBlockState(bp.x, bp.y, bp.z);
					if (!tntState) {
						world.setBlockState(bp.x, bp.y, bp.z,
							{{Prop::Lit, 1}, {Prop::FuseTicks, 60}});
					} else if ((*tntState)[Prop::Lit] == 0) {
						(*tntState)[Prop::Lit] = 1;
						(*tntState)[Prop::FuseTicks] = 60;
					}
					player.breakCD = 0.3f;
					blockHandled = true;
				}

				if (!blockHandled) {
					// Particle effect: use block top color
					particles.emitBlockBreak(glm::vec3(bp), bdef.color_top);

					// Remove active block state
					world.removeBlockState(bp.x, bp.y, bp.z);

					// Drop item
					if (bid != BLOCK_AIR) {
						std::string dropType = bdef.drop.empty() ? bdef.string_id : bdef.drop;
						if (!dropType.empty() && dropType != BlockType::Air) {
							// In creative: add to inventory directly
							if (state == GameState::CREATIVE) {
								player.inventory.addItem(dropType, 1);
							} else {
								// Survival: spawn item entity
								glm::vec3 dropPos = glm::vec3(bp) + glm::vec3(0.5f, 0.5f, 0.5f);
								world.entities.spawn(EntityType::ItemEntity, dropPos,
									{{Prop::ItemType, dropType}, {Prop::Count, 1}, {Prop::Age, 0.0f}});
							}
						}
					}

					c->set(((bp.x % 16) + 16) % 16, ((bp.y % 16) + 16) % 16,
					       ((bp.z % 16) + 16) % 16, BLOCK_AIR);
					renderer.markChunkDirty(cp);
					markBlockDirty(renderer, bp.x, bp.y, bp.z);
					player.breakCD = 0.15f;
				} // !blockHandled
			}
		}

		// --- Place block ---
		if (controls.pressed(Action::PlaceBlock) && player.placeCD <= 0) {
			auto& pp = m_hit->placePos;
			auto& fp = camera.player.feetPos;
			bool inside = pp.x >= (int)std::floor(fp.x - 0.3f) &&
			              pp.x <= (int)std::floor(fp.x + 0.3f) &&
			              pp.z >= (int)std::floor(fp.z - 0.3f) &&
			              pp.z <= (int)std::floor(fp.z + 0.3f) &&
			              pp.y >= (int)std::floor(fp.y) &&
			              pp.y <= (int)std::floor(fp.y + 2.5f);
			if (!inside) {
				auto& slot = player.inventory.slot(player.selectedSlot);
				if (!slot.empty()) {
					ChunkPos cp = World::worldToChunk(pp.x, pp.y, pp.z);
					Chunk* c = world.getChunk(cp);
					if (c) {
						c->set(((pp.x % 16) + 16) % 16, ((pp.y % 16) + 16) % 16,
						       ((pp.z % 16) + 16) % 16,
						       world.blocks.getId(slot.type));
						renderer.markChunkDirty(cp);
						markBlockDirty(renderer, pp.x, pp.y, pp.z);

						// Register active block state if this is an active block
						const BlockDef* placedDef = world.blocks.find(slot.type);
						if (placedDef && placedDef->behavior == BlockBehavior::Active) {
							world.setBlockState(pp.x, pp.y, pp.z, placedDef->default_state);
						}

						// Consume in survival, refill in creative
						if (state == GameState::SURVIVAL) {
							slot.remove(1);
						} else {
							slot.count = 64; // creative: infinite
						}
					}
					player.placeCD = 0.2f;
				}
			}
		}
	}
}

// ================================================================
// tickActiveBlocks -- timer accumulator, ~20 tps
// ================================================================
void GameplayController::tickActiveBlocks(float dt, World& world,
                                          Renderer& renderer,
                                          ParticleSystem& particles)
{
	m_activeBlockTimer += dt;
	if (m_activeBlockTimer >= 0.05f) { // ~20 tps
		world.tickActiveBlocks(m_activeBlockTimer, [&](int bx, int by, int bz, BlockId) {
			ChunkPos cp = World::worldToChunk(bx, by, bz);
			renderer.markChunkDirty(cp);
			// Also mark neighbors for mesh updates
			for (int a = 0; a < 3; a++) {
				int coords[] = {bx, by, bz};
				int l = ((coords[a] % 16) + 16) % 16;
				if (l == 0 || l == 15) {
					glm::ivec3 o(0); o[a] = (l == 0) ? -1 : 1;
					renderer.markChunkDirty({cp.x + o.x, cp.y + o.y, cp.z + o.z});
				}
			}
			// Spawn explosion particles
			particles.emitBlockBreak(glm::vec3(bx, by, bz), {0.9f, 0.7f, 0.2f}, 4);
		});
		m_activeBlockTimer = 0;
	}
}

// ================================================================
// processItemPickup -- attract items, add to inventory, emit particles
// ================================================================
void GameplayController::processItemPickup(float dt, World& world,
                                           Player& player, Camera& camera,
                                           ParticleSystem& particles)
{
	glm::vec3 playerCenter = camera.player.feetPos + glm::vec3(0, 1, 0);
	auto pickups = world.entities.attractItemsToward(playerCenter, 3.0f, 1.2f, dt);
	for (auto* item : pickups) {
		std::string itemType = item->getProp<std::string>(Prop::ItemType);
		int count = item->getProp<int>(Prop::Count, 1);
		int leftover = player.inventory.addItem(itemType, count);
		if (leftover < count) {
			// Picked up at least some
			particles.emitItemPickup(item->position, {0.8f, 0.9f, 1.0f});
			if (leftover == 0) {
				item->removed = true;
			} else {
				item->setProp(Prop::Count, leftover);
			}
		}
	}
}

// ================================================================
// markBlockDirty -- mark chunk + neighboring chunks on chunk edge
// ================================================================
void GameplayController::markBlockDirty(Renderer& renderer,
                                        int bx, int by, int bz)
{
	ChunkPos cp = World::worldToChunk(bx, by, bz);
	for (int a = 0; a < 3; a++) {
		int coords[] = {bx, by, bz};
		int l = ((coords[a] % 16) + 16) % 16;
		if (l == 0 || l == 15) {
			glm::ivec3 o(0);
			o[a] = (l == 0) ? -1 : 1;
			renderer.markChunkDirty({cp.x + o.x, cp.y + o.y, cp.z + o.z});
		}
	}
}

} // namespace aicraft
