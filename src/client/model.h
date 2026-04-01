#pragma once

#include "shared/box_model.h"
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "client/shader.h"

namespace aicraft {

// Renders box-based models with animation.
class ModelRenderer {
public:
	bool init(Shader* highlightShader);
	void shutdown();

	void draw(const BoxModel& model, const glm::mat4& viewProj,
	          glm::vec3 feetPos, float yaw, const AnimState& anim = {},
	          glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.85f, 0.3f)));

private:
	Shader* m_shader = nullptr;
	GLuint m_cubeVAO = 0, m_cubeVBO = 0, m_cubeEBO = 0;
};

} // namespace aicraft
