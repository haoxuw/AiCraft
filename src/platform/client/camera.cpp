#include "client/camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace civcraft {

glm::vec3 Camera::front() const {
	float y = glm::radians(lookYaw), p = glm::radians(lookPitch);
	return glm::normalize(glm::vec3(cos(y)*cos(p), sin(p), sin(y)*cos(p)));
}

glm::vec3 Camera::playerForward() const {
	float y = glm::radians(player.yaw);
	return glm::normalize(glm::vec3(cos(y), 0, sin(y)));
}

glm::vec3 Camera::playerRight() const {
	return glm::normalize(glm::cross(playerForward(), glm::vec3(0, 1, 0)));
}

glm::mat4 Camera::viewMatrix() const {
	return glm::lookAt(position, position + front(), glm::vec3(0, 1, 0));
}

glm::mat4 Camera::projectionMatrix(float aspect) const {
	return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

void Camera::cycleMode() {
	int m = ((int)mode + 1) % 4;
	mode = (CameraMode)m;
	m_firstMouse = true;
}

void Camera::processMouse(GLFWwindow* window) {
	double mx, my;
	glfwGetCursorPos(window, &mx, &my);
	if (m_firstMouse) { m_lastX = mx; m_lastY = my; m_firstMouse = false; }
	float dx = (float)(mx - m_lastX) * mouseSensitivity;
	float dy = (float)(m_lastY - my) * mouseSensitivity;
	m_lastX = mx; m_lastY = my;

	switch (mode) {
	case CameraMode::FirstPerson:
		lookYaw += dx; lookPitch += dy;
		lookPitch = std::clamp(lookPitch, -89.0f, 89.0f);
		player.yaw = lookYaw;
		break;
	case CameraMode::ThirdPerson:
		// Fortnite-style: mouse drives the aim ray exactly like FPS.
		// Camera position is derived from the look direction in
		// updateThirdPerson — no orbit angle anymore.
		lookYaw += dx; lookPitch += dy;
		lookPitch = std::clamp(lookPitch, -60.0f, 60.0f);
		player.yaw = lookYaw;
		break;
	case CameraMode::RPG:
		godOrbitYaw += dx;
		godAngle -= dy;
		godAngle = std::clamp(godAngle, -60.0f, 85.0f);
		break;
	case CameraMode::RTS:
		// RMB drag = orbit (X yaw, Y pitch); MMB drag vertical = zoom.
		if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
			rtsOrbitYaw += dx * 2.5f;
			rtsAngle = std::clamp(rtsAngle + dy * 0.5f, 5.0f, 89.0f);
		}
		if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
			pendingRtsZoom += dy * 0.6f;
		}
		break;
	}
}

void Camera::processInput(GLFWwindow* window, float dt) {
	processMouse(window);

	switch (mode) {
	case CameraMode::FirstPerson: updateFirstPerson(window, dt); break;
	case CameraMode::ThirdPerson: updateThirdPerson(window, dt); break;
	case CameraMode::RPG:         updateRPGPosition(dt); break;
	case CameraMode::RTS:         updateRTS(window, dt); break;
	}
}

// Asymmetric: gentle climb, responsive fall. Max-lag cap prevents long trails on drops.
float Camera::smoothVertical(float targetY, float dt) {
	if (!m_smoothInit) {
		m_smoothY = targetY;
		m_smoothInit = true;
		return targetY;
	}

	float diff = targetY - m_smoothY;

	const float kMaxLag = 1.1f;
	if (diff > kMaxLag)  { m_smoothY = targetY - kMaxLag; diff =  kMaxLag; }
	if (diff < -kMaxLag) { m_smoothY = targetY + kMaxLag; diff = -kMaxLag; }

	float rate;
	if (diff > 0.01f) {
		rate = 9.0f + diff * 5.0f;
	} else if (diff < -0.01f) {
		rate = 24.0f;
	} else {
		m_smoothY = targetY;
		return targetY;
	}

	m_smoothY += diff * std::min(rate * dt, 1.0f);
	return m_smoothY;
}

void Camera::updateFirstPerson(GLFWwindow* window, float dt) {
	float smoothedFeetY = smoothVertical(player.feetPos.y, dt);
	position = glm::vec3(player.feetPos.x, smoothedFeetY + player.eyeHeight, player.feetPos.z);
}

void Camera::updateThirdPerson(GLFWwindow* window, float dt) {
	orbitDistance += (orbitDistanceTarget - orbitDistance) * std::min(dt * 10.0f, 1.0f);

	float smoothedFeetY = smoothVertical(player.feetPos.y, dt);
	glm::vec3 charCenter(player.feetPos.x, smoothedFeetY + player.eyeHeight * 0.8f, player.feetPos.z);

	// Fortnite-style: lookYaw/Pitch already set by mouse (FPS-style aim).
	// Camera sits BEHIND (along horizontal forward) and ABOVE the character,
	// so the character renders in the lower-middle of the screen and the
	// crosshair lands wherever the player is aiming. Position uses
	// horizontal-forward only — the camera's height stays constant when
	// you tilt up/down to aim.
	glm::vec3 camFront = front();
	glm::vec3 camFwdH = glm::normalize(glm::vec3(camFront.x, 0.0f, camFront.z));

	constexpr float kHeightOffset = 2.0f;   // bigger → character drops further down on screen
	position = charCenter - camFwdH * orbitDistance + glm::vec3(0, kHeightOffset, 0);
	// lookYaw/lookPitch already correct from processMouse; no recomputation.
}

