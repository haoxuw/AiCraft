#include "client/particles.h"
#include <cmath>
#include <algorithm>

namespace agentworld {

// Simple deterministic hash for particle randomness
static float prand(int seed) {
	unsigned int n = (unsigned int)seed;
	n = (n << 13) ^ n;
	return (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 2147483647.0f;
}

bool ParticleSystem::init(const std::string& dir) {
	if (!m_shader.loadFromFile(dir + "/particle.vert", dir + "/particle.frag"))
		return false;

	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

	// Layout: vec3 pos, vec4 color, float size = 8 floats per particle
	size_t stride = 8 * sizeof(float);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void*)(7 * sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);
	return true;
}

void ParticleSystem::shutdown() {
	if (m_vbo) glDeleteBuffers(1, &m_vbo);
	if (m_vao) glDeleteVertexArrays(1, &m_vao);
	m_vbo = m_vao = 0;
}

void ParticleSystem::update(float dt) {
	for (auto& p : m_particles) {
		p.vel.y -= 12.0f * dt; // gravity
		p.pos += p.vel * dt;
		p.life -= dt;
		// Fade out
		p.color.a = std::max(0.0f, p.life / p.maxLife);
		// Shrink
		p.size = p.size * (0.98f + 0.02f * (p.life / p.maxLife));
	}

	// Remove dead particles
	m_particles.erase(
		std::remove_if(m_particles.begin(), m_particles.end(),
			[](const Particle& p) { return p.life <= 0; }),
		m_particles.end());
}

void ParticleSystem::render(const glm::mat4& viewProj) {
	if (m_particles.empty()) return;

	// Build vertex data
	std::vector<float> data;
	data.reserve(m_particles.size() * 8);
	for (auto& p : m_particles) {
		data.push_back(p.pos.x);
		data.push_back(p.pos.y);
		data.push_back(p.pos.z);
		data.push_back(p.color.r);
		data.push_back(p.color.g);
		data.push_back(p.color.b);
		data.push_back(p.color.a);
		data.push_back(p.size);
	}

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_DYNAMIC_DRAW);

#ifndef __EMSCRIPTEN__
	glEnable(GL_PROGRAM_POINT_SIZE); // WebGL: always enabled via shader
#endif
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);

	m_shader.use();
	m_shader.setMat4("uVP", viewProj);
	glDrawArrays(GL_POINTS, 0, (GLsizei)m_particles.size());

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
#ifndef __EMSCRIPTEN__
	glDisable(GL_PROGRAM_POINT_SIZE);
#endif
	glBindVertexArray(0);
}

void ParticleSystem::emitBlockBreak(glm::vec3 pos, glm::vec3 color, int count) {
	glm::vec3 center = pos + glm::vec3(0.5f);
	int seed = (int)(pos.x * 73 + pos.y * 37 + pos.z * 91);
	for (int i = 0; i < count; i++) {
		Particle p;
		p.pos = center + glm::vec3(
			(prand(seed + i * 3) - 0.5f) * 0.6f,
			(prand(seed + i * 3 + 1) - 0.5f) * 0.6f,
			(prand(seed + i * 3 + 2) - 0.5f) * 0.6f);
		p.vel = glm::vec3(
			(prand(seed + i * 7) - 0.5f) * 4.0f,
			prand(seed + i * 7 + 1) * 3.0f + 1.0f,
			(prand(seed + i * 7 + 2) - 0.5f) * 4.0f);
		// Slight color variation
		float cv = (prand(seed + i * 11) - 0.5f) * 0.15f;
		p.color = glm::vec4(
			std::clamp(color.r + cv, 0.0f, 1.0f),
			std::clamp(color.g + cv, 0.0f, 1.0f),
			std::clamp(color.b + cv, 0.0f, 1.0f),
			1.0f);
		p.life = 0.5f + prand(seed + i * 13) * 0.8f;
		p.maxLife = p.life;
		p.size = 0.04f + prand(seed + i * 17) * 0.04f;
		m_particles.push_back(p);
	}
}

void ParticleSystem::emitItemPickup(glm::vec3 pos, glm::vec3 color) {
	int seed = (int)(pos.x * 113 + pos.y * 53 + pos.z * 79);
	for (int i = 0; i < 6; i++) {
		Particle p;
		p.pos = pos;
		p.vel = glm::vec3(
			(prand(seed + i * 3) - 0.5f) * 2.0f,
			prand(seed + i * 3 + 1) * 3.0f,
			(prand(seed + i * 3 + 2) - 0.5f) * 2.0f);
		p.color = glm::vec4(color * 1.2f, 1.0f);
		p.life = 0.3f + prand(seed + i * 5) * 0.3f;
		p.maxLife = p.life;
		p.size = 0.03f;
		m_particles.push_back(p);
	}
}

} // namespace agentworld
