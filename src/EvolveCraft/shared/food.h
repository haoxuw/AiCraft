#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace evolvecraft {

using FoodId = uint32_t;

struct Food {
	FoodId id = 0;
	glm::vec3 pos = {0, 0, 0};
	float nutrition = 3.0f;   // energy delivered when eaten
	float radius    = 0.35f;  // visual + overlap radius
	float bobPhase  = 0.0f;
	bool  alive = true;
};

} // namespace evolvecraft
