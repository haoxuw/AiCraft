#include "client/gameplay.h"
#include "client/entity_raycast.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace modcraft {

// ================================================================
// processBlockInteraction -- raycast, break/place, inspect
//
// Mouse button mapping per mode:
//   FPS/TPS: left = break block (crosshair), right = inspect entity
//   RPG:     left = click-to-move (handled in processMovement),
//            right = inspect entity / orbit camera
//   RTS:     left = box select + move (handled in processMovement),
//            right = inspect entity / orbit camera
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

	if (camera.mode == CameraMode::FirstPerson ||
	    camera.mode == CameraMode::ThirdPerson) {
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

	bool isTopDown = (camera.mode == CameraMode::RTS || camera.mode == CameraMode::RPG);
	float rayDist = isTopDown ? 80.0f : 6.0f;
	m_hit = raycastBlocks(chunks, rayOrigin, rayDir, rayDist);

	// Entity raycast: detect entities under crosshair for tooltips/inspect
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
		m_entityHit = raycastEntities(rayEntities, rayOrigin, rayDir, rayDist, player.id());
	}

	m_breakCD -= dt;
	m_hitEvent.happened = false;
	m_placeEvent.happened = false;
	m_attackTarget = ENTITY_NONE;
	m_doorToggled = false;

	// Break progress decay: cancel if no hit for 2 seconds
	if (m_breaking.active) {
		m_breaking.timer += dt;
		if (m_breaking.timer > 2.0f) {
			m_breaking.active = false;
			m_breaking.hits = 0;
		}
	}

	// ── Entity inspect: right-click in all modes ──
	if (m_entityHit) {
		bool inspectTrigger = isTopDown
			? m_rightClick.action : controls.pressed(Action::PlaceBlock);
		if (inspectTrigger && (!m_hit || m_entityHit->distance < m_hit->distance)) {
			m_inspectedEntity = m_entityHit->entityId;
			m_rightClick.action = false;
		}
	}

	// ── Entity attack: left-click on living entity in any camera mode ──
	// Takes priority over block break (FPS/TPS) and click-to-move (RPG).
	// Does NOT set m_swingTriggered here — the attack dispatch in game.cpp
	// handles the swing with weapon-appropriate duration (and only when the
	// item actually allows attacking, preventing "potion swing").
	if (m_entityHit && controls.pressed(Action::BreakBlock)) {
		bool entityIsNearer = !m_hit || m_entityHit->distance <= m_hit->distance;
		if (entityIsNearer)
			m_attackTarget = m_entityHit->entityId;
	}

	// ── FPS/TPS only: left-click break, right-click place block ──
	bool entityAttack = (m_attackTarget != ENTITY_NONE);
	if (m_hit && !entityAttack && (camera.mode == CameraMode::FirstPerson ||
	              camera.mode == CameraMode::ThirdPerson) && player.inventory) {
		// Left-click: break / ignite TNT
		if (controls.pressed(Action::BreakBlock) && m_breakCD <= 0) {
			m_swingTriggered = true; // trigger first-person hand swing
			auto& bp = m_hit->blockPos;
			BlockId bid = chunks.getBlock(bp.x, bp.y, bp.z);
			const BlockDef& bdef = blocks.get(bid);

			if (bdef.string_id == BlockType::TNT) {
				ActionProposal p;
				p.actorId = player.id();
				p.blockPos = bp;
				p.type = ActionProposal::IgniteTNT;
				m_breakCD = 0.3f;
				server.sendAction(p);
			} else if (state == GameState::ADMIN) {
				// Admin: instant break
				ActionProposal p;
				p.actorId = player.id();
				p.blockPos = bp;
				p.type = ActionProposal::BreakBlock;
				m_breakCD = 0.15f;
				server.sendAction(p);
			} else {
				// Survival: 3 hits to break
				if (m_breaking.active && m_breaking.target == bp) {
					m_breaking.hits++;
				} else {
					m_breaking.target = bp;
					m_breaking.hits = 1;
					m_breaking.active = true;
				}
				m_breaking.timer = 0;
				m_breakCD = 0.25f;

				// Per-hit feedback event (particles + sound handled by game.cpp)
				m_hitEvent.happened = true;
				m_hitEvent.pos = glm::vec3(bp) + glm::vec3(0.5f);
				m_hitEvent.color = bdef.color_top;

				if (m_breaking.hits >= 3) {
					ActionProposal p;
					p.actorId = player.id();
					p.blockPos = bp;
					p.type = ActionProposal::BreakBlock;
					server.sendAction(p);
					m_breaking.active = false;
					m_breaking.hits = 0;
				}
			}
		}

		// Right-click: interact with block or place block (no entity closer)
		bool entityCloser = m_entityHit && m_entityHit->distance < m_hit->distance;
		if (controls.pressed(Action::PlaceBlock) && !entityCloser && m_breakCD <= 0) {
			auto& bp = m_hit->blockPos;
			BlockId bid = chunks.getBlock(bp.x, bp.y, bp.z);
			const std::string& blockStr = blocks.get(bid).string_id;

			if (blockStr == BlockType::Door || blockStr == BlockType::DoorOpen) {
				// Door interaction: toggle open/closed
				ActionProposal p;
				p.actorId = player.id();
				p.type = ActionProposal::InteractBlock;
				p.blockPos = bp;
				server.sendAction(p);
				m_breakCD = 0.3f;
				m_doorToggled = true;
				m_doorTogglePos = glm::vec3(bp) + glm::vec3(0.5f);
			} else {
				// Place block from inventory
				int slot = player.getProp<int>(Prop::SelectedSlot, 0);
				const std::string& blockType = player.inventory->hotbar(slot);
				if (!blockType.empty() && player.inventory->has(blockType)) {
					ActionProposal p;
					p.actorId = player.id();
					p.type = ActionProposal::PlaceBlock;
					p.blockPos = m_hit->placePos;
					p.blockType = blockType;
					server.sendAction(p);
					m_breakCD = 0.25f;
					m_placeEvent.happened = true;
					m_placeEvent.pos = glm::vec3(m_hit->placePos) + glm::vec3(0.5f);
					m_placeEvent.blockType = blockType;
				}
			}
		}
	}
}

} // namespace modcraft
