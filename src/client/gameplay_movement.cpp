#include "client/gameplay.h"
#include "shared/physics.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

namespace modcraft {

// ================================================================
// processMovement -- WASD / Sprint / Jump / Click-to-move / RTS
// Reads input, builds ActionProposal::Move, runs client-side physics
// (same moveAndCollide as server), then sends clientPos to server.
// ================================================================
void GameplayController::processMovement(float dt, GameState state,
                                         ControlManager& controls,
                                         Camera& camera, Entity& player,
                                         ServerInterface& server, Window& window,
                                         float jumpVelocity)
{
	if (camera.mode == CameraMode::RTS) {
		// ── RTS: left-click = box select (drag) OR move units (click) ──
		double mx, my;
		glfwGetCursorPos(window.handle(), &mx, &my);
		int ww, wh;
		glfwGetWindowSize(window.handle(), &ww, &wh);
		float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
		float ndcY = 1.0f - (float)(my / wh) * 2.0f;

		bool lmb = controls.held(Action::BreakBlock);
		bool lmbPressed = controls.pressed(Action::BreakBlock);

		if (lmbPressed) {
			m_rtsSelect.dragging = true;
			m_rtsSelect.start = {ndcX, ndcY};
			m_rtsSelect.end = {ndcX, ndcY};
		}
		if (m_rtsSelect.dragging && lmb) {
			m_rtsSelect.end = {ndcX, ndcY};
		}
		if (m_rtsSelect.dragging && !lmb) {
			m_rtsSelect.dragging = false;
			float x0 = std::min(m_rtsSelect.start.x, m_rtsSelect.end.x);
			float x1 = std::max(m_rtsSelect.start.x, m_rtsSelect.end.x);
			float y0 = std::min(m_rtsSelect.start.y, m_rtsSelect.end.y);
			float y1 = std::max(m_rtsSelect.start.y, m_rtsSelect.end.y);
			bool isClick = (x1 - x0 < 0.02f && y1 - y0 < 0.02f);

			if (isClick && !m_rtsSelect.selected.empty() && m_hit) {
				// Click with units selected → move to target (Red Alert style)
				issueRTSMoveOrder(m_hit->blockPos, server, camera);
			} else if (!isClick) {
				// Drag → box select (stop currently moving entities first)
				for (auto& [eid, _] : m_rtsSelect.moveTargets) {
					ActionProposal p;
					p.type = ActionProposal::Move;
					p.actorId = eid;
					p.desiredVel = {0, 0, 0};
					server.sendAction(p);
				}
				m_rtsSelect.moveTargets.clear();
				m_rtsSelect.selected.clear();
				m_clickToMove.active = false;

				float aspect = (float)ww / (float)wh;
				glm::mat4 vp = camera.projectionMatrix(aspect) * camera.viewMatrix();
				server.forEachEntity([&](Entity& e) {
					if (!e.def().isLiving()) return; // only living entities
					glm::vec4 clip = vp * glm::vec4(e.position + glm::vec3(0, 0.5f, 0), 1.0f);
					if (clip.w <= 0) return;
					float sx = clip.x / clip.w;
					float sy = clip.y / clip.w;
					if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1)
						m_rtsSelect.selected.push_back(e.id());
				});
			}
		}

		// ── Continuous movement: send Move each frame until entities arrive ──
		if (!m_rtsSelect.moveTargets.empty()) {
			std::vector<EntityId> arrived;
			for (auto& [eid, target] : m_rtsSelect.moveTargets) {
				Entity* e = server.getEntity(eid);
				if (!e) { arrived.push_back(eid); continue; }

				glm::vec3 toTarget = target - e->position;
				toTarget.y = 0;
				float dist = glm::length(toTarget);

				ActionProposal p;
				p.type = ActionProposal::Move;
				p.actorId = eid;

				if (dist <= 0.5f) {
					arrived.push_back(eid);
					p.desiredVel = {0, 0, 0};
				} else {
					// Check if entity is stuck (has velocity but not moving)
					float hSpeed = std::sqrt(e->velocity.x*e->velocity.x + e->velocity.z*e->velocity.z);
					auto prevIt = m_rtsLastPos.find(eid);
					if (prevIt != m_rtsLastPos.end() && hSpeed > 0.5f) {
						float moved = glm::length(glm::vec2(
							e->position.x - prevIt->second.x,
							e->position.z - prevIt->second.z));
						if (moved < 0.05f * dt) {
							// Stuck against a wall — stop pushing
							arrived.push_back(eid);
							p.desiredVel = {0, 0, 0};
							server.sendAction(p);
							continue;
						}
					}
					m_rtsLastPos[eid] = e->position;

					glm::vec3 dir = glm::normalize(toTarget);
					float spd = e->def().walk_speed;
					if (spd <= 0) spd = camera.moveSpeed;
					p.desiredVel = {dir.x * spd, 0, dir.z * spd};
				}
				server.sendAction(p);
			}
			for (auto eid : arrived) {
				m_rtsSelect.moveTargets.erase(eid);
				m_rtsLastPos.erase(eid);
			}
			if (m_rtsSelect.moveTargets.empty())
				m_clickToMove.active = false;
		}

		return; // RTS: no WASD player movement
	}

	// ── FPS / TPS / RPG: WASD movement ──
	float speed = camera.moveSpeed;
	if (controls.held(Action::Sprint)) speed *= 2.5f;

