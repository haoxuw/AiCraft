#include "game/gameplay.h"
#include "client/entity_raycast.h"
#include "shared/physics.h"
#include <cmath>
#include <algorithm>

namespace aicraft {

// ================================================================
// update -- called each gameplay frame
// ================================================================
void GameplayController::update(float dt, GameState state, World& world,
                                Entity& player, Camera& camera,
                                ControlManager& controls, Renderer& renderer,
                                ParticleSystem& particles, Window& window,
                                float jumpVelocity)
{
	handleCameraInput(dt, controls, camera, window);

	// Number keys 1-9 → hotbar slots 0-8, key 0 → slot 9
	for (int k = GLFW_KEY_1; k <= GLFW_KEY_9; k++) {
		if (glfwGetKey(window.handle(), k) == GLFW_PRESS)
			player.setProp(Prop::SelectedSlot, k - GLFW_KEY_1);
	}
	if (glfwGetKey(window.handle(), GLFW_KEY_0) == GLFW_PRESS)
		player.setProp(Prop::SelectedSlot, 9);

	// ============================================================
	// CLIENT-SIDE ONLY: Collect input → push ActionProposals
	// Server tick (resolveActions, physics, active blocks) runs
	// separately in GameServer::tick(), called by Game::updatePlaying.
	// ============================================================
	processMovement(dt, state, controls, camera, player, world, jumpVelocity);
	processBlockInteraction(dt, state, world, player, camera, controls,
	                        renderer, particles, window);

	// Particles (client-side animation)
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

	// All modes: mouse always controls camera (RPG = always orbit around player)
	camera.processInput(window.handle(), dt);
}

// ================================================================
// processMovement -- WASD / Sprint / Jump / Descend
// Player entity's velocity is set here; EntityManager::step()
// applies physics (same path as pigs, chickens, etc.)
// ================================================================
void GameplayController::processMovement(float dt, GameState state,
                                         ControlManager& controls,
                                         Camera& camera, Entity& player,
                                         World& world, float jumpVelocity)
{
	if (camera.mode == CameraMode::RTS)
		return;

	float speed = camera.moveSpeed;
	if (controls.held(Action::Sprint)) speed *= 2.5f;

	// Compute movement direction based on camera mode
	glm::vec3 move = {0, 0, 0};
	bool hasWASD = controls.held(Action::MoveForward) ||
	               controls.held(Action::MoveBackward) ||
	               controls.held(Action::MoveLeft) ||
	               controls.held(Action::MoveRight);

	if (camera.mode == CameraMode::RPG) {
		glm::vec3 camFwd = camera.godCameraForward();
		glm::vec3 camRight = camera.godCameraRight();
		if (controls.held(Action::MoveForward))  move += camFwd;
		if (controls.held(Action::MoveBackward)) move -= camFwd;
		if (controls.held(Action::MoveLeft))     move -= camRight;
		if (controls.held(Action::MoveRight))    move += camRight;
		if (hasWASD) m_hasMoveTarget = false;

		// Click-to-move
		if (controls.pressed(Action::BreakBlock)) {
			auto hit = raycastBlocks(world, camera.position, camera.front(), 80.0f);
			if (hit) {
				auto& bp = hit->blockPos;
				glm::vec3 target = glm::vec3(bp) + glm::vec3(0.5f, 1.0f, 0.5f);
				bool groundSolid = world.blocks.get(world.getBlock(bp.x, bp.y, bp.z)).solid;
				bool bodyClear = !world.blocks.get(world.getBlock(bp.x, bp.y + 1, bp.z)).solid &&
				                 !world.blocks.get(world.getBlock(bp.x, bp.y + 2, bp.z)).solid;
				if (groundSolid && bodyClear) {
					m_moveTarget = target;
					m_hasMoveTarget = true;
				}
			}
		}

		if (m_hasMoveTarget && !hasWASD) {
			glm::vec3 toTarget = m_moveTarget - player.position;
			toTarget.y = 0;
			float dist = glm::length(toTarget);
			if (dist > 0.5f) {
				move = glm::normalize(toTarget);
			} else {
				m_hasMoveTarget = false;
			}
		}
	} else if (state == GameState::CREATIVE) {
		glm::vec3 fwd = camera.front();
		glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
		if (controls.held(Action::MoveForward))  move += fwd;
		if (controls.held(Action::MoveBackward)) move -= fwd;
		if (controls.held(Action::MoveLeft))     move -= right;
		if (controls.held(Action::MoveRight))    move += right;
	} else {
		glm::vec3 pFwd = camera.playerForward();
		glm::vec3 pRight = camera.playerRight();
		if (controls.held(Action::MoveForward))  move += pFwd;
		if (controls.held(Action::MoveBackward)) move -= pFwd;
		if (controls.held(Action::MoveLeft))     move -= pRight;
		if (controls.held(Action::MoveRight))    move += pRight;
	}

	if (glm::length(move) > 0.01f) move = glm::normalize(move);

	// CLIENT only says WHERE it wants to go — never writes entity state.
	// Server (resolveActions) decides if the move is valid.
	ActionProposal moveAction;
	moveAction.type = ActionProposal::Move;
	moveAction.actorId = player.id();
	moveAction.desiredVel = {move.x * speed, 0, move.z * speed};
	moveAction.jumpVelocity = jumpVelocity;

	// Client REQUESTS fly — server validates against game rules
	bool wantsFly = (state == GameState::CREATIVE) &&
	                (camera.mode == CameraMode::FirstPerson || camera.mode == CameraMode::ThirdPerson);
	moveAction.fly = wantsFly;

	if (wantsFly) {
		if (controls.held(Action::Jump))    moveAction.desiredVel.y = speed;
		if (controls.held(Action::Descend)) moveAction.desiredVel.y = -speed;
	} else {
		moveAction.jump = controls.held(Action::Jump);
	}

	world.actions.propose(moveAction);

	// God view / third person: player model faces movement direction
	if (camera.mode == CameraMode::RPG || camera.mode == CameraMode::ThirdPerson) {
		if (glm::length(move) > 0.01f) {
			float targetYaw = glm::degrees(std::atan2(move.z, move.x));
			float diff = targetYaw - camera.player.yaw;
			while (diff > 180.0f) diff -= 360.0f;
			while (diff < -180.0f) diff += 360.0f;
			camera.player.yaw += diff * std::min(dt * 6.0f, 1.0f);
		}
	}
}

// ================================================================
// processBlockInteraction -- raycast, break, place
// ================================================================
void GameplayController::processBlockInteraction(float dt, GameState state,
                                                 World& world, Entity& player,
                                                 Camera& camera,
                                                 ControlManager& controls,
                                                 Renderer& renderer,
                                                 ParticleSystem& particles,
                                                 Window& window)
{
	// Raycast
	glm::vec3 rayOrigin = camera.position;
	glm::vec3 rayDir = camera.front();
	if (camera.mode == CameraMode::FirstPerson)
		rayOrigin = player.eyePos();

	m_hit = raycastBlocks(world, rayOrigin, rayDir, 6.0f);

	// Entity raycast: detect entities under crosshair for tooltips
	{
		std::vector<RaycastEntity> rayEntities;
		world.entities.forEach([&](Entity& e) {
			if (e.id() == player.id()) return; // skip self
			rayEntities.push_back({
				e.id(), e.typeId(), e.position,
				e.def().collision_box_min, e.def().collision_box_max,
				e.goalText, e.hasError
			});
		});
		m_entityHit = raycastEntities(rayEntities, rayOrigin, rayDir, 6.0f, player.id());
	}

	// Client-side cooldown timers (anti-spam only — server validates independently)
	m_breakCD -= dt;
	m_placeCD -= dt;

	int selectedSlot = player.getProp<int>(Prop::SelectedSlot, 0);

	// Right-click on entity → inspect (prioritize over block placement)
	if (m_entityHit && controls.pressed(Action::PlaceBlock)) {
		// Check entity is closer than block hit
		if (!m_hit || m_entityHit->distance < m_hit->distance) {
			m_inspectedEntity = m_entityHit->entityId;
		}
	}

	// Block interaction — CLIENT only pushes proposals, NEVER modifies world.
	// Server (resolveActions) validates and executes.
	if (m_hit && camera.mode != CameraMode::RTS && player.inventory) {
		// --- Break block ---
		if (controls.pressed(Action::BreakBlock) && m_breakCD <= 0) {
			auto& bp = m_hit->blockPos;
			BlockId bid = world.getBlock(bp.x, bp.y, bp.z);
			const BlockDef& bdef = world.blocks.get(bid);

			ActionProposal p;
			p.actorId = player.id();
			p.blockPos = bp;

			if (bdef.string_id == BlockType::TNT) {
				p.type = ActionProposal::IgniteTNT;
				m_breakCD = 0.3f;
			} else {
				p.type = ActionProposal::BreakBlock;
				m_breakCD = 0.15f;
			}
			world.actions.propose(p);
		}

		// --- Place block ---
		if (controls.pressed(Action::PlaceBlock) && m_placeCD <= 0) {
			std::string placeType = player.inventory->hotbar(selectedSlot);
			if (!placeType.empty() && player.inventory->has(placeType)
			    && world.blocks.find(placeType) != nullptr) {
				ActionProposal p;
				p.type = ActionProposal::PlaceBlock;
				p.actorId = player.id();
				p.blockPos = m_hit->placePos;
				p.blockType = placeType;
				p.slotIndex = selectedSlot;
				world.actions.propose(p);
				m_placeCD = 0.2f;
			}
		}
	}
}

// ================================================================
// resolveActions -- Phase 1: drain action queue, validate, execute
// Server is the sole modifier of world state.
// ================================================================
void GameplayController::resolveActions(float dt, World& world, Entity& player,
                                         Renderer& renderer, ParticleSystem& particles,
                                         GameState state)
{
	auto proposals = world.actions.drain();

	for (auto& p : proposals) {
		switch (p.type) {

		case ActionProposal::Move: {
			Entity* e = world.entities.get(p.actorId);
			if (!e) break;

			// Server validates: clamp to entity's max speed
			float maxSpeed = e->def().walk_speed;
			if (maxSpeed > 0) {
				float len = glm::length(glm::vec2(p.desiredVel.x, p.desiredVel.z));
				if (len > maxSpeed * 3.0f) { // allow sprint (3x base)
					float scale = (maxSpeed * 3.0f) / len;
					p.desiredVel.x *= scale;
					p.desiredVel.z *= scale;
				}
			}

			// SERVER sets fly_mode — client only requests it.
			// Server could validate against game rules (e.g., creative-only).
			e->setProp("fly_mode", p.fly);

			e->velocity.x = p.desiredVel.x;
			e->velocity.z = p.desiredVel.z;

			if (p.fly) {
				e->velocity.y = p.desiredVel.y;
			} else if (p.jump) {
				if (e->onGround) {
					// Normal ground jump
					e->velocity.y = p.jumpVelocity;
					e->onGround = false;
				} else if (e->inventory && e->inventory->has(ItemId::Jetpack)) {
					// Jetpack: continuous thrust while holding jump in air.
					// Adds upward force that exceeds gravity, capped at 80% jump speed.
					e->velocity.y += 35.0f * dt;
					e->velocity.y = std::min(e->velocity.y, p.jumpVelocity * 0.8f);
					e->setProp("jetpack_active", 1);
				}
			}
			// Clear jetpack flag if not thrusting
			if (!(p.jump && !e->onGround && e->inventory && e->inventory->has(ItemId::Jetpack))) {
				e->setProp("jetpack_active", 0);
			}
			break;
		}

		case ActionProposal::BreakBlock: {
			Entity* actor = world.entities.get(p.actorId);
			auto& bp = p.blockPos;
			ChunkPos cp = worldToChunk(bp.x, bp.y, bp.z);
			Chunk* c = world.getChunk(cp);
			if (!c) break;

			BlockId bid = world.getBlock(bp.x, bp.y, bp.z);
			if (bid == BLOCK_AIR) break;
			const BlockDef& bdef = world.blocks.get(bid);

			// Validate: block must be within reach (6 blocks)
			if (actor) {
				float dist = glm::length(glm::vec3(bp) - actor->position);
				if (dist > 8.0f) break;
			}

			// Execute: remove block, drop item, particles
			particles.emitBlockBreak(glm::vec3(bp), bdef.color_top);
			world.removeBlockState(bp.x, bp.y, bp.z);

			if (actor && actor->inventory) {
				std::string dropType = bdef.drop.empty() ? bdef.string_id : bdef.drop;
				if (!dropType.empty() && dropType != BlockType::Air) {
					if (state == GameState::CREATIVE) {
						actor->inventory->add(dropType, 1);
					} else {
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
			break;
		}

		case ActionProposal::PlaceBlock: {
			Entity* actor = world.entities.get(p.actorId);
			auto& pp = p.blockPos;

			// Validate: position must be air
			if (world.getBlock(pp.x, pp.y, pp.z) != BLOCK_AIR) break;

			// Validate: must not be inside the actor
			if (actor) {
				auto& fp = actor->position;
				float hw = (actor->def().collision_box_max.x - actor->def().collision_box_min.x) * 0.5f;
				float ph = actor->def().collision_box_max.y;
				bool inside = pp.x >= (int)std::floor(fp.x - hw) &&
				              pp.x <= (int)std::floor(fp.x + hw) &&
				              pp.z >= (int)std::floor(fp.z - hw) &&
				              pp.z <= (int)std::floor(fp.z + hw) &&
				              pp.y >= (int)std::floor(fp.y) &&
				              pp.y <= (int)std::floor(fp.y + ph);
				if (inside) break;

				float dist = glm::length(glm::vec3(pp) - fp);
				if (dist > 8.0f) break;
			}

			// Validate: actor has the block type
			if (actor && actor->inventory) {
				if (!actor->inventory->has(p.blockType)) break;
			}

			const BlockDef* placedDef = world.blocks.find(p.blockType);
			if (!placedDef) break;

			ChunkPos cp = worldToChunk(pp.x, pp.y, pp.z);
			Chunk* c = world.getChunk(cp);
			if (!c) break;

			c->set(((pp.x % 16) + 16) % 16, ((pp.y % 16) + 16) % 16,
			       ((pp.z % 16) + 16) % 16,
			       world.blocks.getId(p.blockType));
			renderer.markChunkDirty(cp);
			markBlockDirty(renderer, pp.x, pp.y, pp.z);

			if (placedDef->behavior == BlockBehavior::Active) {
				world.setBlockState(pp.x, pp.y, pp.z, placedDef->default_state);
			}

			if (actor && actor->inventory && state == GameState::SURVIVAL) {
				actor->inventory->remove(p.blockType, 1);
			}
			break;
		}

		case ActionProposal::IgniteTNT: {
			auto& bp = p.blockPos;
			BlockId bid = world.getBlock(bp.x, bp.y, bp.z);
			const BlockDef& bdef = world.blocks.get(bid);
			if (bdef.string_id != BlockType::TNT) break;

			auto* tntState = world.getBlockState(bp.x, bp.y, bp.z);
			if (!tntState) {
				world.setBlockState(bp.x, bp.y, bp.z,
					{{Prop::Lit, 1}, {Prop::FuseTicks, 60}});
			} else if ((*tntState)[Prop::Lit] == 0) {
				(*tntState)[Prop::Lit] = 1;
				(*tntState)[Prop::FuseTicks] = 60;
			}
			break;
		}

		case ActionProposal::GrowCrop:
			// Future: advance crop growth stage
			break;

		case ActionProposal::Attack:
			// Future: apply damage to target entity
			break;

		case ActionProposal::PickupItem:
			// Future: validate proximity, add to inventory
			break;
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
	if (m_activeBlockTimer >= 0.05f) {
		world.tickActiveBlocks(m_activeBlockTimer, [&](int bx, int by, int bz, BlockId) {
			ChunkPos cp = worldToChunk(bx, by, bz);
			renderer.markChunkDirty(cp);
			for (int a = 0; a < 3; a++) {
				int coords[] = {bx, by, bz};
				int l = ((coords[a] % 16) + 16) % 16;
				if (l == 0 || l == 15) {
					glm::ivec3 o(0); o[a] = (l == 0) ? -1 : 1;
					renderer.markChunkDirty({cp.x + o.x, cp.y + o.y, cp.z + o.z});
				}
			}
			particles.emitBlockBreak(glm::vec3(bx, by, bz), {0.9f, 0.7f, 0.2f}, 4);
		});
		m_activeBlockTimer = 0;
	}
}

// ================================================================
// processItemPickup -- attract items, add to inventory, emit particles
// ================================================================
void GameplayController::processItemPickup(float dt, World& world,
                                           Entity& player, Camera& camera,
                                           ParticleSystem& particles)
{
	if (!player.inventory) return;

	glm::vec3 playerCenter = player.position + glm::vec3(0, 1, 0);
	auto pickups = world.entities.attractItemsToward(playerCenter, 3.0f, 1.2f, dt);
	for (auto* item : pickups) {
		std::string itemType = item->getProp<std::string>(Prop::ItemType);
		int count = item->getProp<int>(Prop::Count, 1);
		// Counter inventory: always accepts all items (infinite stacks)
		player.inventory->add(itemType, count);
		particles.emitItemPickup(item->position, {0.8f, 0.9f, 1.0f});
		item->removed = true;
	}
}

// ================================================================
// markBlockDirty -- mark chunk + neighboring chunks on chunk edge
// ================================================================
void GameplayController::markBlockDirty(Renderer& renderer,
                                        int bx, int by, int bz)
{
	ChunkPos cp = worldToChunk(bx, by, bz);
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
