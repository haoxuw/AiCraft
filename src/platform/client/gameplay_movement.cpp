#include "client/gameplay.h"
#include "agent/agent_client.h"
#include "shared/physics.h"
#include "shared/entity_physics.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

namespace civcraft {

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
	// Movement state — shared by all modes (RTS virtual joystick, RPG click, WASD)
	float speed = camera.moveSpeed;
	bool sprinting = controls.held(Action::Sprint);
	if (sprinting) speed *= 2.5f;
	glm::vec3 move = {0, 0, 0};
	bool hasWASD = controls.held(Action::MoveForward) ||
	               controls.held(Action::MoveBackward) ||
	               controls.held(Action::MoveLeft) ||
	               controls.held(Action::MoveRight);

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
				// Click with units selected → plan on the client (no server goal).
				// The server is intentionally unaware of the route — we drive
				// each owned unit via incremental Move proposals below.
				glm::vec3 center = glm::vec3(m_hit->blockPos) + glm::vec3(0.5f, 1.0f, 0.5f);
				auto& bl = server.blockRegistry();
				if (bl.get(server.chunks().getBlock(m_hit->blockPos.x, m_hit->blockPos.y, m_hit->blockPos.z)).solid) {
					printf("[RTS] Move order: %d entities → center (%.1f,%.1f,%.1f)\n",
						(int)m_rtsSelect.selected.size(), center.x, center.y, center.z);

					// Build the batch-plan inputs: per-entity start cell.
					std::vector<EntityId>   eids;
					std::vector<glm::ivec3> starts;
					eids.reserve(m_rtsSelect.selected.size());
					starts.reserve(m_rtsSelect.selected.size());
					for (auto eid : m_rtsSelect.selected) {
						Entity* e = server.getEntity(eid);
						if (!e) continue;
						eids.push_back(eid);
						starts.push_back(glm::ivec3(
							(int)std::floor(e->position.x),
							(int)std::floor(e->position.y),
							(int)std::floor(e->position.z)));
					}
					glm::ivec3 gi{(int)std::floor(center.x),
					              (int)std::floor(center.y),
					              (int)std::floor(center.z)};
					m_rtsExec.planGroup(eids, starts, gi,
					                    server.chunks(), server.blockRegistry());

					for (auto eid : m_rtsSelect.selected) {
						m_moveOrders[eid] = {center, true};
						if (m_agentClient) m_agentClient->onOverride(eid, center);
					}
					m_moveTargetPos = center;
					m_hasMoveTarget = true;
				}
			} else if (!isClick) {
				// Drag → box select
				m_rtsSelect.selected.clear();

				float aspect = (float)ww / (float)wh;
				glm::mat4 vp = camera.projectionMatrix(aspect) * camera.viewMatrix();
				EntityId myId = server.localPlayerId();
				bool isAdmin = (state == GameState::ADMIN);
				server.forEachEntity([&](Entity& e) {
					if (!e.def().isLiving()) return;
					// Only select entities the player owns (or all if admin)
					if (!isAdmin) {
						int owner = e.getProp<int>(Prop::Owner, 0);
						if (owner != (int)myId) return;
					}
					glm::vec4 clip = vp * glm::vec4(e.position + glm::vec3(0, 0.5f, 0), 1.0f);
					if (clip.w <= 0) return;
					float sx = clip.x / clip.w;
					float sy = clip.y / clip.w;
					if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1)
						m_rtsSelect.selected.push_back(e.id());
				});
			}
		}

		// RTS: local player uses virtual joystick toward the next waypoint
		// in its client-side plan (falling back to the raw goal if no plan).
		// Other owned units are driven below via ActionProposal::Move.
		{
			auto pit = m_moveOrders.find(player.id());
			if (pit != m_moveOrders.end() && pit->second.active) {
				auto wp = m_rtsExec.steerTargetFor(player.id(), player.position);
				glm::vec3 steer = wp ? *wp : pit->second.target;
				glm::vec3 toTarget = steer - player.position;
				toTarget.y = 0;
				float dist = glm::length(toTarget);
				float finalDist = glm::length(glm::vec3(pit->second.target.x - player.position.x,
				                                        0,
				                                        pit->second.target.z - player.position.z));
				if (finalDist < 1.0f) {
					m_moveOrders.erase(pit);
					m_rtsExec.cancel(player.id());
				} else if (dist > 0.01f) {
					move = glm::normalize(toTarget);
				}
			}
			// Drive every *other* owned commanded entity via Move proposals.
			// Server sees these as ordinary per-tick Moves — it does no nav
			// planning itself.
			m_rtsExec.driveRemote(server, player.id());
			// Clean up stale move-order entries for removed entities.
			std::vector<EntityId> arrived;
			for (auto& [eid, order] : m_moveOrders) {
				if (eid == player.id()) continue;
				if (!order.active || !server.getEntity(eid) || !m_rtsExec.has(eid))
					arrived.push_back(eid);
			}
			for (auto eid : arrived) m_moveOrders.erase(eid);
			if (m_moveOrders.empty()) m_hasMoveTarget = false;
		}
		// Fall through to WASD/physics below — local player gets local moveAndCollide
	}

	// WASD/jump cancels any active move order for the local player.
	// No server message needed — the plan lives only on the client.
	bool wantsJump = controls.held(Action::Jump);
	if ((hasWASD || wantsJump) && m_moveOrders.count(player.id())) {
		m_moveOrders.erase(player.id());
		m_rtsExec.cancel(player.id());
		m_hasMoveTarget = false;
	}

	if (camera.mode == CameraMode::RPG) {
		// RPG: WASD relative to camera orbit direction
		glm::vec3 camFwd = camera.godCameraForward();
		glm::vec3 camRight = camera.godCameraRight();
		if (controls.held(Action::MoveForward))  move += camFwd;
		if (controls.held(Action::MoveBackward)) move -= camFwd;
		if (controls.held(Action::MoveLeft))     move -= camRight;
		if (controls.held(Action::MoveRight))    move += camRight;

		// Left-click move: raycast → set move order (virtual joystick toward target)
		if (controls.pressed(Action::BreakBlock) && m_attackTarget == ENTITY_NONE) {
			double mx, my;
			glfwGetCursorPos(window.handle(), &mx, &my);
			int ww, wh;
			glfwGetWindowSize(window.handle(), &ww, &wh);
			float ndcX = (float)(mx / ww) * 2.0f - 1.0f;
			float ndcY = 1.0f - (float)(my / wh) * 2.0f;
			float aspect = (float)ww / (float)wh;
			glm::mat4 invVP = glm::inverse(camera.projectionMatrix(aspect) * camera.viewMatrix());
			glm::vec4 nearPt = invVP * glm::vec4(ndcX, ndcY, -1, 1);
			glm::vec4 farPt  = invVP * glm::vec4(ndcX, ndcY,  1, 1);
			nearPt /= nearPt.w; farPt /= farPt.w;
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
					printf("[RPG] Click-to-move → (%.1f,%.1f,%.1f) entity=%u\n",
						target.x, target.y, target.z, player.id());
					m_moveOrders[player.id()] = {target, true};
					m_moveTargetPos = target;
					m_hasMoveTarget = true;
					server.sendSetGoal(player.id(), target);
					if (m_agentClient) m_agentClient->onOverride(player.id(), target);
				}
			}
		}

		// RPG click-to-move: virtual joystick toward target for responsive local movement.
		// C_SET_GOAL was already sent on click; server runs nav in parallel.
		// Client drives the local player directly for instant feedback.
		auto pit = m_moveOrders.find(player.id());
		if (pit != m_moveOrders.end() && pit->second.active && !hasWASD) {
			glm::vec3 toTarget = pit->second.target - player.position;
			toTarget.y = 0;
			float dist = glm::length(toTarget);
			if (dist < 1.0f) {
				m_moveOrders.erase(pit);
				m_hasMoveTarget = false;
			} else {
				move = glm::normalize(toTarget);
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
	moveAction.sprint = sprinting;
	moveAction.jumpVelocity = jumpVelocity;

	// Fly: only when entity has fly_mode property set (toggled by F11 in admin)
	bool wantsFly = player.getProp<bool>("fly_mode", false);
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
	bool clientPosInvalid = false;
	{
		auto& chunks = server.chunks();
		auto& blocks = server.blockRegistry();

		// Safety: don't run physics if the ground chunk isn't loaded yet.
		// Without ground data, getBlock returns air → entity falls through void.
		// Hold position and wait for chunk to arrive (server SNAP will correct).
		int feetBx = (int)std::floor(player.position.x);
		int feetBy = (int)std::floor(player.position.y) - 1;
		int feetBz = (int)std::floor(player.position.z);
		ChunkPos feetCp = {feetBx >> 4, feetBy >> 4, feetBz >> 4};
		if (!chunks.getChunk(feetCp)) {
			// Ground chunk not loaded — freeze in place, don't apply gravity.
			// Still send look direction so chunk streaming view-bias is correct.
			player.velocity = {0, 0, 0};
			moveAction.clientPos = player.position;
			moveAction.hasClientPos = true;
			moveAction.desiredVel = {0, 0, 0};
			moveAction.lookPitch = camera.lookPitch;
			moveAction.lookYaw   = camera.lookYaw;
			server.sendAction(moveAction);
			return;
		}

		BlockSolidFn solidFn = [&](int x, int y, int z) -> float {
			const auto& bd = blocks.get(chunks.getBlock(x, y, z));
			return bd.solid ? bd.collision_height : 0.0f;
		};

		glm::vec3 localVel = {moveAction.desiredVel.x, player.velocity.y, moveAction.desiredVel.z};
		if (moveAction.fly)
			localVel.y = moveAction.desiredVel.y;
		else if (moveAction.jump && player.onGround)
			localVel.y = jumpVelocity;

		glm::vec3 prePos = player.position;
		glm::vec3 preVel = player.velocity;
		bool preOnGround = player.onGround;
		stepEntityPhysics(player, localVel, solidFn, dt, moveAction.fly);

		// Defense-in-depth: if physics produced a position that overlaps a block
		// (edge cases around step-up, thin floors, or block changes mid-frame),
		// revert rather than send an invalid clientPos. Server would reject it
		// anyway; reverting keeps client/server mirrors consistent immediately.
		const auto& def = player.def();
		MoveParams mp = makeMoveParams(def.collision_box_min, def.collision_box_max,
		                               def.gravity_scale, def.isLiving(), moveAction.fly);
		clientPosInvalid = isPositionBlocked(solidFn, player.position, mp.halfWidth, mp.height);
		if (clientPosInvalid) {
			player.position = prePos;
			player.velocity = preVel;
			player.onGround = preOnGround;
		}

		// Face movement direction (same logic as server.cpp)
		if (std::abs(localVel.x) > 0.01f || std::abs(localVel.z) > 0.01f)
			player.yaw = glm::degrees(std::atan2(localVel.z, localVel.x));
	}

	moveAction.clientPos    = player.position;
	// When our own collision check flagged the predicted position as in-wall,
	// hand authority back to the server this tick: it will run moveAndCollide
	// with its own block data and write an authoritative position. Continuing
	// to send the reverted clientPos would (a) be a stale position the server
	// may also reject, and (b) keep the client stuck re-proposing it.
	moveAction.hasClientPos = !clientPosInvalid;
	// Send post-physics Y velocity so server stays in sync during jumps/falls.
	// desiredVel.y is normally 0 (non-fly); we override it after local physics.
	moveAction.desiredVel.y = player.velocity.y;
	moveAction.lookPitch = camera.lookPitch;
	moveAction.lookYaw   = camera.lookYaw;
	server.sendAction(moveAction);
}

} // namespace civcraft
