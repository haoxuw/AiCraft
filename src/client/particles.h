#pragma once

#include "client/gl.h"
#include <glm/glm.hpp>
#include <vector>
#include "client/shader.h"

namespace agentworld {

struct Particle {
	glm::vec3 pos;
	glm::vec3 vel;
	glm::vec4 color;
	float life;
	float maxLife;
	float size;
};

class ParticleSystem {
public:
	bool init(const std::string& shaderDir);
	void shutdown();

	void update(float dt);
	void render(const glm::mat4& viewProj);

	void emitBlockBreak(glm::vec3 pos, glm::vec3 color, int count = 14);
	void emitItemPickup(glm::vec3 pos, glm::vec3 color);
	void addParticle(const Particle& p) { m_particles.push_back(p); }

	size_t count() const { return m_particles.size(); }

private:
	std::vector<Particle> m_particles;
	Shader m_shader;
	GLuint m_vao = 0, m_vbo = 0;
};

} // namespace agentworld
