#include "client/camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace agentworld {

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
	m_firstMouse = true; // reset mouse tracking on mode change
	const char* names[] = {"First Person", "Third Person", "RPG", "RTS"};
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
		orbitYaw += dx; orbitPitch -= dy;
		orbitPitch = std::clamp(orbitPitch, 5.0f, 80.0f);
		player.yaw = orbitYaw; // player faces where camera orbits
		break;
	case CameraMode::RPG:
		// Mouse orbits camera around player (player yaw unchanged)
		// X: rotate orbit, Y: raise/lower camera angle
		godOrbitYaw += dx;
		godAngle += dy;
		godAngle = std::clamp(godAngle, 15.0f, 80.0f);
		break;
	case CameraMode::RTS:
		break; // no mouse look in RTS, mouse used for selection
	}
}

void Camera::processInput(GLFWwindow* window, float dt) {
	if (mode != CameraMode::RTS)
		processMouse(window);

	switch (mode) {
	case CameraMode::FirstPerson: updateFirstPerson(window, dt); break;
	case CameraMode::ThirdPerson: updateThirdPerson(window, dt); break;
	case CameraMode::RPG:     updateRPG(window, dt); break;
	case CameraMode::RTS:         updateRTS(window, dt); break;
	}
}

// Smooth vertical camera tracking.
// Going UP (step-up): gentle rise like climbing stairs.
// Going DOWN (falling/stepping down): faster tracking for responsiveness.
float Camera::smoothVertical(float targetY, float dt) {
	if (!m_smoothInit) {
		m_smoothY = targetY;
		m_smoothInit = true;
		return targetY;
	}

	float diff = targetY - m_smoothY;

	// Asymmetric smoothing: gentle climb up, responsive fall down
	float rate;
	if (diff > 0.01f) {
		// Rising (step-up): smooth climb over ~0.2s
		rate = 8.0f;
	} else if (diff < -0.01f) {
		// Falling/stepping down: fast tracking
		rate = 18.0f;
	} else {
		// Close enough: snap to avoid floating-point drift
		m_smoothY = targetY;
		return targetY;
	}

	m_smoothY += diff * std::min(rate * dt, 1.0f);
	return m_smoothY;
}

void Camera::updateFirstPerson(GLFWwindow* window, float dt) {
	// Always smooth feetPos.y, then add eye height offset
	float smoothedFeetY = smoothVertical(player.feetPos.y, dt);
	position = glm::vec3(player.feetPos.x, smoothedFeetY + player.eyeHeight, player.feetPos.z);
}

void Camera::updateThirdPerson(GLFWwindow* window, float dt) {
	// Smooth zoom
	orbitDistance += (orbitDistanceTarget - orbitDistance) * std::min(dt * 10.0f, 1.0f);

	float yaw = glm::radians(orbitYaw);
	float pitch = glm::radians(orbitPitch);

	glm::vec3 offset(
		-cos(yaw) * cos(pitch) * orbitDistance,
		sin(pitch) * orbitDistance,
		-sin(yaw) * cos(pitch) * orbitDistance
	);

	float smoothedFeetY = smoothVertical(player.feetPos.y, dt);
	glm::vec3 target(player.feetPos.x, smoothedFeetY + player.eyeHeight * 0.8f, player.feetPos.z);
	position = target + offset;

	glm::vec3 dir = glm::normalize(target - position);
	lookYaw = glm::degrees(atan2(dir.z, dir.x));
	lookPitch = glm::degrees(asin(dir.y));
}

void Camera::updateRPG(GLFWwindow* window, float dt) {
	// Smooth zoom
	godDistance += (godDistanceTarget - godDistance) * std::min(dt * 10.0f, 1.0f);

	float angle = glm::radians(godAngle);
	float yaw = glm::radians(godOrbitYaw);

	glm::vec3 offset(
		-cos(yaw) * cos(angle) * godDistance,
		sin(angle) * godDistance,
		-sin(yaw) * cos(angle) * godDistance
	);

	float smoothedFeetY = smoothVertical(player.feetPos.y, dt);
	glm::vec3 target(player.feetPos.x, smoothedFeetY + 1.0f, player.feetPos.z);
	position = target + offset;

	glm::vec3 dir = glm::normalize(target - position);
	lookYaw = glm::degrees(atan2(dir.z, dir.x));
	lookPitch = glm::degrees(asin(dir.y));
}

void Camera::updateRTS(GLFWwindow* window, float dt) {
	// Top-down camera, WASD pans, mouse for selection

	float speed = rtsPanSpeed * dt;
	if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) speed *= 2.5f;

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) rtsCenter.z -= speed;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) rtsCenter.z += speed;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) rtsCenter.x -= speed;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) rtsCenter.x += speed;

	// Edge scrolling (move camera when mouse near screen edge)
	double mx, my;
	glfwGetCursorPos(window, &mx, &my);
	int w, h;
	glfwGetWindowSize(window, &w, &h);
	float edgeMargin = 30.0f;
	if (mx < edgeMargin) rtsCenter.x -= speed;
	if (mx > w - edgeMargin) rtsCenter.x += speed;
	if (my < edgeMargin) rtsCenter.z -= speed;
	if (my > h - edgeMargin) rtsCenter.z += speed;

	// Smooth zoom
	rtsHeight += (rtsHeightTarget - rtsHeight) * std::min(dt * 10.0f, 1.0f);

	float angle = glm::radians(rtsAngle);
	position = rtsCenter + glm::vec3(0, sin(angle) * rtsHeight, cos(angle) * rtsHeight * 0.3f);

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

} // namespace agentworld
