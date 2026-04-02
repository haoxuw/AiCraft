#include "game/gameplay.h"
#include "client/entity_raycast.h"
#include "shared/physics.h"
#include <glm/gtc/matrix_transform.hpp>
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

	processMovement(dt, state, controls, camera, player, server, window, jumpVelocity);
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
		camera.resetMouseTracking();
	}

	// Cursor state: determined by camera mode + UI overlay
	bool wantCapture = (camera.mode == CameraMode::FirstPerson ||
	                    camera.mode == CameraMode::ThirdPerson);

	bool rpgOrbit = false;
	if (camera.mode == CameraMode::RPG) {
		rpgOrbit = glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
		wantCapture = rpgOrbit;
	}

	// UI overlay overrides: free cursor when inventory/ImGui is active
	if (m_uiWantsCursor)
		wantCapture = false;

	glfwSetInputMode(window.handle(), GLFW_CURSOR,
		wantCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

	if (wantCapture) {
		camera.processInput(window.handle(), dt);
	} else {
		// Cursor free: update camera position but don't process mouse look
		camera.resetMouseTracking();
		switch (camera.mode) {
		case CameraMode::RPG: camera.updateRPGPosition(dt); break;
		case CameraMode::RTS: camera.updateRTS(window.handle(), dt); break;
		default: break; // FPS/TPS: camera frozen when UI is open
		}
	}
}

