#pragma once

#include "game/types.h"
#include "server/world.h"
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
	// Called each gameplay frame. Player is an Entity like any other.
	void update(float dt, GameState state, World& world, Entity& player,
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
	                     Camera& camera, Entity& player, World& world,
	                     float jumpVelocity);
	void processBlockInteraction(float dt, GameState state, World& world,
	                             Entity& player, Camera& camera, ControlManager& controls,
	                             Renderer& renderer, ParticleSystem& particles,
	                             Window& window);
	void tickActiveBlocks(float dt, World& world, Renderer& renderer,
	                      ParticleSystem& particles);
	void processItemPickup(float dt, World& world, Entity& player,
	                        Camera& camera, ParticleSystem& particles);

	void resolveActions(float dt, World& world, Entity& player,
	                    Renderer& renderer, ParticleSystem& particles,
	                    GameState state);
	void markBlockDirty(Renderer& renderer, int bx, int by, int bz);

	std::optional<RayHit> m_hit;
	std::optional<EntityHit> m_entityHit;
	EntityId m_inspectedEntity = ENTITY_NONE;
	float m_activeBlockTimer = 0;

	// Client-side input cooldowns (anti-spam, not authoritative)
	float m_breakCD = 0;
	float m_placeCD = 0;

	// Click-to-move for god view
	bool m_hasMoveTarget = false;
	glm::vec3 m_moveTarget = {0, 0, 0};
};

} // namespace agentworld
