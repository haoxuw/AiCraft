// CellCraft — prebuilt monster templates.
//
// Authored as RadialCell + parts (the same representation the Creature
// Lab produces). `makeMonsterFromTemplate` materializes a sim::Monster
// ready to spawn.
//
// Convention: head is at local +y (θ = π/2), tail at −y. Parts are
// stored in the monster's local angle space.

#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/sim/monster.h"
#include "CellCraft/sim/part.h"
#include "CellCraft/sim/radial_cell.h"

namespace civcraft::cellcraft::monsters {

struct MonsterTemplate {
	std::string id;
	std::string name;
	sim::RadialCell cell;              // playdough shape
	std::vector<sim::Part>  parts;     // mods
	glm::vec3 color = glm::vec3(1.0f);
	float     initial_biomass = 20.0f;

	// Convenience: polygon derived from the cell.
	std::vector<glm::vec2> shape() const { return sim::cellToPolygon(cell, 1); }
};

namespace detail {

// Build an elongated cell: head_r at θ=π/2 (+y), side_r at θ=0 (+x). Uses
// a cosine-shaped interpolation so the body tapers smoothly.
inline sim::RadialCell elongated(float head_r, float tail_r, float side_r) {
	sim::RadialCell c; c.init_circle(side_r);
	for (int i = 0; i < sim::RadialCell::N; ++i) {
		float a = sim::RadialCell::TWO_PI * (float)i / (float)sim::RadialCell::N;
		// Interpolate between side_r (at ±x) and head_r/tail_r (at ±y).
		float sy = std::sin(a);                // +1 head, −1 tail
		float k  = sy * sy;                    // 1 at poles, 0 at equator
		float pole = (sy >= 0.0f) ? head_r : tail_r;
		c.r[i] = side_r + (pole - side_r) * k;
	}
	c.enforce_symmetry_();
	return c;
}

// Circle with low-frequency wobble so it isn't a perfect disk.
inline sim::RadialCell wobble_circle(float R, float amp, int freq) {
	sim::RadialCell c; c.init_circle(R);
	for (int i = 0; i < sim::RadialCell::N; ++i) {
		float a = sim::RadialCell::TWO_PI * (float)i / (float)sim::RadialCell::N;
		c.r[i] = R * (1.0f + amp * std::cos(a * (float)freq));
	}
	c.enforce_symmetry_();
	return c;
}

} // namespace detail

inline std::vector<MonsterTemplate> getPrebuiltMonsters() {
	using namespace detail;
	const float PI = 3.14159265359f;
	std::vector<MonsterTemplate> out;

	// ── Stinger ─ tall narrow body, twin spikes + venom up top, flagella down.
	{
		MonsterTemplate m;
		m.id    = "base:stinger";
		m.name  = "Stinger";
		m.color = glm::vec3(0.95f, 0.30f, 0.35f);
		m.initial_biomass = 18.0f;
		m.cell = elongated(70.0f, 70.0f, 18.0f);
		// SPIKES at top (head +y); VENOM also at top; FLAGELLA at bottom.
		m.parts.push_back({sim::PartType::SPIKE,       {  8.0f,  58.0f}, PI * 0.5f, 1.3f});
		m.parts.push_back({sim::PartType::SPIKE,       { -8.0f,  58.0f}, PI * 0.5f, 1.3f});
		m.parts.push_back({sim::PartType::VENOM_SPIKE, {  0.0f,  64.0f}, PI * 0.5f, 1.0f});
		m.parts.push_back({sim::PartType::MOUTH,       {  0.0f,  42.0f}, PI * 0.5f, 1.0f});
		m.parts.push_back({sim::PartType::FLAGELLA,    {  0.0f, -60.0f}, -PI * 0.5f, 1.0f});
		out.push_back(m);
	}

	// ── Blob ─ nearly-circular, 2 ARMOR + REGEN + POISON.
	{
		MonsterTemplate m;
		m.id    = "base:blob";
		m.name  = "Blob";
		m.color = glm::vec3(0.45f, 0.80f, 0.55f);
		m.initial_biomass = 40.0f;
		m.cell = wobble_circle(50.0f, 0.04f, 3);
		m.parts.push_back({sim::PartType::ARMOR,  {  15.0f,   0.0f}, 0.0f,      1.5f});
		m.parts.push_back({sim::PartType::ARMOR,  { -15.0f,   0.0f}, PI,        1.5f});
		m.parts.push_back({sim::PartType::REGEN,  {   0.0f,   0.0f}, 0.0f,      1.2f});
		m.parts.push_back({sim::PartType::POISON, {   0.0f, -18.0f}, -PI * 0.5f, 1.3f});
		m.parts.push_back({sim::PartType::MOUTH,  {   0.0f,  30.0f},  PI * 0.5f, 1.0f});
		out.push_back(m);
	}

	// ── Dart ─ elongated, FLAGELLAx2 at tail, TEETH up top, EYES.
	{
		MonsterTemplate m;
		m.id    = "base:dart";
		m.name  = "Dart";
		m.color = glm::vec3(0.35f, 0.55f, 0.95f);
		m.initial_biomass = 25.0f;
		m.cell = elongated(80.0f, 80.0f, 22.0f);
		m.parts.push_back({sim::PartType::FLAGELLA, {  8.0f, -68.0f}, -PI * 0.5f});
		m.parts.push_back({sim::PartType::FLAGELLA, { -8.0f, -68.0f}, -PI * 0.5f});
		m.parts.push_back({sim::PartType::TEETH,    {  0.0f,  70.0f},  PI * 0.5f});
		m.parts.push_back({sim::PartType::EYES,     {  0.0f,  40.0f},  PI * 0.5f});
		m.parts.push_back({sim::PartType::MOUTH,    {  0.0f,  55.0f},  PI * 0.5f, 1.0f});
		out.push_back(m);
	}

	// ── Tusker ─ wide body, HORN frontal, flagella tail, armor side.
	{
		MonsterTemplate m;
		m.id    = "base:tusker";
		m.name  = "Tusker";
		m.color = glm::vec3(0.85f, 0.65f, 0.35f);
		m.initial_biomass = 32.0f;
		m.cell = elongated(50.0f, 42.0f, 40.0f);
		m.parts.push_back({sim::PartType::HORN,     {  0.0f,  40.0f},  PI * 0.5f, 1.6f});
		m.parts.push_back({sim::PartType::MOUTH,    {  0.0f,  28.0f},  PI * 0.5f, 1.0f});
		m.parts.push_back({sim::PartType::FLAGELLA, {  0.0f, -36.0f}, -PI * 0.5f, 1.0f});
		m.parts.push_back({sim::PartType::ARMOR,    { 34.0f,   0.0f},  0.0f,      1.1f});
		out.push_back(m);
	}

	return out;
}

// Build a Monster ready to hand to World::spawn_monster.
inline sim::Monster makeMonsterFromTemplate(const MonsterTemplate& t,
                                            uint32_t owner,
                                            glm::vec2 pos,
                                            float heading) {
	sim::Monster m;
	m.owner_id = owner;
	m.core_pos = pos;
	m.heading  = heading;
	m.shape    = t.shape();
	m.color    = t.color;
	m.biomass  = t.initial_biomass;
	// Lifetime biomass seeded from the starter budget so an already-
	// chunky prebuilt enters the match at the right tier (and its body
	// is pre-scaled to match).
	m.lifetime_biomass = t.initial_biomass;
	m.tier     = sim::computeTier(m.lifetime_biomass);
	m.parts    = t.parts;
	if (m.tier > 1) {
		float s = sim::tierSizeMult(m.tier);
		for (auto& v : m.shape) v *= s;
	}
	m.refresh_stats();
	m.hp = m.hp_max;
	return m;
}

} // namespace civcraft::cellcraft::monsters
