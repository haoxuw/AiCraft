#pragma once

#include "game/types.h"
#include "common/world.h"
#include "common/constants.h"
#include <glad/gl.h>
#include "client/camera.h"
#include "client/renderer.h"
#include "client/controls.h"
#include "client/particles.h"
#include "client/raycast.h"
#include "client/window.h"
#include <optional>

namespace aicraft {

class Player;

class GameplayController {
public:
	// Called each gameplay frame. Updates world state, player, etc.
	void update(float dt, GameState state, World& world, Player& player,
	            Camera& camera, ControlManager& controls, Renderer& renderer,
	            ParticleSystem& particles, Window& window);

	// The raycast hit from this frame (for HUD and highlight rendering)
	const std::optional<RayHit>& currentHit() const { return m_hit; }

private:
	void handleCameraInput(float dt, ControlManager& controls, Camera& camera, Window& window);
	void processMovement(float dt, GameState state, ControlManager& controls,
	                     Camera& camera, Player& player, World& world);
	void processBlockInteraction(float dt, GameState state, World& world,
	                             Player& player, Camera& camera, ControlManager& controls,
	                             Renderer& renderer, ParticleSystem& particles);
	void tickActiveBlocks(float dt, World& world, Renderer& renderer,
	                      ParticleSystem& particles);
	void processItemPickup(float dt, World& world, Player& player,
	                        Camera& camera, ParticleSystem& particles);

	void markBlockDirty(Renderer& renderer, int bx, int by, int bz);

	std::optional<RayHit> m_hit;
	float m_activeBlockTimer = 0;
};

} // namespace aicraft
