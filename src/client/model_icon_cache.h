#pragma once

/**
 * ModelIconCache — renders BoxModels to small cached textures for UI display.
 *
 * Used by inventory and hotbar to show proper 3D item icons instead of
 * flat-color cubes. Each unique model is rendered once to a 64x64 FBO,
 * then the texture is reused every frame.
 *
 * Call getIcon(modelKey) to get a GL texture ID. If the model hasn't been
 * rendered yet, it renders on demand and caches the result.
 */

#include "shared/box_model.h"
#include "client/model.h"
#include "client/shader.h"
#include "client/gl.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <string>

namespace agentica {

class ModelIconCache {
public:
	void init(Shader* shader, ModelRenderer* renderer) {
		m_shader = shader;
		m_renderer = renderer;

		// Create a shared FBO for rendering icons
		glGenFramebuffers(1, &m_fbo);
		glGenRenderbuffers(1, &m_depthRbo);
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, ICON_SIZE, ICON_SIZE);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void shutdown() {
		for (auto& [_, tex] : m_cache) glDeleteTextures(1, &tex);
		m_cache.clear();
		if (m_depthRbo) { glDeleteRenderbuffers(1, &m_depthRbo); m_depthRbo = 0; }
		if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
	}

	// Get a slowly rotating icon. Re-renders periodically for animation.
	GLuint getIcon(const std::string& key, const BoxModel& model) {
		float now = m_globalTime;
		auto it = m_cache.find(key);
		auto timeIt = m_renderTime.find(key);

		// Re-render every 0.05s for smooth rotation (20 FPS icons)
		bool needsRender = (it == m_cache.end());
		if (!needsRender && timeIt != m_renderTime.end() && (now - timeIt->second) > 0.05f)
			needsRender = true;

		if (needsRender) {
			if (it != m_cache.end()) glDeleteTextures(1, &it->second);
			float yaw = now * 30.0f; // 30 degrees per second
			GLuint tex = renderToTexture(model, yaw);
			m_cache[key] = tex;
			m_renderTime[key] = now;
			return tex;
		}
		return it->second;
	}

	void setTime(float t) { m_globalTime = t; }

	// Invalidate a specific icon (e.g., after model reload)
	void invalidate(const std::string& key) {
		auto it = m_cache.find(key);
		if (it != m_cache.end()) {
			glDeleteTextures(1, &it->second);
			m_cache.erase(it);
		}
	}

	// Invalidate all icons
	void invalidateAll() {
		for (auto& [_, tex] : m_cache) glDeleteTextures(1, &tex);
		m_cache.clear();
	}

	static constexpr int ICON_SIZE = 96;

private:
	GLuint renderToTexture(const BoxModel& model, float yaw = -30.0f) {
		if (!m_fbo || !m_shader || !m_renderer) return 0;

		// Create texture for this icon
		GLuint tex;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ICON_SIZE, ICON_SIZE, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		// Save GL state
		GLint prevFbo, prevViewport[4];
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
		glGetIntegerv(GL_VIEWPORT, prevViewport);

		// Bind FBO with this texture as color attachment
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
		glViewport(0, 0, ICON_SIZE, ICON_SIZE);

		// Dark warm background matching inventory cell bg
		glClearColor(0.08f, 0.07f, 0.05f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);

		// Camera: look at the model from an isometric-ish angle
		float modelH = model.totalHeight * model.modelScale;
		if (modelH < 0.1f) modelH = 0.5f;
		float dist = modelH * 2.8f;
		float aspect = 1.0f;

		glm::mat4 proj = glm::perspective(glm::radians(30.0f), aspect, 0.1f, 100.0f);
		glm::mat4 view = glm::lookAt(
			glm::vec3(dist * 0.6f, modelH * 0.7f, dist * 0.6f),
			glm::vec3(0, modelH * 0.35f, 0),
			glm::vec3(0, 1, 0)
		);
		glm::mat4 vp = proj * view;

		// Draw model with slight rotation for 3/4 view
		AnimState anim = {0, 0, 0};
		m_renderer->draw(model, vp, glm::vec3(0, 0, 0), yaw, anim);

		// Restore GL state
		glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

		return tex;
	}

	Shader* m_shader = nullptr;
	ModelRenderer* m_renderer = nullptr;
	GLuint m_fbo = 0;
	GLuint m_depthRbo = 0;
	float m_globalTime = 0;
	std::unordered_map<std::string, GLuint> m_cache;
	std::unordered_map<std::string, float> m_renderTime; // last render time per key
};

} // namespace agentica
