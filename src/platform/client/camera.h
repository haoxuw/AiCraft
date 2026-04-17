#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

namespace civcraft {

enum class CameraMode {
	FirstPerson,  // FPS: camera at player eyes
	ThirdPerson,  // Behind player, orbiting
	RPG,      // Minecraft Dungeons style, fixed angle from above
	RTS,          // Angled overhead, free camera, mouse select
};

// Player physical properties
struct PlayerState {
	glm::vec3 feetPos = {0, 0, 0};  // feet position (bottom of collision box)
	float eyeHeight = 1.9f;          // eye height (scaled with 1.25x model)
	float bodyHeight = 2.5f;         // player is 2.5 blocks tall (1.25x scale)
	float bodyWidth = 0.75f;
	float yaw = -90.0f;              // facing direction (degrees)

	glm::vec3 eyePos() const { return feetPos + glm::vec3(0, eyeHeight, 0); }
};

class Camera {
public:
	// Player state (owned by camera for now, will move to server later)
	PlayerState player;

	// Camera config
	CameraMode mode = CameraMode::FirstPerson;
	float mouseSensitivity = 0.1f;
	float moveSpeed = 8.0f;
	float fov = 70.0f;
	float nearPlane = 0.1f;
	float farPlane = 500.0f;

	// Third-person orbit
	float orbitDistance = 6.0f;
	float orbitDistanceTarget = 6.0f;  // scroll sets target, actual lerps
	float orbitYaw = -90.0f;
	float orbitPitch = 25.0f;    // angle above horizontal

	// RPG camera
	float godDistance = 20.0f;
	float godDistanceTarget = 20.0f;
	float godAngle = 55.0f;      // degrees from horizontal
	float godOrbitYaw = -90.0f;  // camera orbit (independent of player yaw)

	// RPG camera helpers: camera-relative directions on XZ plane
	glm::vec3 godCameraForward() const;
	glm::vec3 godCameraRight() const;

	// RTS
	glm::vec3 rtsCenter = {0, 0, 0};  // camera looks at this point
	float rtsHeight = 40.0f;
	float rtsHeightTarget = 40.0f;
	float rtsAngle = 30.0f;           // elevation above horizontal (deg). 90°=top-down,
	                                  // 0°=horizon. 30° default = clear side view
	                                  // where unit silhouettes (hats, capes) read.
	float rtsOrbitYaw = -90.0f;       // same orbit as RPG (looking south)
	float rtsPanSpeed = 30.0f;
	glm::vec3 rtsPanVel = {0, 0, 0}; // smoothed pan velocity (momentum)
	bool rtsEdgeScroll = true;       // edge-scroll toggle (hold Alt to bypass)

	// Camera bookmarks: Ctrl+1..4 saves current view, 1..4 jumps back.
	struct RtsBookmark { glm::vec3 center; float height; float orbitYaw; bool set = false; };
	RtsBookmark rtsBookmarks[4];

	// Queued from scroll callback. Zoom pivots around rtsCenter (screen
	// center), not the cursor — users found cursor-biased zoom disorienting.
	float pendingRtsZoom = 0;

	// Computed camera state (updated each frame)
	glm::vec3 position = {0, 0, 0};  // actual camera position
	float lookYaw = -90.0f;
	float lookPitch = 0.0f;

	void processInput(GLFWwindow* window, float dt);
	void updateRPGPosition(float dt); // position only, no mouse
	glm::mat4 viewMatrix() const;
	glm::mat4 projectionMatrix(float aspect) const;
	glm::vec3 front() const;         // camera look direction

	// Player-relative forward (horizontal only, for movement)
	glm::vec3 playerForward() const;
	glm::vec3 playerRight() const;

	void cycleMode();

	// Smoothed feet position for model rendering (same X/Z, smoothed Y)
	glm::vec3 smoothedFeetPos() const {
		if (!m_smoothInit) return player.feetPos;
		return {player.feetPos.x, m_smoothY, player.feetPos.z};
	}

	// Reset smooth tracking (call on spawn/teleport)
	void resetSmoothing() { m_smoothY = player.feetPos.y; m_smoothInit = true; }

	// Reset mouse delta tracking (prevents camera jump after cursor release)
	void resetMouseTracking() { m_firstMouse = true; }
	void updateRTS(GLFWwindow* window, float dt); // public: called directly when cursor free

private:
	void updateFirstPerson(GLFWwindow* window, float dt);
	void updateThirdPerson(GLFWwindow* window, float dt);
	void processMouse(GLFWwindow* window);

	// Smoothed vertical tracking for step-up / step-down
	float smoothVertical(float targetY, float dt);
	float m_smoothY = 0;
	bool m_smoothInit = false;

	bool m_firstMouse = true;
	double m_lastX = 0, m_lastY = 0;

	// Edge-detection for bookmark 1..4 keys (press, not hold).
	bool m_rtsBookmarkKeyPrev[4] = {false, false, false, false};
};

} // namespace civcraft
