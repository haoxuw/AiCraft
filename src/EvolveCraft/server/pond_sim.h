#pragma once

// PondSim — authoritative simulation for EvolveCraft.
//
// Owns the SwimField, the list of creatures, and the list of food. Runs one
// tick at a time; each tick calls brains (C++ for stages 1-3, Python for 4+),
// integrates physics, resolves eats/attacks, spawns food, logs events.
//
// Singleplayer runs this on the same thread as the renderer. Multiplayer
// (stage 10) moves it into its own thread + server process.

#include "shared/swim_field.h"
#include "shared/creature.h"
#include "shared/food.h"
#include "shared/species_presets.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <random>
#include <cstdio>
#include <cmath>
#include <functional>
#include <algorithm>

namespace evolvecraft {

struct SimConfig {
	float pondRadius = 60.0f;
	int   initialCells = 1;
	int   initialFood = 0;
	float foodSpawnPerSec = 0.0f;
	int   maxFood = 0;
	unsigned seed = 1337;
};

struct SimStats {
	int tickCount = 0;
	int creaturesAlive = 0;
	int foodAlive = 0;
	float totalEnergy = 0.0f;
};

class PondSim {
public:
	void init(const SimConfig& cfg) {
		m_cfg = cfg;
		m_field.radius = cfg.pondRadius;
		m_rng.seed(cfg.seed);
		m_nextId = 1;
		m_creatures.clear();
		m_foods.clear();

		for (int i = 0; i < cfg.initialCells; i++) {
			DNA d;
			d.colorHue = (float)i / std::max(1, cfg.initialCells);
			spawnCreature(randomPondPos(cfg.pondRadius * 0.4f),
			              Species::Wanderer, d);
		}
		for (int i = 0; i < cfg.initialFood; i++) spawnFoodRandom();
	}

	// Stage 1: extremely simple — one cell, does nothing. We still run physics
	// + boundary so the cell stays inside the pond. Future stages will inject
	// a "decide" pass before physics.
	void tick(float dt) {
		m_stats.tickCount++;
		m_time += dt;

		// Optional food spawn (stage 2+)
		if (m_cfg.foodSpawnPerSec > 0.0f && (int)m_foods.size() < m_cfg.maxFood) {
			m_foodSpawnAccum += dt * m_cfg.foodSpawnPerSec;
			while (m_foodSpawnAccum >= 1.0f && (int)m_foods.size() < m_cfg.maxFood) {
				m_foodSpawnAccum -= 1.0f;
				spawnFoodRandom();
			}
		}

		// === DECIDE pass (hook for Python brains later) ===
		if (m_decideHook) m_decideHook(*this, dt);

		// === INTEGRATE physics + boundary ===
		for (auto& c : m_creatures) {
			if (!c.alive) continue;
			c.age += dt;
			c.energy -= c.dna.metabolism * dt;

			// Starvation
			if (c.energy <= 0.0f) {
				c.energy = 0.0f;
				c.hp -= 1;
				if (c.hp <= 0) {
					c.alive = false;
					if (m_onDeathEvent) m_onDeathEvent(c, "starved");
				}
			}

			// Boundary force
			c.vel += m_field.boundaryForce(c.pos) * dt * 8.0f;

			// Drag (water resistance)
			float drag = 1.5f;
			c.vel *= std::exp(-drag * dt);

			// Integrate
			c.pos += c.vel * dt;

			// Lock to water plane (with a tiny bob)
			c.bobPhase += dt * 2.0f;
			c.pos.y = m_field.waterLevel + 0.15f * std::sin(c.bobPhase);

			// Yaw follows velocity
			glm::vec2 vxz(c.vel.x, c.vel.z);
			if (glm::dot(vxz, vxz) > 0.01f) {
				c.yaw = std::atan2(vxz.x, vxz.y);  // facing direction
			}
		}

		// === RESOLVE: eat food (stage 2+) ===
		for (auto& c : m_creatures) {
			if (!c.alive) continue;
			for (auto& f : m_foods) {
				if (!f.alive) continue;
				float dx = c.pos.x - f.pos.x;
				float dz = c.pos.z - f.pos.z;
				float r = f.radius + 0.5f * c.dna.size;
				if (dx*dx + dz*dz <= r*r) {
					c.energy += f.nutrition;
					if (c.hp < c.maxHp) c.hp = std::min(c.maxHp, c.hp + 2);
					f.alive = false;
					if (m_onEatHook)  m_onEatHook(c.pos);
					if (m_onEatEvent) m_onEatEvent(c, f);
				}
			}
		}

		// === RESOLVE: combat (stage 5+) ===
		if (m_combatEnabled) resolveCombat();

		// Compact dead entities occasionally
		if ((m_stats.tickCount & 63) == 0) compact();

		updateStats();
	}

	// Accessors
	const SwimField& field() const { return m_field; }
	const std::vector<Creature>& creatures() const { return m_creatures; }
	std::vector<Creature>& creaturesMut() { return m_creatures; }
	const std::vector<Food>& foods() const { return m_foods; }
	std::vector<Food>& foodsMut() { return m_foods; }
	const SimStats& stats() const { return m_stats; }
	std::mt19937& rng() { return m_rng; }
	float time() const { return m_time; }

