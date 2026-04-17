#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

namespace civcraft {

enum class CameraMode {
	FirstPerson,
	ThirdPerson,
	RPG,
	RTS,
};

struct PlayerState {
	glm::vec3 feetPos = {0, 0, 0};
	float eyeHeight = 1.9f;
	float bodyHeight = 2.5f;
	float bodyWidth = 0.75f;
	float yaw = -90.0f;              // degrees

	glm::vec3 eyePos() const { return feetPos + glm::vec3(0, eyeHeight, 0); }
};

class Camera {
public:
	PlayerState player;

	CameraMode mode = CameraMode::FirstPerson;
	float mouseSensitivity = 0.1f;
	float moveSpeed = 8.0f;
	float fov = 70.0f;
	float nearPlane = 0.1f;
	float farPlane = 500.0f;

	// TPS/RPG/RTS share 45° look-down; mode switch feels like zoom, not teleport.
	// All three use "camera looks +x at yaw=0" (see updateRTS position formula).
	float orbitDistance = 12.0f;
	float orbitDistanceTarget = 12.0f;
	float orbitYaw = -90.0f;
	float orbitPitch = 45.0f;

	float godDistance = 12.0f;
	float godDistanceTarget = 12.0f;
	float godAngle = 45.0f;
	float godOrbitYaw = -90.0f;

	glm::vec3 godCameraForward() const;
	glm::vec3 godCameraRight() const;

	glm::vec3 rtsCenter = {0, 0, 0};
	float rtsHeight = 7.0f;
	float rtsHeightTarget = 7.0f;
	float rtsAngle = 45.0f;           // elevation deg above horizontal; 90=top-down, 0=horizon
	float rtsOrbitYaw = -90.0f;
	float rtsPanSpeed = 30.0f;
	glm::vec3 rtsPanVel = {0, 0, 0};
	bool rtsEdgeScroll = true;       // Alt bypasses

	// Ctrl+1..4 save, 1..4 recall.
	struct RtsBookmark { glm::vec3 center; float height; float orbitYaw; bool set = false; };
	RtsBookmark rtsBookmarks[4];

	// Zoom pivots around rtsCenter (screen center); cursor-biased felt disorienting.
	float pendingRtsZoom = 0;

	glm::vec3 position = {0, 0, 0};
	float lookYaw = -90.0f;
	float lookPitch = 0.0f;

	void processInput(GLFWwindow* window, float dt);
	void updateRPGPosition(float dt);
	glm::mat4 viewMatrix() const;
	glm::mat4 projectionMatrix(float aspect) const;
	glm::vec3 front() const;

	glm::vec3 playerForward() const;
	glm::vec3 playerRight() const;

	void cycleMode();

	glm::vec3 smoothedFeetPos() const {
		if (!m_smoothInit) return player.feetPos;
		return {player.feetPos.x, m_smoothY, player.feetPos.z};
	}

	void resetSmoothing() { m_smoothY = player.feetPos.y; m_smoothInit = true; }

	// Prevents camera jump after cursor release.
	void resetMouseTracking() { m_firstMouse = true; }
	void updateRTS(GLFWwindow* window, float dt);

private:
	void updateFirstPerson(GLFWwindow* window, float dt);
	void updateThirdPerson(GLFWwindow* window, float dt);
	void processMouse(GLFWwindow* window);

	float smoothVertical(float targetY, float dt);
	float m_smoothY = 0;
	bool m_smoothInit = false;

	bool m_firstMouse = true;
	double m_lastX = 0, m_lastY = 0;

	bool m_rtsBookmarkKeyPrev[4] = {false, false, false, false};
};

} // namespace civcraft
