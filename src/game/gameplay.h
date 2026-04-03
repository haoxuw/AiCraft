#pragma once

#include "game/types.h"
#include "shared/server_interface.h"
#include "shared/entity.h"
#include "shared/constants.h"
#include "client/gl.h"
#include "client/camera.h"
#include "client/renderer.h"
#include "client/controls.h"
#include "client/particles.h"
#include "client/raycast.h"
#include "client/entity_raycast.h"
#include "client/window.h"
#include <optional>
#include <unordered_map>

namespace agentworld {

class GameplayController {
public:
	// Called each gameplay frame. Gathers client input → ActionProposals.
	void update(float dt, GameState state, ServerInterface& server, Entity& player,
	            Camera& camera, ControlManager& controls, Renderer& renderer,
	            ParticleSystem& particles, Window& window,
	            float jumpVelocity = 17.0f);

	// --- Query state (for HUD / renderer) ---

	const std::optional<RayHit>& currentHit() const { return m_hit; }
	const std::optional<EntityHit>& currentEntityHit() const { return m_entityHit; }

	EntityId inspectedEntity() const { return m_inspectedEntity; }
	void clearInspection() { m_inspectedEntity = ENTITY_NONE; }

	bool hasMoveTarget() const { return m_clickToMove.active; }
	glm::vec3 moveTarget() const { return m_clickToMove.target; }

	bool isBoxDragging() const { return m_rtsSelect.dragging; }
	glm::vec2 boxStart() const { return m_rtsSelect.start; }
	glm::vec2 boxEnd() const { return m_rtsSelect.end; }
	const std::vector<EntityId>& selectedEntities() const { return m_rtsSelect.selected; }

	// Set by game loop: true when inventory/ImGui/chat is open and wants cursor
	void setUIWantsCursor(bool v) { m_uiWantsCursor = v; }

private:
	// --- Subsystems (each has its own .cpp implementation) ---

	void handleCameraInput(float dt, ControlManager& controls, Camera& camera, Window& window);
	void processMovement(float dt, GameState state, ControlManager& controls,
	                     Camera& camera, Entity& player, ServerInterface& server,
	                     Window& window, float jumpVelocity);
	void processBlockInteraction(float dt, GameState state, ServerInterface& server,
	                             Entity& player, Camera& camera, ControlManager& controls,
	                             Window& window);
	void issueRTSMoveOrder(glm::ivec3 blockPos, ServerInterface& server, Camera& camera);

	// --- Raycast results (set per frame by processBlockInteraction) ---

	std::optional<RayHit> m_hit;
	std::optional<EntityHit> m_entityHit;
	EntityId m_inspectedEntity = ENTITY_NONE;

	// --- Input cooldowns ---

	float m_breakCD = 0;
	bool m_uiWantsCursor = false;

	// --- Right-click state (RPG/RTS: drag → orbit, quick click → action) ---

	struct RightClick {
		bool held = false;
		bool orbiting = false;
		bool action = false;      // set on release if no drag
		double startX = 0, startY = 0;
	} m_rightClick;

	// --- Click-to-move (RPG player + RTS visual indicator) ---

	struct ClickToMove {
		bool active = false;
		glm::vec3 target = {0, 0, 0};
	} m_clickToMove;

	// --- RTS box selection + unit movement ---

	struct RTSSelection {
		bool dragging = false;
		glm::vec2 start = {0, 0};
		glm::vec2 end = {0, 0};
		std::vector<EntityId> selected;
		// Per-entity move targets (grid formation), continuously tracked
		std::unordered_map<EntityId, glm::vec3> moveTargets;
	} m_rtsSelect;
};

} // namespace agentworld
