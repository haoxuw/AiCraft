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

namespace agentworld {

class GameplayController {
public:
	// Called each gameplay frame. Gathers client input → ActionProposals.
	// Server handles resolution, physics, active blocks, item pickup.
	void update(float dt, GameState state, ServerInterface& server, Entity& player,
	            Camera& camera, ControlManager& controls, Renderer& renderer,
	            ParticleSystem& particles, Window& window,
	            float jumpVelocity = 17.0f);

	// The raycast hit from this frame (for HUD and highlight rendering)
	const std::optional<RayHit>& currentHit() const { return m_hit; }
	const std::optional<EntityHit>& currentEntityHit() const { return m_entityHit; }

	// Entity inspection: set when player right-clicks an entity
	EntityId inspectedEntity() const { return m_inspectedEntity; }
	void clearInspection() { m_inspectedEntity = ENTITY_NONE; }

private:
	void handleCameraInput(float dt, ControlManager& controls, Camera& camera, Window& window);
	void processMovement(float dt, GameState state, ControlManager& controls,
	                     Camera& camera, Entity& player, ServerInterface& server,
	                     Window& window, float jumpVelocity);
	void processBlockInteraction(float dt, GameState state, ServerInterface& server,
	                             Entity& player, Camera& camera, ControlManager& controls,
	                             Window& window);

	std::optional<RayHit> m_hit;
	std::optional<EntityHit> m_entityHit;
	EntityId m_inspectedEntity = ENTITY_NONE;

	// Client-side input cooldowns (anti-spam, not authoritative)
	float m_breakCD = 0;
	float m_placeCD = 0;

	// Click-to-move for RPG/RTS
	bool m_hasMoveTarget = false;
	glm::vec3 m_moveTarget = {0, 0, 0};
public:
	bool hasMoveTarget() const { return m_hasMoveTarget; }
	glm::vec3 moveTarget() const { return m_moveTarget; }
private:
};

} // namespace agentworld
