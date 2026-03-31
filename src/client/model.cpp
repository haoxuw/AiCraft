#include "client/model.h"
#include <cmath>

namespace aicraft {

bool ModelRenderer::init(Shader* shader) {
	m_shader = shader;

	// 24 vertices (4 per face) with per-face normals for lighting.
	// Each vertex: pos.x, pos.y, pos.z, norm.x, norm.y, norm.z
	float verts[] = {
		// Front face (z=0), normal (0,0,-1)
		0,0,0, 0,0,-1,  1,0,0, 0,0,-1,  1,1,0, 0,0,-1,  0,1,0, 0,0,-1,
		// Back face (z=1), normal (0,0,1)
		1,0,1, 0,0,1,   0,0,1, 0,0,1,   0,1,1, 0,0,1,   1,1,1, 0,0,1,
		// Left face (x=0), normal (-1,0,0)
		0,0,1, -1,0,0,  0,0,0, -1,0,0,  0,1,0, -1,0,0,  0,1,1, -1,0,0,
		// Right face (x=1), normal (1,0,0)
		1,0,0, 1,0,0,   1,0,1, 1,0,0,   1,1,1, 1,0,0,   1,1,0, 1,0,0,
		// Bottom face (y=0), normal (0,-1,0)
		0,0,1, 0,-1,0,  1,0,1, 0,-1,0,  1,0,0, 0,-1,0,  0,0,0, 0,-1,0,
		// Top face (y=1), normal (0,1,0)
		0,1,0, 0,1,0,   1,1,0, 0,1,0,   1,1,1, 0,1,0,   0,1,1, 0,1,0,
	};
	unsigned int indices[] = {
		0,1,2,   0,2,3,     // Front
		4,5,6,   4,6,7,     // Back
		8,9,10,  8,10,11,   // Left
		12,13,14, 12,14,15, // Right
		16,17,18, 16,18,19, // Bottom
		20,21,22, 20,22,23, // Top
	};

	glGenVertexArrays(1, &m_cubeVAO);
	glGenBuffers(1, &m_cubeVBO);
	glGenBuffers(1, &m_cubeEBO);

	glBindVertexArray(m_cubeVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
	// Position attribute
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), nullptr);
	glEnableVertexAttribArray(0);
	// Normal attribute
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
	glEnableVertexAttribArray(1);
	glBindVertexArray(0);
	return true;
}

void ModelRenderer::shutdown() {
	if (m_cubeEBO) glDeleteBuffers(1, &m_cubeEBO);
	if (m_cubeVBO) glDeleteBuffers(1, &m_cubeVBO);
	if (m_cubeVAO) glDeleteVertexArrays(1, &m_cubeVAO);
}

void ModelRenderer::draw(const BoxModel& model, const glm::mat4& viewProj,
                          glm::vec3 feetPos, float yaw, const AnimState& anim,
                          glm::vec3 sunDir) {
	m_shader->use();
	glEnable(GL_DEPTH_TEST);

	float s = model.modelScale; // uniform scale for all geometry

	// Walk cycle phase: driven by distance traveled
	float walkPhase = anim.walkDistance * model.walkCycleSpeed;

	// Speed factor clamped to [0,1] for smoothstep
	float speedFactor = std::min(anim.speed / 6.0f, 1.0f);
	float smoothSpeed = speedFactor * speedFactor * (3.0f - 2.0f * speedFactor);

	// Vertical bounce (scaled with model size)
	float walkBob = 0.0f;
	if (smoothSpeed > 0.05f) {
		float rawBob = std::abs(std::sin(walkPhase));
		walkBob = rawBob * rawBob * model.walkBobAmount * smoothSpeed * s;
	}

	// Idle bob / breathing (scaled)
	float idleBob = 0.0f;
	if (smoothSpeed < 0.1f) {
		float idleBlend = 1.0f - smoothSpeed / 0.1f;
		idleBob = std::sin(anim.time * model.idleBobSpeed) * model.idleBobAmount * idleBlend * s;
	}

	// Root transform: world position + facing + bounce
	glm::mat4 root = glm::translate(glm::mat4(1.0f),
		feetPos + glm::vec3(0, idleBob + walkBob, 0));
	root = glm::rotate(root, glm::radians(-yaw - 90.0f), glm::vec3(0, 1, 0));

	// Forward lean proportional to speed
	if (smoothSpeed > 0.05f) {
		float lean = smoothSpeed * 3.5f;
		root = glm::rotate(root, glm::radians(lean), glm::vec3(1, 0, 0));
	}

	// Lateral body sway: weight shifts toward planted foot (scaled)
	if (smoothSpeed > 0.05f && model.lateralSwayAmount > 0.001f) {
		float sway = std::sin(walkPhase) * model.lateralSwayAmount * smoothSpeed * s;
		root = glm::translate(root, glm::vec3(sway, 0, 0));
		float hipRoll = -std::sin(walkPhase) * glm::radians(1.5f) * smoothSpeed;
		root = glm::rotate(root, hipRoll, glm::vec3(0, 0, 1));
	}

	m_shader->setVec3("uSunDir", sunDir);

	GLint colorLoc = glGetUniformLocation(m_shader->id(), "uColor");
	glBindVertexArray(m_cubeVAO);

	for (auto& part : model.parts) {
		glm::mat4 partMat = root;

		// Apply limb swing animation (pivot scaled to model size)
		if (part.swingAmplitude > 0.001f && smoothSpeed > 0.02f) {
			float angle = std::sin(walkPhase * part.swingSpeed + part.swingPhase)
			              * glm::radians(part.swingAmplitude)
			              * smoothSpeed;

			partMat = glm::translate(partMat, part.pivot * s);
			partMat = glm::rotate(partMat, angle, part.swingAxis);
			partMat = glm::translate(partMat, -part.pivot * s);
		}

		// Scale part geometry to model size
		glm::mat4 worldMat = glm::translate(partMat, (part.offset - part.halfSize) * s);
		partMat = glm::scale(worldMat, part.halfSize * 2.0f * s);

		m_shader->setMat4("uModel", worldMat);
		m_shader->setMat4("uMVP", viewProj * partMat);
		glUniform4f(colorLoc, part.color.r, part.color.g, part.color.b, part.color.a);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
	}
}

} // namespace aicraft
