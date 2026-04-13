#pragma once

// EvolveCraft — SwimField: a bounded circular pond.
//
// The "world" is a 2.5D fluid plane — entities move on XZ, Y pinned near 0
// with a small vertical bob for visual depth. No blocks, no chunks.
//
// Bounded by a circle of radius `radius` centered at the origin. Cells that
// leave the circle get pushed back softly (velocity reflected toward center).

#include <glm/glm.hpp>

namespace evolvecraft {

struct SwimField {
	float radius = 60.0f;         // pond radius in world units (meters)
	float waterLevel = 0.0f;      // Y plane the surface sits on
	glm::vec3 pondColor = {0.05f, 0.18f, 0.28f};

	bool inside(glm::vec3 p) const {
		return glm::dot(glm::vec2(p.x, p.z), glm::vec2(p.x, p.z))
		       <= radius * radius;
	}

	// Soft push-back: returns a velocity delta that nudges entities back in-bounds.
	glm::vec3 boundaryForce(glm::vec3 pos) const {
		glm::vec2 xz(pos.x, pos.z);
		float d = glm::length(xz);
		if (d <= radius - 1.5f) return {0, 0, 0};
		glm::vec2 n = (d > 1e-4f) ? (xz / d) : glm::vec2(1, 0);
		float over = d - (radius - 1.5f);
		return glm::vec3(-n.x, 0, -n.y) * (over * 2.0f);
	}
};

} // namespace evolvecraft
