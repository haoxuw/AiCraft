#include "game/gameplay.h"
#include "client/entity_raycast.h"
#include "shared/physics.h"
#include <cmath>
#include <algorithm>

namespace agentworld {

// ================================================================
// update -- called each gameplay frame (CLIENT-SIDE ONLY)
//
// Gathers player input and submits ActionProposals to the server.
// The server (GameServer::tick) handles:
//   - Action validation + execution (resolveActions)
//   - Physics simulation (stepPhysics)
//   - Active block ticking (TNT, crops)
//   - Item pickup
//
// This means a LAN client has identical gameplay to singleplayer —
// both submit the same ActionProposals via ServerInterface.
// ================================================================
void GameplayController::update(float dt, GameState state, ServerInterface& server,
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

	processMovement(dt, state, controls, camera, player, server, jumpVelocity);
	processBlockInteraction(dt, state, server, player, camera, controls, window);

	// Particles (client-side animation only)
	particles.update(dt);
}

// ================================================================
// handleCameraInput -- cycle view, toggle cursor, mouse look
// ================================================================
void GameplayController::handleCameraInput(float dt, ControlManager& controls,
                                           Camera& camera, Window& window)
{
	if (controls.pressed(Action::CycleView)) {
		camera.cycleMode();
		if (camera.mode == CameraMode::RTS)
			glfwSetInputMode(window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		else
			glfwSetInputMode(window.handle(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}
	camera.processInput(window.handle(), dt);
}

// ================================================================
// processMovement -- WASD / Sprint / Jump / Descend
// Reads input, builds ActionProposal::Move, sends to server.
// Client NEVER sets entity position or velocity directly.
// ================================================================
void GameplayController::processMovement(float dt, GameState state,
                                         ControlManager& controls,
                                         Camera& camera, Entity& player,
                                         ServerInterface& server, float jumpVelocity)
{
	if (camera.mode == CameraMode::RTS)
		return;

	float speed = camera.moveSpeed;
	if (controls.held(Action::Sprint)) speed *= 2.5f;

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
			auto& chunks = server.chunks();
			auto hit = raycastBlocks(chunks, camera.position, camera.front(), 80.0f);
			if (hit) {
				auto& bp = hit->blockPos;
				glm::vec3 target = glm::vec3(bp) + glm::vec3(0.5f, 1.0f, 0.5f);
				auto& blocks = server.blockRegistry();
				bool groundSolid = blocks.get(chunks.getBlock(bp.x, bp.y, bp.z)).solid;
				bool bodyClear = !blocks.get(chunks.getBlock(bp.x, bp.y + 1, bp.z)).solid &&
				                 !blocks.get(chunks.getBlock(bp.x, bp.y + 2, bp.z)).solid;
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
	} else if (state == GameState::ADMIN) {
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

	// Build ActionProposal — server validates and executes
	ActionProposal moveAction;
	moveAction.type = ActionProposal::Move;
	moveAction.actorId = player.id();
	moveAction.desiredVel = {move.x * speed, 0, move.z * speed};
	moveAction.jumpVelocity = jumpVelocity;

	bool wantsFly = (state == GameState::ADMIN) &&
	                (camera.mode == CameraMode::FirstPerson || camera.mode == CameraMode::ThirdPerson);
	moveAction.fly = wantsFly;

	if (wantsFly) {
		if (controls.held(Action::Jump))    moveAction.desiredVel.y = speed;
		if (controls.held(Action::Descend)) moveAction.desiredVel.y = -speed;
	} else {
		moveAction.jump = controls.held(Action::Jump);
	}

	server.sendAction(moveAction);

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
// processBlockInteraction -- raycast, break, place proposals
// Client reads world via ChunkSource, sends proposals to server.
// Server validates range, ownership, and executes.
// ================================================================
void GameplayController::processBlockInteraction(float dt, GameState state,
                                                 ServerInterface& server,
                                                 Entity& player,
                                                 Camera& camera,
                                                 ControlManager& controls,
                                                 Window& window)
{
	auto& chunks = server.chunks();
	auto& blocks = server.blockRegistry();

	// Raycast blocks
	glm::vec3 rayOrigin = camera.position;
	glm::vec3 rayDir = camera.front();
	if (camera.mode == CameraMode::FirstPerson)
		rayOrigin = player.eyePos();

	m_hit = raycastBlocks(chunks, rayOrigin, rayDir, 6.0f);

	// Entity raycast: detect entities under crosshair for tooltips
	{
		std::vector<RaycastEntity> rayEntities;
		server.forEachEntity([&](Entity& e) {
			if (e.id() == player.id()) return;
			rayEntities.push_back({
				e.id(), e.typeId(), e.position,
				e.def().collision_box_min, e.def().collision_box_max,
				e.goalText, e.hasError
			});
		});
		m_entityHit = raycastEntities(rayEntities, rayOrigin, rayDir, 6.0f, player.id());
	}

	m_breakCD -= dt;
	m_placeCD -= dt;

	int selectedSlot = player.getProp<int>(Prop::SelectedSlot, 0);

	// Right-click on entity → inspect
	if (m_entityHit && controls.pressed(Action::PlaceBlock)) {
		if (!m_hit || m_entityHit->distance < m_hit->distance) {
			m_inspectedEntity = m_entityHit->entityId;
		}
	}

	// Block interaction — CLIENT only pushes proposals, NEVER modifies world
	if (m_hit && camera.mode != CameraMode::RTS && player.inventory) {
		// Break block
		if (controls.pressed(Action::BreakBlock) && m_breakCD <= 0) {
			auto& bp = m_hit->blockPos;
			BlockId bid = chunks.getBlock(bp.x, bp.y, bp.z);
			const BlockDef& bdef = blocks.get(bid);

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
			server.sendAction(p);
		}

		// Place block
		if (controls.pressed(Action::PlaceBlock) && m_placeCD <= 0) {
			std::string placeType = player.inventory->hotbar(selectedSlot);
			if (!placeType.empty() && player.inventory->has(placeType)
			    && blocks.find(placeType) != nullptr) {
				ActionProposal p;
				p.type = ActionProposal::PlaceBlock;
				p.actorId = player.id();
				p.blockPos = m_hit->placePos;
				p.blockType = placeType;
				p.slotIndex = selectedSlot;
				server.sendAction(p);
				m_placeCD = 0.2f;
			}
		}
	}
}

} // namespace agentworld