// ================================================================
// processMovement -- WASD / Sprint / Jump / Descend
// Reads input, builds ActionProposal::Move, sends to server.
// Client NEVER sets entity position or velocity directly.
// ================================================================
void GameplayController::processMovement(float dt, GameState state,
                                         ControlManager& controls,
                                         Camera& camera, Entity& player,
                                         ServerInterface& server, Window& window,
                                         float jumpVelocity)
{
	if (camera.mode == CameraMode::RTS) {
		// RTS: box selection + click-to-move for selected units
		double mx, my;
		glfwGetCursorPos(window.handle(), &mx, &my);
		int ww, wh;
		glfwGetWindowSize(window.handle(), &ww, &wh);
		float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
		float ndcY = 1.0f - (float)(my / wh) * 2.0f;

		bool lmb = controls.held(Action::BreakBlock);
		bool lmbPressed = controls.pressed(Action::BreakBlock);

		// Start box drag
		if (lmbPressed) {
			m_boxDragging = true;
			m_boxStart = {ndcX, ndcY};
			m_boxEnd = {ndcX, ndcY};
		}
		// Update box while dragging
		if (m_boxDragging && lmb) {
			m_boxEnd = {ndcX, ndcY};
		}
		// Release: select entities in box
		if (m_boxDragging && !lmb) {
			m_boxDragging = false;
			float x0 = std::min(m_boxStart.x, m_boxEnd.x);
			float x1 = std::max(m_boxStart.x, m_boxEnd.x);
			float y0 = std::min(m_boxStart.y, m_boxEnd.y);
			float y1 = std::max(m_boxStart.y, m_boxEnd.y);

			// Small drag = click (single select or move order)
			bool isClick = (x1 - x0 < 0.02f && y1 - y0 < 0.02f);

			if (isClick && !m_selectedEntities.empty()) {
				// Click-to-move selected entities to terrain hit point
				if (m_hit) {
					auto& bp = m_hit->blockPos;
					glm::vec3 target = glm::vec3(bp) + glm::vec3(0.5f, 1.0f, 0.5f);
					auto& bl = server.blockRegistry();
					bool ok = bl.get(server.chunks().getBlock(bp.x, bp.y, bp.z)).solid;
					if (ok) {
						m_moveTarget = target;
						m_hasMoveTarget = true;
						// Send move action for each selected entity
						for (EntityId eid : m_selectedEntities) {
							ActionProposal p;
							p.type = ActionProposal::Move;
							p.actorId = eid;
							glm::vec3 toTarget = target - player.position;
							toTarget.y = 0;
							if (glm::length(toTarget) > 0.1f)
								toTarget = glm::normalize(toTarget) * camera.moveSpeed;
							p.desiredVel = {toTarget.x, 0, toTarget.z};
							server.sendAction(p);
						}
					}
				}
			} else if (!isClick) {
				// Box select: find living entities whose screen positions fall inside box
				m_selectedEntities.clear();
				float aspect = (float)ww / (float)wh;
				glm::mat4 vp = camera.projectionMatrix(aspect) * camera.viewMatrix();

				server.forEachEntity([&](Entity& e) {
					if (e.id() == player.id()) return;
					if (e.def().category != Category::Animal &&
					    e.def().category != Category::Player) return;

					// Project entity position to NDC
					glm::vec4 clip = vp * glm::vec4(e.position + glm::vec3(0, 0.5f, 0), 1.0f);
					if (clip.w <= 0) return;
					float sx = clip.x / clip.w;
					float sy = clip.y / clip.w;

					if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) {
						m_selectedEntities.push_back(e.id());
					}
				});
				printf("[RTS] Selected %zu entities\n", m_selectedEntities.size());
			}
		}

		// Right-click: move selected entities
		if (controls.pressed(Action::PlaceBlock) && !m_selectedEntities.empty() && m_hit) {
			auto& bp = m_hit->blockPos;
			glm::vec3 target = glm::vec3(bp) + glm::vec3(0.5f, 1.0f, 0.5f);
			m_moveTarget = target;
			m_hasMoveTarget = true;
		}
		return;
	}

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

		// Click-to-move: raycast from camera through mouse cursor position
		if (controls.pressed(Action::BreakBlock)) {
			// Get mouse position in NDC
			double mx, my;
			glfwGetCursorPos(window.handle(), &mx, &my);
			int ww, wh;
			glfwGetWindowSize(window.handle(), &ww, &wh);
			float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
			float ndcY = 1.0f - (float)(my / wh) * 2.0f;

			// Unproject mouse to world ray
			float aspect = (float)ww / (float)wh;
			glm::mat4 invVP = glm::inverse(
				camera.projectionMatrix(aspect) * camera.viewMatrix());
			glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1, 1);
			glm::vec4 farPt  = invVP * glm::vec4(ndcX, ndcY,  1, 1);
			nearPt /= nearPt.w;
			farPt  /= farPt.w;
			glm::vec3 rayDir = glm::normalize(glm::vec3(farPt - nearPt));

			auto& chunks = server.chunks();
			auto hit = raycastBlocks(chunks, glm::vec3(nearPt), rayDir, 80.0f);
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
	} else if (camera.mode == CameraMode::ThirdPerson) {
		// TPS (Fortnite): WASD relative to camera orbit (not player facing)
		float orbRad = glm::radians(camera.orbitYaw);
		glm::vec3 camFwd = glm::normalize(glm::vec3(std::cos(orbRad), 0, std::sin(orbRad)));
		glm::vec3 camRight = glm::normalize(glm::cross(camFwd, glm::vec3(0, 1, 0)));
		if (controls.held(Action::MoveForward))  move += camFwd;
		if (controls.held(Action::MoveBackward)) move -= camFwd;
		if (controls.held(Action::MoveLeft))     move -= camRight;
		if (controls.held(Action::MoveRight))    move += camRight;
	} else {
		// FPS: WASD relative to look direction
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

	// Apply velocity locally for client-side prediction.
	// Server will validate and may correct, but this makes movement feel instant.
	player.velocity.x = moveAction.desiredVel.x;
	player.velocity.z = moveAction.desiredVel.z;
	if (moveAction.fly) {
		player.velocity.y = moveAction.desiredVel.y;
	} else if (moveAction.jump && player.onGround) {
		player.velocity.y = jumpVelocity;
		player.onGround = false;
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

	// Raycast: origin + direction depends on camera mode
	glm::vec3 rayOrigin, rayDir;

	if (camera.mode == CameraMode::FirstPerson) {
		// FPS: ray from player eyes in camera look direction
		rayOrigin = player.eyePos();
		rayDir = camera.front();
	} else if (camera.mode == CameraMode::ThirdPerson) {
		// TPS: ray from player eyes in camera direction (aim PAST player)
		rayOrigin = player.eyePos();
		rayDir = camera.front();
	} else {
		// RPG/RTS: ray from camera through mouse cursor position
		double mx, my;
		glfwGetCursorPos(window.handle(), &mx, &my);
		int ww, wh;
		glfwGetWindowSize(window.handle(), &ww, &wh);
		float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
		float ndcY = 1.0f - (float)(my / wh) * 2.0f;
		float aspect = (float)ww / (float)wh;
		glm::mat4 invVP = glm::inverse(
			camera.projectionMatrix(aspect) * camera.viewMatrix());
		glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1, 1);
		glm::vec4 farPt  = invVP * glm::vec4(ndcX, ndcY,  1, 1);
		nearPt /= nearPt.w;
		farPt  /= farPt.w;
		rayOrigin = glm::vec3(nearPt);
		rayDir = glm::normalize(glm::vec3(farPt - nearPt));
	}

	float rayDist = (camera.mode == CameraMode::RTS || camera.mode == CameraMode::RPG) ? 80.0f : 6.0f;
	m_hit = raycastBlocks(chunks, rayOrigin, rayDir, rayDist);

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
