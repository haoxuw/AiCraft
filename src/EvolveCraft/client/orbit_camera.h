#pragma once

// Orbit camera for EvolveCraft: overhead RTS-style view of the pond.
// WASD pan, Q/E zoom, mouse-drag rotate. Independent of modcraft::Camera
// to avoid pulling in player-state coupling.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <cmath>

namespace evolvecraft {

struct OrbitCamera {
	glm::vec3 center = {0, 0, 0};   // point camera orbits / looks at
	float distance     = 55.0f;     // radius from center
	float minDistance  = 8.0f;
	float maxDistance  = 140.0f;
	float pitchDeg     = 50.0f;     // 0=side, 90=straight down
	float yawDeg       = 0.0f;      // rotate around Y
	float fov          = 55.0f;
	float near_        = 0.1f;
	float far_         = 500.0f;
	float panSpeed     = 30.0f;
	float zoomSpeed    = 1.12f;

	glm::vec3 position() const {
		float p = glm::radians(pitchDeg);
		float y = glm::radians(yawDeg);
		glm::vec3 dir = { std::cos(p) * std::sin(y),
		                  std::sin(p),
		                  std::cos(p) * std::cos(y) };
		return center + dir * distance;
	}

	glm::mat4 view() const {
		return glm::lookAt(position(), center, glm::vec3(0, 1, 0));
	}

	glm::mat4 proj(float aspect) const {
		return glm::perspective(glm::radians(fov), aspect, near_, far_);
	}

	void processInput(GLFWwindow* w, float dt) {
		glm::vec3 right = glm::normalize(glm::vec3(std::cos(glm::radians(yawDeg)), 0,
		                                           -std::sin(glm::radians(yawDeg))));
		glm::vec3 forward = glm::normalize(glm::vec3(std::sin(glm::radians(yawDeg)), 0,
		                                             std::cos(glm::radians(yawDeg))));
		float spd = panSpeed * dt;
		if (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) spd *= 2.5f;
		if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) center -= forward * spd;
		if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) center += forward * spd;
		if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) center -= right * spd;
		if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) center += right * spd;
		if (glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS) yawDeg -= 60.0f * dt;
		if (glfwGetKey(w, GLFW_KEY_E) == GLFW_PRESS) yawDeg += 60.0f * dt;
		if (glfwGetKey(w, GLFW_KEY_R) == GLFW_PRESS) pitchDeg = std::min(85.0f, pitchDeg + 30.0f * dt);
		if (glfwGetKey(w, GLFW_KEY_F) == GLFW_PRESS) pitchDeg = std::max(10.0f, pitchDeg - 30.0f * dt);
		// Stay inside clamp
		distance = glm::clamp(distance, minDistance, maxDistance);
		center.y = 0; // keep on water plane
	}

	void applyScroll(double yOff) {
		if (yOff > 0) distance /= zoomSpeed;
		else if (yOff < 0) distance *= zoomSpeed;
		distance = glm::clamp(distance, minDistance, maxDistance);
	}
};

} // namespace evolvecraft