void Camera::updateRPGPosition(float dt) {
	godDistance += (godDistanceTarget - godDistance) * std::min(dt * 10.0f, 1.0f);

	float angle = glm::radians(godAngle);
	float yaw = glm::radians(godOrbitYaw);

	glm::vec3 offset(
		-cos(yaw) * cos(angle) * godDistance,
		sin(angle) * godDistance,
		-sin(yaw) * cos(angle) * godDistance
	);

	float smoothedFeetY = smoothVertical(player.feetPos.y, dt);
	glm::vec3 target(player.feetPos.x, smoothedFeetY + player.eyeHeight * 0.8f, player.feetPos.z);
	position = target + offset;

	glm::vec3 dir = glm::normalize(target - position);
	lookYaw = glm::degrees(atan2(dir.z, dir.x));
	lookPitch = glm::degrees(asin(dir.y));
}

void Camera::updateRTS(GLFWwindow* window, float dt) {
	// SC2/WoW-style: WASD/edge pan, RMB orbit, MMB/scroll zoom, Q/E rotate, Home reset.
	// Ctrl+1..4 save bookmark, 1..4 recall. Ctrl = fast pan (Shift reserved for box-select).
	smoothVertical(player.feetPos.y, dt);

	bool fast = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
	            glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
	bool altHeld = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
	               glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;

	// Matches TPS/RPG: cam at rtsCenter + (-cos(yaw), _, -sin(yaw))*D → "up on screen" = +x at yaw=0.
	float yaw = glm::radians(rtsOrbitYaw);
	glm::vec3 fwd = glm::normalize(glm::vec3(cos(yaw), 0, sin(yaw)));
	glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));

	glm::vec3 inputDir(0);
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) inputDir += fwd;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) inputDir -= fwd;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) inputDir -= right;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) inputDir += right;

	if (rtsEdgeScroll && !altHeld) {
		double mx, my;
		glfwGetCursorPos(window, &mx, &my);
		int w, h;
		glfwGetWindowSize(window, &w, &h);
		const float edgeMargin = 18.0f;  // narrow to avoid unintentional drift
		if (mx >= 0 && mx < edgeMargin)     inputDir -= right;
		if (mx > w - edgeMargin && mx <= w) inputDir += right;
		if (my >= 0 && my < edgeMargin)     inputDir += fwd;
		if (my > h - edgeMargin && my <= h) inputDir -= fwd;
	}

	// Momentum: snap up quickly, coast down slowly (SC2 feel).
	float baseSpeed = rtsPanSpeed * (fast ? 2.5f : 1.0f);
	glm::vec3 targetVel = (glm::length(inputDir) > 0.001f)
		? glm::normalize(inputDir) * baseSpeed : glm::vec3(0);
	float panLerp = (glm::length(targetVel) > 0.001f) ? 14.0f : 5.0f;
	rtsPanVel += (targetVel - rtsPanVel) * std::min(dt * panLerp, 1.0f);
	rtsCenter += rtsPanVel * dt;

	float rotSpeed = 90.0f * dt * (fast ? 2.0f : 1.0f);
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) rtsOrbitYaw -= rotSpeed;
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) rtsOrbitYaw += rotSpeed;
	if (glfwGetKey(window, GLFW_KEY_HOME) == GLFW_PRESS) rtsOrbitYaw = 90.0f;

	// Edge-detected so held keys don't re-trigger.
	for (int i = 0; i < 4; i++) {
		int key = GLFW_KEY_1 + i;
		bool pressed = glfwGetKey(window, key) == GLFW_PRESS;
		bool wasPressed = m_rtsBookmarkKeyPrev[i];
		m_rtsBookmarkKeyPrev[i] = pressed;
		if (!pressed || wasPressed) continue;
		if (fast) {
			rtsBookmarks[i] = {rtsCenter, rtsHeightTarget, rtsOrbitYaw, true};
		} else if (rtsBookmarks[i].set) {
			rtsCenter      = rtsBookmarks[i].center;
			rtsHeightTarget = rtsBookmarks[i].height;
			rtsOrbitYaw    = rtsBookmarks[i].orbitYaw;
			rtsPanVel      = glm::vec3(0);
		}
	}

	if (std::abs(pendingRtsZoom) > 0.001f) {
		float factor = std::pow(0.85f, pendingRtsZoom);
		rtsHeightTarget = std::clamp(rtsHeightTarget * factor, 5.0f, 120.0f);
		pendingRtsZoom = 0;
	}

	rtsHeight += (rtsHeightTarget - rtsHeight) * std::min(dt * 10.0f, 1.0f);

	float angle = glm::radians(rtsAngle);
	float tanA = std::max(0.1f, (float)std::tan(angle));
	float horizontalDist = rtsHeight / tanA;
	position = rtsCenter + glm::vec3(
		-cos(yaw) * horizontalDist,
		rtsHeight,
		-sin(yaw) * horizontalDist
	);

	glm::vec3 dir = glm::normalize(rtsCenter - position);
	lookYaw = glm::degrees(atan2(dir.z, dir.x));
	lookPitch = glm::degrees(asin(dir.y));
}

glm::vec3 Camera::godCameraForward() const {
	float y = glm::radians(godOrbitYaw);
	return glm::normalize(glm::vec3(cos(y), 0, sin(y)));
}

glm::vec3 Camera::godCameraRight() const {
	return glm::normalize(glm::cross(godCameraForward(), glm::vec3(0, 1, 0)));
}

} // namespace civcraft
