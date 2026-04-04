#include "client/gameplay.h"
#include <cmath>

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
// handleCameraInput -- cycle view, right-click orbit, cursor state
// ================================================================
void GameplayController::handleCameraInput(float dt, ControlManager& controls,
                                           Camera& camera, Window& window)
{
	m_rightClick.action = false; // reset each frame

	if (controls.pressed(Action::CycleView)) {
		camera.cycleMode();
		camera.resetMouseTracking();
	}

	// Cursor state: determined by camera mode + UI overlay
	bool wantCapture = (camera.mode == CameraMode::FirstPerson ||
	                    camera.mode == CameraMode::ThirdPerson);

	// RPG/RTS: right-click hold+drag = orbit camera, quick click = action
	if (camera.mode == CameraMode::RPG || camera.mode == CameraMode::RTS) {
		bool rmb = glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

		if (rmb && !m_rightClick.held) {
			// Just pressed — record start position
			m_rightClick.held = true;
			m_rightClick.orbiting = false;
			glfwGetCursorPos(window.handle(), &m_rightClick.startX, &m_rightClick.startY);
		}

		if (rmb && m_rightClick.held && !m_rightClick.orbiting) {
			// Check if mouse moved enough to count as drag
			double cx, cy;
			glfwGetCursorPos(window.handle(), &cx, &cy);
			double ddx = cx - m_rightClick.startX, ddy = cy - m_rightClick.startY;
			if (ddx * ddx + ddy * ddy > 25.0) {
				m_rightClick.orbiting = true;
				camera.resetMouseTracking();
			}
		}

		if (!rmb && m_rightClick.held) {
			// Just released
			m_rightClick.held = false;
			if (!m_rightClick.orbiting)
				m_rightClick.action = true; // quick click → action
			m_rightClick.orbiting = false;
		}

		wantCapture = m_rightClick.orbiting;
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

} // namespace agentworld
