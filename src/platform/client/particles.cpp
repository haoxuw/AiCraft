#include "client/particles.h"
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>

namespace civcraft {

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

void ParticleSystem::emitDeathPuff(glm::vec3 pos, glm::vec3 bodyColor, float entityHeight) {
	// Center of entity mass
	glm::vec3 center = pos + glm::vec3(0, entityHeight * 0.5f, 0);
	int seed = (int)(pos.x * 113 + pos.y * 53 + pos.z * 79 + 999);

	// Spread size proportional to entity height
	float spread = entityHeight * 0.6f;

	for (int i = 0; i < 28; i++) {
		Particle p;
		// Spawn within entity volume
		p.pos = center + glm::vec3(
			(prand(seed + i * 3)     - 0.5f) * spread,
			(prand(seed + i * 3 + 1) - 0.5f) * spread,
			(prand(seed + i * 3 + 2) - 0.5f) * spread);

		// Burst outward and upward
		float angle = prand(seed + i * 7) * 6.28318f;
		float speed = 2.5f + prand(seed + i * 7 + 1) * 4.0f;
		p.vel = glm::vec3(
			std::cos(angle) * speed,
			prand(seed + i * 7 + 2) * 5.0f + 1.0f,
			std::sin(angle) * speed);

		// Mix body color with red blood
		float redBias = prand(seed + i * 11);
		glm::vec3 col = mix(bodyColor, glm::vec3(0.75f, 0.08f, 0.05f), redBias * 0.6f);
		float cv = (prand(seed + i * 13) - 0.5f) * 0.12f;
		p.color = glm::vec4(
			std::clamp(col.r + cv, 0.0f, 1.0f),
			std::clamp(col.g + cv, 0.0f, 1.0f),
			std::clamp(col.b + cv, 0.0f, 1.0f),
			1.0f);

		p.life    = 0.4f + prand(seed + i * 17) * 0.7f;
		p.maxLife = p.life;
		p.size    = 0.05f + prand(seed + i * 19) * 0.08f;
		m_particles.push_back(p);
	}
}

void ParticleSystem::emitSwingShockwave(glm::vec3 center, glm::vec3 normal,
                                        float facingYawRad, glm::vec3 color, int count) {
	// Build an orthonormal frame with `normal` as the ring's axis. The ring
	// lives in the plane spanned by `tangent` and `bitangent`; we only emit
	// into the front 180° (centred on the attacker's facing) so the shockwave
	// reads as sweeping OUT from the swing, not a full disc behind the body.
	glm::vec3 n = glm::length(normal) > 1e-4f ? glm::normalize(normal)
	                                          : glm::vec3(0, 1, 0);
	// Pick a helper not parallel to n
	glm::vec3 helper = (std::abs(n.y) < 0.9f) ? glm::vec3(0, 1, 0)
	                                          : glm::vec3(1, 0, 0);
	glm::vec3 tangent   = glm::normalize(glm::cross(helper, n));
	glm::vec3 bitangent = glm::cross(n, tangent);

	int seed = (int)(center.x * 911 + center.y * 419 + center.z * 277
	                + facingYawRad * 1000.0f);

	for (int i = 0; i < count; i++) {
		Particle p;
		// Angle spread: front 180° cone centred on facing yaw (around +Z/-Z
		// plane projected onto the tangent frame). We use i to bias across
		// the cone and add small jitter.
		float t = (float)i / (float)std::max(1, count - 1);
		float angle = facingYawRad + (t - 0.5f) * 3.14159f
		            + (prand(seed + i * 5) - 0.5f) * 0.25f;

		// Direction in world space within the ring plane
		glm::vec3 dir = std::cos(angle) * tangent + std::sin(angle) * bitangent;

		float speed  = 4.0f + prand(seed + i * 7) * 1.5f;
		// Gravity is 12 m/s² in update(); counter it with an upward bias so
		// the ring stays roughly horizontal for its short life.
		p.vel        = dir * speed + glm::vec3(0, 3.0f, 0);
		// Start slightly off-centre so the ring has thickness
		p.pos        = center + dir * 0.15f
		             + glm::vec3(0, (prand(seed + i * 11) - 0.5f) * 0.05f, 0);

		float cv = (prand(seed + i * 13) - 0.5f) * 0.10f;
		p.color = glm::vec4(
			std::clamp(color.r + cv, 0.0f, 1.0f),
			std::clamp(color.g + cv, 0.0f, 1.0f),
			std::clamp(color.b + cv, 0.0f, 1.0f),
			0.9f);
		p.life    = 0.22f + prand(seed + i * 17) * 0.08f;
		p.maxLife = p.life;
		p.size    = 0.08f + prand(seed + i * 19) * 0.04f;
		m_particles.push_back(p);
	}
}

} // namespace civcraft
