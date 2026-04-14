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

// Food comes in two flavors. Diet behavior (herbivore/carnivore/omnivore)
// will be wired up in commit 3 — for now pickup is flavor-agnostic.
enum class FoodType : uint8_t {
	MEAT  = 0,  // pink chunk, bigger biomass (risk/reward later)
	PLANT = 1,  // green leaf cluster, smaller biomass, more common
};

// Diet × FoodType biomass yield multiplier applied on pickup.
// CARNIVORE thrives on MEAT (1.5×), struggles on PLANT (0.4×), and vice
// versa for HERBIVORE. OMNIVORE gets a flat 1.0× either way — jack of all
// diets, master of none.
inline float yieldMultiplier(Diet diet, FoodType food) {
	switch (diet) {
	case Diet::CARNIVORE: return (food == FoodType::MEAT)  ? 1.5f : 0.4f;
	case Diet::HERBIVORE: return (food == FoodType::PLANT) ? 1.5f : 0.4f;
	case Diet::OMNIVORE:
	default:              return 1.0f;
	}
}

struct Food {
	uint32_t  id      = 0;
	glm::vec2 pos     = glm::vec2(0.0f);
	float     biomass = 1.0f;
	FoodType  type    = FoodType::PLANT;
	uint32_t  seed    = 0;   // per-food stable noise seed (shape/wobble)
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

	uint32_t add_food(const glm::vec2& pos, float biomass,
	                  FoodType type = FoodType::PLANT) {
		Food f;
		f.id      = next_id++;
		f.pos     = pos;
		f.biomass = biomass;
		f.type    = type;
		f.seed    = (uint32_t)rng();
		food.push_back(f);
		return f.id;
	}

	void scatter_food(int n) {
		std::uniform_real_distribution<float> rad_dist(0.0f, map_radius * 0.95f);
		std::uniform_real_distribution<float> ang_dist(0.0f, 6.28318530718f);
		// Plant is smaller (4–9), meat is chunkier (8–15).
		std::uniform_real_distribution<float> plant_bm(FOOD_PLANT_BIOMASS_MIN, FOOD_PLANT_BIOMASS_MAX);
		std::uniform_real_distribution<float> meat_bm (FOOD_MEAT_BIOMASS_MIN,  FOOD_MEAT_BIOMASS_MAX);
		std::uniform_real_distribution<float> kind_dist(0.0f, 1.0f);
		for (int i = 0; i < n; ++i) {
			float r = std::sqrt(rad_dist(rng) / map_radius) * map_radius;
			float a = ang_dist(rng);
			glm::vec2 p(std::cos(a) * r, std::sin(a) * r);
			// 60% plant / 40% meat.
			bool is_plant = kind_dist(rng) < FOOD_PLANT_FRACTION;
			FoodType t = is_plant ? FoodType::PLANT : FoodType::MEAT;
			float bm = is_plant ? plant_bm(rng) : meat_bm(rng);
			add_food(p, bm, t);
		}
	}
};

} // namespace civcraft::cellcraft::sim
