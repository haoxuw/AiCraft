#pragma once

// Built-in C++ brains for EvolveCraft stages 5–6.
// These are stand-ins for the Python-moddable species brains described in
// DESIGN.md §4. The Python runtime (BrainRuntime) goes in at a later stage;
// for the vertical slice we compute actions here so the sim is observably
// non-trivial end-to-end.
//
// When Python is added, the BrainRuntime becomes a PondSim::m_decideHook
// replacement; the decide surface is the same.

#include "server/pond_sim.h"
#include "shared/creature.h"
#include <cmath>

namespace evolvecraft {

namespace detail {

static inline glm::vec3 steer(const Creature& me, glm::vec3 target, float speed) {
	glm::vec3 d = target - me.pos;
	d.y = 0;
	float len = std::sqrt(d.x*d.x + d.z*d.z);
	if (len < 0.01f) return {0, 0, 0};
	return (d / len) * speed;
}

// Find nearest alive creature matching predicate (species etc.).
template <typename Pred>
static const Creature* nearestCreature(const std::vector<Creature>& cs,
                                       const Creature& me, float maxR, Pred pred) {
	float best = maxR * maxR;
	const Creature* out = nullptr;
	for (auto& c : cs) {
		if (!c.alive || c.id == me.id) continue;
		if (!pred(c)) continue;
		float dx = c.pos.x - me.pos.x, dz = c.pos.z - me.pos.z;
		float d = dx*dx + dz*dz;
		if (d < best) { best = d; out = &c; }
	}
	return out;
}

static const Food* nearestFood(const std::vector<Food>& fs, const Creature& me, float maxR) {
	float best = maxR * maxR;
	const Food* out = nullptr;
	for (auto& f : fs) {
		if (!f.alive) continue;
		float dx = f.pos.x - me.pos.x, dz = f.pos.z - me.pos.z;
		float d = dx*dx + dz*dz;
		if (d < best) { best = d; out = &f; }
	}
	return out;
}

} // namespace detail

static inline void installBrains(PondSim& sim) {
	sim.m_decideHook = [](PondSim& sim, float dt) {
		auto& cs = sim.creaturesMut();
		auto& foods = sim.foods();

		for (auto& me : cs) {
			if (!me.alive) continue;
			glm::vec3 desired = {0, 0, 0};

			switch (me.species) {
			case Species::Wanderer: {
				// Every ~1s, pick a new random goal near current pos. Persist it
				// in `goal` so we can display it for debugging.
				if (sim.time() - me.age > 0 && (((int)(sim.time()*2) ^ me.id) & 63) == 0) {
					float a = sim.randUniform(0, 6.28f);
					float r = sim.randUniform(6.0f, 18.0f);
					me.goal = me.pos + glm::vec3(std::cos(a)*r, 0, std::sin(a)*r);
				}
				// Also seek nearest food opportunistically.
				auto* f = detail::nearestFood(foods, me, me.effectiveSense);
				glm::vec3 tgt = f ? f->pos : me.goal;
				desired = detail::steer(me, tgt, me.effectiveSpeed);
				break;
			}
			case Species::Prey: {
				// Flee nearest predator, else seek food.
				auto* pred = detail::nearestCreature(cs, me, me.effectiveSense * 1.2f,
					[](const Creature& c){ return c.species == Species::Predator; });
				if (pred) {
					glm::vec3 flee = me.pos - pred->pos;
					flee.y = 0;
					float L = std::sqrt(flee.x*flee.x + flee.z*flee.z);
					if (L > 0.01f) flee /= L;
					me.goal = me.pos + flee * 12.0f;
					desired = flee * me.effectiveSpeed * 1.25f;
				} else {
					auto* f = detail::nearestFood(foods, me, me.effectiveSense);
					if (f) {
						me.goal = f->pos;
						desired = detail::steer(me, f->pos, me.effectiveSpeed);
					} else {
						if ((((int)(sim.time()*2) ^ me.id) & 31) == 0) {
							float a = sim.randUniform(0, 6.28f);
							me.goal = me.pos + glm::vec3(std::cos(a), 0, std::sin(a)) * 12.0f;
						}
						desired = detail::steer(me, me.goal, me.effectiveSpeed * 0.6f);
					}
				}
				break;
			}
			case Species::Predator: {
				auto* prey = detail::nearestCreature(cs, me, me.effectiveSense,
					[](const Creature& c){ return c.species == Species::Prey; });
				if (prey) {
					me.goal = prey->pos;
					desired = detail::steer(me, prey->pos, me.effectiveSpeed * 1.1f);
				} else {
					if ((((int)(sim.time()*2) ^ me.id) & 63) == 0) {
						float a = sim.randUniform(0, 6.28f);
						float r = sim.randUniform(8.0f, 20.0f);
						me.goal = me.pos + glm::vec3(std::cos(a)*r, 0, std::sin(a)*r);
					}
					desired = detail::steer(me, me.goal, me.effectiveSpeed * 0.8f);
				}
				break;
			}
			}

			// Blend desired into current velocity (smooth steering).
			glm::vec3 delta = desired - me.vel;
			delta.y = 0;
			me.vel += delta * std::min(1.0f, 4.0f * dt);
			me.vel.y = 0;
		}

		// === Reproduction (Stage 6) ===
		// A cell with energy >= splitThresh clones, halves energy, and offspring
		// gets gaussian-mutated DNA. Each species reproduces naturally.
		struct PendingBirth { CreatureId parentId; glm::vec3 parentPos; SpeciesId sp; OwnerId owner; DNA dna; glm::vec3 pos; };
		std::vector<PendingBirth> births;
		for (auto& me : cs) {
			if (!me.alive) continue;
			if (me.energy < me.dna.splitThresh) continue;
			if ((int)cs.size() + (int)births.size() >= 220) continue; // soft cap

			me.energy *= 0.5f;
			DNA childDna = me.dna;
			childDna.size       = std::max(0.4f, childDna.size       * (1.0f + sim.randGauss(0, 0.04f)));
			childDna.baseSpeed  = std::max(1.0f, childDna.baseSpeed  * (1.0f + sim.randGauss(0, 0.05f)));
			childDna.baseSense  = std::max(4.0f, childDna.baseSense  * (1.0f + sim.randGauss(0, 0.05f)));
			childDna.metabolism = std::max(0.1f, childDna.metabolism * (1.0f + sim.randGauss(0, 0.05f)));
			childDna.aggression = std::clamp(childDna.aggression + sim.randGauss(0, 0.03f), 0.0f, 1.0f);
			childDna.caution    = std::clamp(childDna.caution    + sim.randGauss(0, 0.03f), 0.0f, 1.0f);
			childDna.colorHue   = std::fmod(childDna.colorHue + sim.randGauss(0, 0.015f) + 1.0f, 1.0f);

			glm::vec3 offset = { sim.randUniform(-1.0f, 1.0f), 0, sim.randUniform(-1.0f, 1.0f) };
			births.push_back({me.id, me.pos, me.species, me.owner, childDna, me.pos + offset});
		}
		for (auto& b : births) {
			// Find the parent (may have died/been compacted between frames;
			// we still want to log a split so we pass what we captured).
			Creature& child = sim.spawnCreature(b.pos, b.sp, b.dna, b.owner, b.parentId);
			if (sim.m_onSplitHook)  sim.m_onSplitHook(b.parentPos);
			if (sim.m_onSplitEvent) {
				// Build a parent proxy from the captured snapshot so the log
				// sees parent identity even if the parent has since moved.
				Creature parentProxy;
				parentProxy.id = b.parentId;
				parentProxy.species = b.sp;
				parentProxy.owner = b.owner;
				parentProxy.pos = b.parentPos;
				sim.m_onSplitEvent(parentProxy, child);
			}
		}
	};
}

} // namespace evolvecraft
