#pragma once

#include "client/gfx.h"
#include <glm/glm.hpp>
#include <vector>
#include "client/shader.h"

namespace civcraft {

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
	void emitDeathPuff(glm::vec3 pos, glm::vec3 bodyColor, float entityHeight);
	// Minecraft-Dungeons-style swing shockwave: an expanding ring of
	// particles in the plane perpendicular to `normal`, centred at `center`.
	// facingYawRad orients the ring's initial spread so the arc reads as
	// "sweeping in front of" the attacker. Radius grows for `kShockLife`
	// seconds.
	void emitSwingShockwave(glm::vec3 center, glm::vec3 normal,
	                        float facingYawRad, glm::vec3 color, int count = 24);
	void addParticle(const Particle& p) { m_particles.push_back(p); }

	size_t count() const { return m_particles.size(); }

private:
	std::vector<Particle> m_particles;
	Shader m_shader;
	GLuint m_vao = 0, m_vbo = 0;
};

} // namespace civcraft
