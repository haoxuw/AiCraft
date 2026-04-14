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
#include "LifeCraft/sim/part.h"

namespace civcraft::lifecraft::monsters {

struct MonsterTemplate {
	std::string id;
	std::string name;
	std::vector<glm::vec2> shape;     // local space, core at origin
	glm::vec3 color = glm::vec3(1.0f);
	float     initial_biomass = 20.0f;
	std::vector<sim::Part> parts;
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
		// Pointy wedge: 1 spike + 1 venom spike at the tip + a flagella.
		m.parts.push_back({sim::PartType::SPIKE,       { 58.0f,  4.0f}, 0.0f});
		m.parts.push_back({sim::PartType::VENOM_SPIKE, { 58.0f, -4.0f}, 0.0f});
		m.parts.push_back({sim::PartType::FLAGELLA,    {-24.0f,  0.0f}, 3.14159f});
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
		// 2 armor plates + poison core + 2 regen for tanky-healer feel.
		m.parts.push_back({sim::PartType::ARMOR,  { 20.0f,   0.0f}, 0.0f});
		m.parts.push_back({sim::PartType::ARMOR,  {-10.0f,  17.0f}, 2.094f});
		m.parts.push_back({sim::PartType::POISON, {  0.0f,   0.0f}, 0.0f});
		m.parts.push_back({sim::PartType::REGEN,  { -8.0f,  -8.0f}, 0.0f});
		m.parts.push_back({sim::PartType::REGEN,  {  8.0f,  -8.0f}, 0.0f});
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
		// Speedster: 2 flagella + 1 cilia for turn + mouth for food + teeth.
		m.parts.push_back({sim::PartType::FLAGELLA, {-50.0f,  4.0f}, 3.14159f});
		m.parts.push_back({sim::PartType::FLAGELLA, {-50.0f, -4.0f}, 3.14159f});
		m.parts.push_back({sim::PartType::CILIA,    {  0.0f,  9.0f}, 1.57f});
		m.parts.push_back({sim::PartType::MOUTH,    { 52.0f,  0.0f}, 0.0f});
		out.push_back(m);

		// ── tusker ────────────────────────────────────────────────────────
		// Chonky attacker with a HORN out front — hits like a truck frontally.
		{
			MonsterTemplate tk;
			tk.id = "base:tusker";
			tk.name = "Tusker";
			tk.color = glm::vec3(0.85f, 0.65f, 0.35f);
			tk.initial_biomass = 32.0f;
			// Teardrop pointing +X.
			const int N2 = 14;
			for (int i = 0; i < N2; ++i) {
				float a = 6.28318530718f * float(i) / float(N2);
				float r = 26.0f + 8.0f * std::cos(a);
				tk.shape.emplace_back(std::cos(a) * r * 1.1f, std::sin(a) * r * 0.8f);
			}
			tk.parts.push_back({sim::PartType::HORN,     { 34.0f,  0.0f}, 0.0f});
			tk.parts.push_back({sim::PartType::FLAGELLA, {-30.0f,  0.0f}, 3.14159f});
			tk.parts.push_back({sim::PartType::ARMOR,    { -6.0f, 16.0f}, 1.57f});
			out.push_back(tk);
		}
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
	m.parts    = t.parts;
	m.refresh_stats(); // sets hp_max from biomass + parts
	m.hp       = m.hp_max;
	return m;
}

} // namespace civcraft::lifecraft::monsters
