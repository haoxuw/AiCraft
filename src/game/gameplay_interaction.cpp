#include "game/gameplay.h"
#include "client/entity_raycast.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace agentworld {

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

	// ── Entity inspect: right-click in all modes ──
	if (m_entityHit) {
		bool inspectTrigger = isTopDown
			? m_rightClick.action : controls.pressed(Action::PlaceBlock);
		if (inspectTrigger && (!m_hit || m_entityHit->distance < m_hit->distance)) {
			m_inspectedEntity = m_entityHit->entityId;
			m_rightClick.action = false;
		}
	}

	// ── FPS/TPS only: left-click break, right-click place block ──
	if (m_hit && (camera.mode == CameraMode::FirstPerson ||
	              camera.mode == CameraMode::ThirdPerson) && player.inventory) {
		// Left-click: break / ignite TNT
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

		// Right-click: place block (only if no entity is closer)
		bool entityCloser = m_entityHit && m_entityHit->distance < m_hit->distance;
		if (controls.pressed(Action::PlaceBlock) && !entityCloser && m_breakCD <= 0) {
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
			}
		}
	}
}

} // namespace agentworld