	glm::vec3 move = {0, 0, 0};
	bool hasWASD = controls.held(Action::MoveForward) ||
	               controls.held(Action::MoveBackward) ||
	               controls.held(Action::MoveLeft) ||
	               controls.held(Action::MoveRight);

	if (camera.mode == CameraMode::RPG) {
		// RPG: WASD relative to camera orbit direction
		glm::vec3 camFwd = camera.godCameraForward();
		glm::vec3 camRight = camera.godCameraRight();
		if (controls.held(Action::MoveForward))  move += camFwd;
		if (controls.held(Action::MoveBackward)) move -= camFwd;
		if (controls.held(Action::MoveLeft))     move -= camRight;
		if (controls.held(Action::MoveRight))    move += camRight;
		if (hasWASD) m_clickToMove.active = false;

		// Left-click move: raycast from camera through mouse cursor
		// Skip if processBlockInteraction already claimed the click as an entity attack
		if (controls.pressed(Action::BreakBlock) && m_attackTarget == ENTITY_NONE) {
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
					m_clickToMove.target = target;
					m_clickToMove.active = true;
				}
			}
		}

		// Continuous tracking toward click-to-move target
		if (m_clickToMove.active && !hasWASD) {
			glm::vec3 toTarget = m_clickToMove.target - player.position;
			toTarget.y = 0;
			float dist = glm::length(toTarget);
			if (dist > 0.5f) {
				move = glm::normalize(toTarget);
			} else {
				m_clickToMove.active = false;
			}
		}
	} else if (state == GameState::ADMIN &&
	           (camera.mode == CameraMode::FirstPerson || camera.mode == CameraMode::ThirdPerson)) {
		// Admin fly mode: 3D movement along camera look direction (FPS/TPS only)
		glm::vec3 fwd = camera.front();
		glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
		if (controls.held(Action::MoveForward))  move += fwd;
		if (controls.held(Action::MoveBackward)) move -= fwd;
		if (controls.held(Action::MoveLeft))     move -= right;
		if (controls.held(Action::MoveRight))    move += right;
	} else if (camera.mode == CameraMode::ThirdPerson) {
		// TPS: WASD relative to camera orbit (Fortnite-style)
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

	// Client-side physics: run the same moveAndCollide() as the server so
	// movement feels local — walls block, gravity pulls, step-up works.
	// Server accepts clientPos and normally agrees; it only snaps on large errors.
	{
		auto& chunks = server.chunks();
		auto& blocks = server.blockRegistry();
		BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
			const auto& bd = blocks.get(chunks.getBlock(x, y, z));
			return bd.solid ? bd.collision_height : 0.0f;
		};

		const auto& def = player.def();
		MoveParams mp;
		mp.halfWidth  = (def.collision_box_max.x - def.collision_box_min.x) * 0.5f;
		mp.height     = def.collision_box_max.y - def.collision_box_min.y;
		mp.gravity    = 32.0f * def.gravity_scale;
		mp.stepHeight = def.isLiving() ? 1.0f : 0.0f;
		mp.canFly     = moveAction.fly;
		mp.smoothStep = false;

		glm::vec3 localVel = {moveAction.desiredVel.x, player.velocity.y, moveAction.desiredVel.z};
		if (moveAction.fly)
			localVel.y = moveAction.desiredVel.y;
		else if (moveAction.jump && player.onGround)
			localVel.y = jumpVelocity;

		auto result = moveAndCollide(solidFn, player.position, localVel, dt, mp, player.onGround);
		player.position = result.position;
		player.velocity = result.velocity;
		player.onGround = result.onGround;

		// Face movement direction (same logic as server.cpp)
		if (std::abs(localVel.x) > 0.01f || std::abs(localVel.z) > 0.01f)
			player.yaw = glm::degrees(std::atan2(localVel.z, localVel.x));
	}

	moveAction.clientPos    = player.position;
	moveAction.hasClientPos = true;
	moveAction.lookPitch = camera.lookPitch;
	moveAction.lookYaw   = camera.player.yaw;
	server.sendAction(moveAction);
}

// ================================================================
// issueRTSMoveOrder -- compute grid formation and assign targets
// ================================================================
void GameplayController::issueRTSMoveOrder(glm::ivec3 blockPos,
                                           ServerInterface& server,
                                           Camera& camera)
{
	glm::vec3 center = glm::vec3(blockPos) + glm::vec3(0.5f, 1.0f, 0.5f);
	auto& bl = server.blockRegistry();
	if (!bl.get(server.chunks().getBlock(blockPos.x, blockPos.y, blockPos.z)).solid)
		return;

	m_clickToMove.target = center;
	m_clickToMove.active = true;
	m_rtsSelect.moveTargets.clear();
	m_rtsLastPos.clear();

	int n = (int)m_rtsSelect.selected.size();
	int cols = std::max(1, (int)std::ceil(std::sqrt((float)n)));
	int rows = (n + cols - 1) / cols;
	float spacing = 2.0f;
	float offX = (cols - 1) * spacing * 0.5f;
	float offZ = (rows - 1) * spacing * 0.5f;

	for (int i = 0; i < n; i++) {
		glm::vec3 pos = center + glm::vec3(
			(i % cols) * spacing - offX, 0,
			(i / cols) * spacing - offZ);
		m_rtsSelect.moveTargets[m_rtsSelect.selected[i]] = pos;
	}
}

} // namespace modcraft
