// CellCraft — World state. Plain data owned by Sim. No logic here
// beyond simple accessors/mutators; all ticking happens in Sim.

#pragma once

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/sim/monster.h"
#include "CellCraft/sim/tuning.h"

namespace civcraft::cellcraft::sim {

struct Food {
	uint32_t  id = 0;
	glm::vec2 pos = glm::vec2(0.0f);
	float     biomass = 1.0f;
};

struct World {
	std::unordered_map<uint32_t, Monster> monsters;
	std::vector<Food>                     food;
	float                                 map_radius = DEFAULT_MAP_RADIUS;
	uint32_t                              next_id    = 1;
	std::mt19937                          rng{0xC0FFEEu};

	Monster* get(uint32_t id) {
		auto it = monsters.find(id);
		return it == monsters.end() ? nullptr : &it->second;
	}
	const Monster* get(uint32_t id) const {
		auto it = monsters.find(id);
		return it == monsters.end() ? nullptr : &it->second;
	}

	uint32_t spawn_monster(Monster m) {
		m.id = next_id++;
		m.refresh_stats(); // sets hp_max taking parts into account
		if (m.hp <= 0.0f || m.hp > m.hp_max) m.hp = m.hp_max;
		uint32_t id = m.id;
		monsters.emplace(id, std::move(m));
		return id;
	}

	void remove_monster(uint32_t id) {
		monsters.erase(id);
	}

	uint32_t add_food(const glm::vec2& pos, float biomass) {
		Food f;
		f.id = next_id++;
		f.pos = pos;
		f.biomass = biomass;
		food.push_back(f);
		return f.id;
	}

	void scatter_food(int n) {
		std::uniform_real_distribution<float> rad_dist(0.0f, map_radius * 0.95f);
		std::uniform_real_distribution<float> ang_dist(0.0f, 6.28318530718f);
		std::uniform_real_distribution<float> bm_dist(FOOD_BIOMASS_MIN, FOOD_BIOMASS_MAX);
		for (int i = 0; i < n; ++i) {
			float r = std::sqrt(rad_dist(rng) / map_radius) * map_radius;
			float a = ang_dist(rng);
			glm::vec2 p(std::cos(a) * r, std::sin(a) * r);
			add_food(p, bm_dist(rng));
		}
	}
};

} // namespace civcraft::cellcraft::sim
