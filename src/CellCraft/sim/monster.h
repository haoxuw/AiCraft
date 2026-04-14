// CellCraft — Monster. The "creature" entity: a closed polygon body
// with a core at the local-space origin, plus derived stats cached
// from the shape. See docs/00_OVERVIEW.md § Shape → physics mapping.
//
// Shape invariants:
// - stored in monster-local coordinates with core at origin (0,0)
// - closed loop: last vertex != first vertex; closure is implicit
// - counter-clockwise or clockwise both accepted (we use |shoelace|)

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/sim/part.h"
#include "CellCraft/sim/part_stats.h"
#include "CellCraft/sim/polygon_util.h"
#include "CellCraft/sim/tuning.h"

namespace civcraft::cellcraft::sim {

struct Monster {
	uint32_t id = 0;
	uint32_t owner_id = 0; // 0 = neutral / AI

	glm::vec2 core_pos = glm::vec2(0.0f);
	glm::vec2 velocity = glm::vec2(0.0f);
	float     heading  = 0.0f;

	std::vector<glm::vec2> shape; // local space, core at origin
	glm::vec3 color = glm::vec3(1.0f);

	float hp       = 1.0f;
	float hp_max   = 1.0f;
	float biomass  = 0.0f;

	// Derived stats (recomputed via refresh_stats()).
	float max_core_radius = 0.0f;
	float max_width       = 0.0f;   // AABB half-extent proxy (max of x/y)
	float area            = 0.0f;
	float turn_speed      = 0.0f;
	float move_speed      = 0.0f;
	float mass            = 0.0f;

	bool alive = true;

	// Modular parts + cached aggregate effect.
	std::vector<Part> parts;
	PartEffect        part_effect;

	// Active status effects (venom DoT, etc). Simple vector — short-lived,
	// typically 0–4 entries per monster.
	std::vector<StatusEffect> status;

	void refresh_stats() {
		max_core_radius = polygon_max_radius_from_origin(shape);
		glm::vec2 half = polygon_local_halfextents(shape);
		max_width = std::max(half.x, half.y);
		area = polygon_area(shape);
		mass = area * DENSITY;

		float r = std::max(max_core_radius, 1e-3f);
		float w = std::max(max_width, 1e-3f);

		float base_turn = std::clamp(TURN_K / r, TURN_MIN, TURN_MAX);
		float base_move = std::clamp(MOVE_K / w, MOVE_MIN, MOVE_MAX);

		part_effect = computePartEffects(parts);
		turn_speed  = base_turn * part_effect.turn_mult;
		move_speed  = base_move * part_effect.speed_mult;
		hp_max      = std::max(1.0f, biomass * HP_PER_BIOMASS * part_effect.hp_mult);
		if (hp > hp_max) hp = hp_max;
	}

	// Scale shape in place; biomass/hp are caller's business.
	void scale_shape(float factor) {
		for (auto& v : shape) v *= factor;
		refresh_stats();
	}
};

} // namespace civcraft::cellcraft::sim
