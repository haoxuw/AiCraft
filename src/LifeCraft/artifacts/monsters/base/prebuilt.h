// LifeCraft — prebuilt monster templates. Hand-authored polygons in
// monster-local space (core at origin). Three archetypes keyed by id.
//
// TODO: move to Python artifacts (artifacts/monsters/base/*.py) once
// the pybind surface lands; this header is a C++ stub so the sim has
// content to exercise in unit tests and the client's first bring-up.

#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "LifeCraft/sim/monster.h"

namespace civcraft::lifecraft::monsters {

struct MonsterTemplate {
	std::string id;
	std::string name;
	std::vector<glm::vec2> shape;     // local space, core at origin
	glm::vec3 color = glm::vec3(1.0f);
	float     initial_biomass = 20.0f;
};

inline std::vector<MonsterTemplate> getPrebuiltMonsters() {
	std::vector<MonsterTemplate> out;

	// ── stinger ────────────────────────────────────────────────────────
	// Long narrow triangle-ish wedge, sharp tip on +X. Narrow frontal
	// area → fast forward; tight core radius on the wide end → decent
	// turn. 8 verts.
	{
		MonsterTemplate m;
		m.id = "base:stinger";
		m.name = "Stinger";
		m.color = glm::vec3(0.95f, 0.30f, 0.35f);
		m.initial_biomass = 18.0f;
		m.shape = {
			{ 60.0f,  0.0f},
			{ 30.0f,  6.0f},
			{  5.0f, 10.0f},
			{-20.0f, 12.0f},
			{-25.0f,  0.0f},
			{-20.0f,-12.0f},
			{  5.0f,-10.0f},
			{ 30.0f, -6.0f},
		};
		out.push_back(m);
	}

	// ── blob ──────────────────────────────────────────────────────────
	// Near-circle, 16 verts, radius ~22. Tanky, medium turn/speed.
	{
		MonsterTemplate m;
		m.id = "base:blob";
		m.name = "Blob";
		m.color = glm::vec3(0.45f, 0.80f, 0.55f);
		m.initial_biomass = 40.0f;
		const int N = 16;
		const float R = 22.0f;
		for (int i = 0; i < N; ++i) {
			float a = 6.28318530718f * float(i) / float(N);
			// Slight wobble so it isn't a perfect circle.
			float r = R * (0.92f + 0.08f * std::cos(a * 3.0f));
			m.shape.emplace_back(std::cos(a) * r, std::sin(a) * r);
		}
		out.push_back(m);
	}

	// ── dart ───────────────────────────────────────────────────────────
	// Elongated ellipse (long on X). Fast forward, slow turn (long
	// reach radius). 20 verts.
	{
		MonsterTemplate m;
		m.id = "base:dart";
		m.name = "Dart";
		m.color = glm::vec3(0.35f, 0.55f, 0.95f);
		m.initial_biomass = 25.0f;
		const int N = 20;
		const float A = 55.0f; // semi-major
		const float B = 10.0f; // semi-minor
		for (int i = 0; i < N; ++i) {
			float t = 6.28318530718f * float(i) / float(N);
			m.shape.emplace_back(std::cos(t) * A, std::sin(t) * B);
		}
		out.push_back(m);
	}

	return out;
}

// Convenience: build a Monster ready to hand to World::spawn_monster.
inline sim::Monster makeMonsterFromTemplate(const MonsterTemplate& t,
                                            uint32_t owner,
                                            glm::vec2 pos,
                                            float heading) {
	sim::Monster m;
	m.owner_id = owner;
	m.core_pos = pos;
	m.heading  = heading;
	m.shape    = t.shape;
	m.color    = t.color;
	m.biomass  = t.initial_biomass;
	m.hp_max   = t.initial_biomass * sim::HP_PER_BIOMASS;
	m.hp       = m.hp_max;
	m.refresh_stats();
	return m;
}

} // namespace civcraft::lifecraft::monsters
