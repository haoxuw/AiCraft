// CellCraft — starter creature templates for the picker screen.
//
// Six big, readable starters. Each one uses the original part names
// (SPIKE, TEETH, FLAGELLA, ...). RANDOM and PLAIN are special-cased
// by the caller.

#pragma once

#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/artifacts/monsters/base/prebuilt.h"
#include "CellCraft/sim/part.h"
#include "CellCraft/sim/radial_cell.h"

namespace civcraft::cellcraft::monsters {

enum class StarterKind {
	SPIKY = 0,
	SQUISHY,
	ZOOMY,
	TOUGH,
	RANDOM,
	PLAIN,
	KIND_COUNT,
};

inline const char* starterName(StarterKind k) {
	switch (k) {
	case StarterKind::SPIKY:   return "SPIKY";
	case StarterKind::SQUISHY: return "SQUISHY";
	case StarterKind::ZOOMY:   return "ZOOMY";
	case StarterKind::TOUGH:   return "TOUGH";
	case StarterKind::RANDOM:  return "RANDOM";
	case StarterKind::PLAIN:   return "PLAIN";
	default: return "?";
	}
}

inline const char* starterTagline(StarterKind k) {
	switch (k) {
	case StarterKind::SPIKY:   return "POKES THINGS";
	case StarterKind::SQUISHY: return "POISON BLOB";
	case StarterKind::ZOOMY:   return "GO FAST";
	case StarterKind::TOUGH:   return "HARD TO HURT";
	case StarterKind::RANDOM:  return "MYSTERY!";
	case StarterKind::PLAIN:   return "MAKE YOUR OWN";
	default: return "";
	}
}

namespace starter_detail {
inline const float PI = 3.14159265359f;
} // namespace starter_detail

inline MonsterTemplate makeStarter(StarterKind k, std::mt19937& rng) {
	using namespace detail; // elongated, wobble_circle from prebuilt.h
	using starter_detail::PI;
	MonsterTemplate m;
	m.id = std::string("starter:") + starterName(k);
	m.name = starterName(k);
	m.initial_biomass = 25.0f;
	switch (k) {
	case StarterKind::SPIKY: {
		m.color = glm::vec3(0.95f, 0.35f, 0.40f);
		m.cell  = elongated(70.0f, 70.0f, 22.0f);
		// 4 SPIKES + 2 VENOM SPIKES — symmetric pairs.
		m.parts.push_back({sim::PartType::SPIKE,       { 12.0f,  56.0f},  PI*0.5f, 1.2f});
		m.parts.push_back({sim::PartType::SPIKE,       {-12.0f,  56.0f},  PI*0.5f, 1.2f});
		m.parts.push_back({sim::PartType::SPIKE,       { 18.0f,  20.0f},  0.20f,   1.0f});
		m.parts.push_back({sim::PartType::SPIKE,       {-18.0f,  20.0f},  PI-0.20f,1.0f});
		m.parts.push_back({sim::PartType::VENOM_SPIKE, {  6.0f,  64.0f},  PI*0.5f, 1.0f});
		m.parts.push_back({sim::PartType::VENOM_SPIKE, { -6.0f,  64.0f},  PI*0.5f, 1.0f});
		m.parts.push_back({sim::PartType::MOUTH,       {  0.0f,  40.0f},  PI*0.5f, 1.0f});
		break;
	}
	case StarterKind::SQUISHY: {
		m.color = glm::vec3(0.55f, 0.85f, 0.55f);
		m.initial_biomass = 35.0f;
		m.cell = wobble_circle(55.0f, 0.06f, 4);
		m.parts.push_back({sim::PartType::POISON, {   0.0f,   0.0f}, 0.0f,    1.4f});
		m.parts.push_back({sim::PartType::ARMOR,  {  28.0f,   0.0f}, 0.0f,    1.2f});
		m.parts.push_back({sim::PartType::ARMOR,  { -28.0f,   0.0f}, PI,      1.2f});
		m.parts.push_back({sim::PartType::MOUTH,  {   0.0f,  33.0f}, PI*0.5f, 1.0f});
		break;
	}
	case StarterKind::ZOOMY: {
		m.color = glm::vec3(0.45f, 0.65f, 1.00f);
		m.cell = elongated(85.0f, 75.0f, 18.0f);
		m.parts.push_back({sim::PartType::FLAGELLA, {  8.0f, -65.0f}, -PI*0.5f, 1.1f});
		m.parts.push_back({sim::PartType::FLAGELLA, { -8.0f, -65.0f}, -PI*0.5f, 1.1f});
		m.parts.push_back({sim::PartType::CILIA,    { 20.0f, -20.0f},  0.0f,    1.0f});
		m.parts.push_back({sim::PartType::CILIA,    {-20.0f, -20.0f},  PI,      1.0f});
		m.parts.push_back({sim::PartType::MOUTH,    {  0.0f,  55.0f},  PI*0.5f, 1.0f});
		break;
	}
	case StarterKind::TOUGH: {
		m.color = glm::vec3(0.70f, 0.70f, 0.78f);
		m.initial_biomass = 45.0f;
		m.cell = wobble_circle(60.0f, 0.05f, 6);
		m.parts.push_back({sim::PartType::ARMOR, { 32.0f,   8.0f}, 0.0f,    1.3f});
		m.parts.push_back({sim::PartType::ARMOR, {-32.0f,   8.0f}, PI,      1.3f});
		m.parts.push_back({sim::PartType::ARMOR, {  0.0f,  30.0f}, PI*0.5f, 1.1f});
		m.parts.push_back({sim::PartType::REGEN, { 14.0f,  -8.0f}, 0.0f,    1.2f});
		m.parts.push_back({sim::PartType::REGEN, {-14.0f,  -8.0f}, PI,      1.2f});
		m.parts.push_back({sim::PartType::MOUTH, {  0.0f,  38.0f}, PI*0.5f, 1.0f});
		break;
	}
	case StarterKind::PLAIN: {
		m.color = glm::vec3(0.92f, 0.88f, 0.78f);
		m.cell = wobble_circle(45.0f, 0.0f, 1);
		m.parts.push_back({sim::PartType::MOUTH, {  0.0f,  27.0f}, PI*0.5f, 1.0f});
		break;
	}
	case StarterKind::RANDOM: {
		// Random color (pastel) + 3-5 random parts at sensible anchors.
		std::uniform_real_distribution<float> col_d(0.45f, 0.95f);
		m.color = glm::vec3(col_d(rng), col_d(rng), col_d(rng));
		m.cell = wobble_circle(50.0f, 0.05f, 3 + (int)(rng() % 4));
		std::uniform_int_distribution<int> nparts(3, 5);
		std::uniform_int_distribution<int> pt_d(0, (int)sim::PartType::PART_TYPE_COUNT - 1);
		std::uniform_real_distribution<float> ang_d(0.0f, starter_detail::PI * 2.0f);
		std::uniform_real_distribution<float> sc_d(0.9f, 1.3f);
		int n = nparts(rng);
		for (int i = 0; i < n; ++i) {
			sim::PartType t = (sim::PartType)pt_d(rng);
			float a = ang_d(rng);
			float r = 35.0f + (rng() % 20);
			glm::vec2 pos(std::cos(a) * r, std::sin(a) * r);
			// Mirror across y so it's symmetric.
			m.parts.push_back({t, pos, std::atan2(pos.y, pos.x), sc_d(rng)});
			m.parts.push_back({t, glm::vec2(-pos.x, pos.y), starter_detail::PI - std::atan2(pos.y, pos.x), sc_d(rng)});
		}
		// Guarantee a MOUTH so a random starter can always eat.
		m.parts.push_back({sim::PartType::MOUTH, {0.0f, 30.0f}, starter_detail::PI * 0.5f, 1.0f});
		break;
	}
	default: break;
	}
	return m;
}

} // namespace civcraft::cellcraft::monsters