	// Spawn helpers (used by brains, tests, reproduction)
	Creature& spawnCreature(glm::vec3 pos, SpeciesId sp, const DNA& dna,
	                        OwnerId owner = 0, CreatureId parent = 0) {
		Creature c;
		c.id = m_nextId++;
		c.species = sp;
		c.owner = owner;
		c.pos = pos;
		c.dna = dna;
		c.body = presetBodyFor(sp, dna);
		// Stat aggregation: DNA + sum of part contributions.
		float dAttack = 0, dSpeed = 0, dSense = 0;
		for (int i = 0; i < c.body.partCount; i++) {
			dAttack += c.body.parts[i].dAttack;
			dSpeed  += c.body.parts[i].dSpeed;
			dSense  += c.body.parts[i].dSense;
		}
		c.hp = c.maxHp = 10 + (int)(dna.size * 4) + (int)dAttack;
		c.energy = 5.0f;
		c.effectiveSpeed = dna.baseSpeed + dSpeed;
		c.effectiveSense = dna.baseSense + dSense;
		c.bobPhase = randUniform(0, 6.28f);
		m_creatures.push_back(c);
		if (m_onBirthEvent) m_onBirthEvent(m_creatures.back(), parent);
		return m_creatures.back();
	}

	Food& spawnFood(glm::vec3 pos, float nutrition = 3.0f) {
		Food f;
		f.id = m_nextId++;
		f.pos = pos;
		f.nutrition = nutrition;
		f.bobPhase = randUniform(0, 6.28f);
		m_foods.push_back(f);
		return m_foods.back();
	}

	void spawnFoodRandom() {
		glm::vec3 p = randomPondPos(m_cfg.pondRadius * 0.92f);
		spawnFood(p, 3.0f);
	}

	// Hook points (set before tick). Keeps PondSim itself free of game rules.
	//
	// Two parallel hook sets:
	//   • Visual hooks (pos-only): fed to the GUI particle system. Kept simple
	//     so they're free to call and existing call sites don't churn.
	//   • Event hooks (rich): fed to detailed game loggers. Pass the involved
	//     creatures/food directly so the log can include id, species, dna, …
	// Both sets fire from the same code paths; either can be left null.
	std::function<void(PondSim&, float dt)> m_decideHook;
	std::function<void(glm::vec3 pos)>      m_onEatHook;
	std::function<void(glm::vec3 pos)>      m_onSplitHook;
	std::function<void(glm::vec3 pos)>      m_onDeathHook;

	// Rich event hooks for headless logging.
	std::function<void(const Creature&, CreatureId parent)>            m_onBirthEvent;
	std::function<void(const Creature&, const char* cause)>            m_onDeathEvent;
	std::function<void(const Creature&, const Food&)>                  m_onEatEvent;
	std::function<void(const Creature&, const Creature&, int dmg)>     m_onAttackEvent;
	std::function<void(const Creature& parent, const Creature& child)> m_onSplitEvent;
	bool m_combatEnabled = false;

	float randUniform(float lo, float hi) {
		std::uniform_real_distribution<float> d(lo, hi);
		return d(m_rng);
	}
	float randGauss(float mean, float stddev) {
		std::normal_distribution<float> d(mean, stddev);
		return d(m_rng);
	}

	glm::vec3 randomPondPos(float maxRadius) {
		float r = maxRadius * std::sqrt(randUniform(0, 1));
		float a = randUniform(0, 6.2831853f);
		return { r * std::cos(a), m_field.waterLevel, r * std::sin(a) };
	}

private:
	void updateStats() {
		m_stats.creaturesAlive = 0;
		m_stats.foodAlive = 0;
		m_stats.totalEnergy = 0;
		for (auto& c : m_creatures) if (c.alive) {
			m_stats.creaturesAlive++;
			m_stats.totalEnergy += c.energy;
		}
		for (auto& f : m_foods) if (f.alive) m_stats.foodAlive++;
	}

	void compact() {
		auto ce = std::remove_if(m_creatures.begin(), m_creatures.end(),
			[&](const Creature& c) {
				if (!c.alive && m_onDeathHook) m_onDeathHook(c.pos);
				return !c.alive;
			});
		m_creatures.erase(ce, m_creatures.end());

		auto fe = std::remove_if(m_foods.begin(), m_foods.end(),
			[](const Food& f){ return !f.alive; });
		m_foods.erase(fe, m_foods.end());
	}

	void resolveCombat() {
		// Simple O(n²) overlap check; fine at stage 5 scale.
		for (size_t i = 0; i < m_creatures.size(); i++) {
			auto& a = m_creatures[i];
			if (!a.alive) continue;
			if (a.species != Species::Predator) continue;
			for (size_t j = 0; j < m_creatures.size(); j++) {
				if (i == j) continue;
				auto& b = m_creatures[j];
				if (!b.alive) continue;
				if (b.species != Species::Prey) continue;

				float dx = a.pos.x - b.pos.x, dz = a.pos.z - b.pos.z;
				float reach = 0.6f * (a.dna.size + b.dna.size);
				if (dx*dx + dz*dz <= reach*reach) {
					int dmg = 2 + (int)(a.dna.aggression * 3.0f);
					b.hp -= dmg;
					a.energy -= 0.3f;
					if (m_onAttackEvent) m_onAttackEvent(a, b, dmg);
					if (b.hp <= 0) {
						b.alive = false;
						a.energy += 4.0f; // meal
						if (m_onDeathHook)  m_onDeathHook(b.pos);
						if (m_onDeathEvent) m_onDeathEvent(b, "killed");
					}
				}
			}
		}
	}

	SimConfig m_cfg;
	SwimField m_field;
	std::vector<Creature> m_creatures;
	std::vector<Food> m_foods;
	std::mt19937 m_rng;
	CreatureId m_nextId = 1;
	float m_time = 0.0f;
	float m_foodSpawnAccum = 0.0f;
	SimStats m_stats;
};

} // namespace evolvecraft
