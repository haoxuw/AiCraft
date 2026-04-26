#include "client/game_vk.h"

#include <cmath>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Camera projection helpers that stay on Game because game_vk_playing.cpp
// also needs them for raycasts and mouse unprojection. All pixel-pushing code
// moved to WorldRenderer / HudRenderer / MenuRenderer / PanelRenderer /
// EntityUiRenderer — see game_vk_renderers.h.

namespace solarium::vk {

glm::mat4 Game::viewProj() const {
	glm::mat4 proj = m_cam.projectionMatrix(m_aspect);
	proj[1][1] *= -1.0f;  // Vulkan Y-flip
	glm::mat4 view = m_cam.viewMatrix();
	// Camera shake as a post-view translation — shadow pass stays stable.
	if (m_cameraShake > 0.0f && m_shakeIntensity > 0.0f) {
		float t = m_wallTime * 60.0f;
		float sx = std::sin(t * 13.1f) * 0.5f + std::sin(t * 27.3f) * 0.5f;
		float sy = std::cos(t * 17.7f) * 0.5f + std::cos(t * 31.9f) * 0.5f;
		float k = m_cameraShake * m_shakeIntensity;
		glm::mat4 jitter = glm::translate(glm::mat4(1.0f),
			glm::vec3(sx * k, sy * k, 0.0f));
		view = jitter * view;
	}
	return proj * view;
}

glm::mat4 Game::pickViewProj() const {
	return m_cam.projectionMatrix(m_aspect) * m_cam.viewMatrix();
}

bool Game::projectWorld(const glm::vec3& world, glm::vec3& out) const {
	glm::vec4 clip = viewProj() * glm::vec4(world, 1.0f);
	if (clip.w <= 0.01f) return false;
	float ndcX = clip.x / clip.w;
	float ndcY = -clip.y / clip.w;  // Y already flipped in proj — un-flip
	// UI expects +y up (rhi_ui.cpp). Internal VP has proj[1][1] *= -1 for
	// Vulkan image coords, so re-flip here for UI consumption.
	if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) return false;
	out = glm::vec3(ndcX, ndcY, clip.z / clip.w);
	return true;
}

} // namespace solarium::vk
