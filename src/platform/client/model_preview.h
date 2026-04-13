#pragma once

/**
 * ModelPreview — renders a BoxModel to an offscreen FBO for ImGui display.
 *
 * Used in the Handbook to show 3D previews of creatures, items, blocks.
 * Features:
 *   - Natural slow rotation when idle (showcase spin)
 *   - Click + drag to rotate manually
 *   - Renders to a texture, displayed via ImGui::Image()
 */

#include "client/box_model.h"
#include "client/model.h"
#include "client/shader.h"
#include "client/gl.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

namespace modcraft {

class ModelPreview {
public:
	bool init(Shader* shader, int width = 256, int height = 256) {
		m_width = width;
		m_height = height;
		m_shader = shader;

		// Create FBO
		glGenFramebuffers(1, &m_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

		// Color texture
		glGenTextures(1, &m_texture);
		glBindTexture(GL_TEXTURE_2D, m_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

		// Depth renderbuffer
		glGenRenderbuffers(1, &m_depthRbo);
		glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return true;
	}

	void shutdown() {
		if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
		if (m_texture) { glDeleteTextures(1, &m_texture); m_texture = 0; }
		if (m_depthRbo) { glDeleteRenderbuffers(1, &m_depthRbo); m_depthRbo = 0; }
	}

	// Render a model and display as ImGui image widget.
	// Returns true if the widget was hovered/interacted with.
	bool render(ModelRenderer& renderer, const BoxModel& model,
	            float dt, float displaySize = 200) {
		if (!m_fbo || !m_shader) return false;

		// Natural rotation + drag
		if (m_dragging) {
			ImVec2 delta = ImGui::GetMouseDragDelta(0);
			m_rotY += delta.x * 0.5f;
			m_rotX += delta.y * 0.3f;
			ImGui::ResetMouseDragDelta(0);
		} else {
			m_rotY += dt * 30.0f; // slow showcase spin
		}

		// Clamp vertical rotation
		m_rotX = glm::clamp(m_rotX, -45.0f, 45.0f);

		// Save current GL state
		GLint prevFbo, prevViewport[4];
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
		glGetIntegerv(GL_VIEWPORT, prevViewport);

		// Render to FBO
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glViewport(0, 0, m_width, m_height);
		glClearColor(0.94f, 0.93f, 0.90f, 1.0f); // warm bg matching theme
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);

		// Camera setup for model preview
		float aspect = (float)m_width / (float)m_height;
		float modelH = model.totalHeight * model.modelScale;
		float dist = modelH * 2.5f;

		glm::mat4 proj = glm::perspective(glm::radians(35.0f), aspect, 0.1f, 100.0f);
		glm::mat4 view = glm::lookAt(
			glm::vec3(0, modelH * 0.5f, dist),
			glm::vec3(0, modelH * 0.45f, 0),
			glm::vec3(0, 1, 0)
		);
		glm::mat4 vp = proj * view;

		// Draw model with gentle animation
		AnimState anim = {m_animTime, 1.5f, m_animTime};
		m_animTime += dt;

		renderer.draw(model, vp, glm::vec3(0, 0, 0), m_rotY, anim);

		// Restore GL state
		glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

		// Display in ImGui (flip UV vertically — OpenGL vs ImGui coords)
		ImGui::Image((ImTextureID)(intptr_t)m_texture,
			ImVec2(displaySize, displaySize),
			ImVec2(0, 1), ImVec2(1, 0)); // flipped UVs

		// Handle drag interaction
		bool hovered = ImGui::IsItemHovered();
		if (hovered && ImGui::IsMouseClicked(0)) {
			m_dragging = true;
		}
		if (!ImGui::IsMouseDown(0)) {
			m_dragging = false;
		}

		return hovered;
	}

private:
	GLuint m_fbo = 0;
	GLuint m_texture = 0;
	GLuint m_depthRbo = 0;
	Shader* m_shader = nullptr;
	int m_width = 256, m_height = 256;

	float m_rotY = 0;      // horizontal rotation (degrees)
	float m_rotX = 0;      // vertical tilt
	bool m_dragging = false;
	float m_animTime = 0;
};

} // namespace modcraft
