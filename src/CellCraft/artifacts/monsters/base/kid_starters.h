// CellCraft — KID-MODE starter creature templates.
//
// Six big, readable starters for the kid picker screen. Each one uses
// the original part names (SPIKE, TEETH, FLAGELLA, ...) so kids learn
// the real vocabulary. RANDOM and PLAIN are special-cased by the caller.

#pragma once

#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/artifacts/monsters/base/prebuilt.h"
#include "CellCraft/sim/part.h"
#include "CellCraft/sim/radial_cell.h"

namespace civcraft::cellcraft::monsters {

enum class KidStarterKind {
	SPIKY = 0,
	SQUISHY,
	ZOOMY,
	TOUGH,
	RANDOM,
	PLAIN,
	KIND_COUNT,
};

inline const char* kidStarterName(KidStarterKind k) {
	switch (k) {
	case KidStarterKind::SPIKY:   return "SPIKY";
	case KidStarterKind::SQUISHY: return "SQUISHY";
	case KidStarterKind::ZOOMY:   return "ZOOMY";
	case KidStarterKind::TOUGH:   return "TOUGH";
	case KidStarterKind::RANDOM:  return "RANDOM";
	case KidStarterKind::PLAIN:   return "PLAIN";
	default: return "?";
	}
}

inline const char* kidStarterTagline(KidStarterKind k) {
	switch (k) {
	case KidStarterKind::SPIKY:   return "POKES THINGS";
	case KidStarterKind::SQUISHY: return "POISON BLOB";
	case KidStarterKind::ZOOMY:   return "GO FAST";
	case KidStarterKind::TOUGH:   return "HARD TO HURT";
	case KidStarterKind::RANDOM:  return "MYSTERY!";
	case KidStarterKind::PLAIN:   return "MAKE YOUR OWN";
	default: return "";
	}
}

namespace kid_detail {
inline const float PI = 3.14159265359f;
} // namespace kid_detail

inline MonsterTemplate makeKidStarter(KidStarterKind k, std::mt19937& rng) {
	using namespace detail; // elongated, wobble_circle from prebuilt.h
	using kid_detail::PI;
	MonsterTemplate m;
	m.id = std::string("kid:") + kidStarterName(k);
	m.name = kidStarterName(k);
	m.initial_biomass = 25.0f;
	switch (k) {
	case KidStarterKind::SPIKY: {
		m.color = glm::vec3(0.95f, 0.35f, 0.40f);
		m.cell  = elongated(70.0f, 70.0f, 22.0f);
		// 4 SPIKES + 2 VENOM SPIKES — symmetric pairs.
		m.parts.push_back({sim::PartType::SPIKE,       { 12.0f,  56.0f},  PI*0.5f, 1.2f});
		m.parts.push_back({sim::PartType::SPIKE,       {-12.0f,  56.0f},  PI*0.5f, 1.2f});
		m.parts.push_back({sim::PartType::SPIKE,       { 18.0f,  20.0f},  0.20f,   1.0f});
		m.parts.push_back({sim::PartType::SPIKE,       {-18.0f,  20.0f},  PI-0.20f,1.0f});
		m.parts.push_back({sim::PartType::VENOM_SPIKE, {  6.0f,  64.0f},  PI*0.5f, 1.0f});
		m.parts.push_back({sim::PartType::VENOM_SPIKE, { -6.0f,  64.0f},  PI*0.5f, 1.0f});
		break;
	}
	case KidStarterKind::SQUISHY: {
		m.color = glm::vec3(0.55f, 0.85f, 0.55f);
		m.initial_biomass = 35.0f;
		m.cell = wobble_circle(55.0f, 0.06f, 4);
		m.parts.push_back({sim::PartType::POISON, {   0.0f,   0.0f}, 0.0f,    1.4f});
		m.parts.push_back({sim::PartType::ARMOR,  {  28.0f,   0.0f}, 0.0f,    1.2f});
		m.parts.push_back({sim::PartType::ARMOR,  { -28.0f,   0.0f}, PI,      1.2f});
		break;
	}
	case KidStarterKind::ZOOMY: {
		m.color = glm::vec3(0.45f, 0.65f, 1.00f);
		m.cell = elongated(85.0f, 75.0f, 18.0f);
		m.parts.push_back({sim::PartType::FLAGELLA, {  8.0f, -65.0f}, -PI*0.5f, 1.1f});
		m.parts.push_back({sim::PartType::FLAGELLA, { -8.0f, -65.0f}, -PI*0.5f, 1.1f});
		m.parts.push_back({sim::PartType::CILIA,    { 20.0f, -20.0f},  0.0f,    1.0f});
		m.parts.push_back({sim::PartType::CILIA,    {-20.0f, -20.0f},  PI,      1.0f});
		break;
	}
	case KidStarterKind::TOUGH: {
		m.color = glm::vec3(0.70f, 0.70f, 0.78f);
		m.initial_biomass = 45.0f;
		m.cell = wobble_circle(60.0f, 0.05f, 6);
		m.parts.push_back({sim::PartType::ARMOR, { 32.0f,   8.0f}, 0.0f,    1.3f});
		m.parts.push_back({sim::PartType::ARMOR, {-32.0f,   8.0f}, PI,      1.3f});
		m.parts.push_back({sim::PartType::ARMOR, {  0.0f,  30.0f}, PI*0.5f, 1.1f});
		m.parts.push_back({sim::PartType::REGEN, { 14.0f,  -8.0f}, 0.0f,    1.2f});
		m.parts.push_back({sim::PartType::REGEN, {-14.0f,  -8.0f}, PI,      1.2f});
		break;
	}
	case KidStarterKind::PLAIN: {
		m.color = glm::vec3(0.92f, 0.88f, 0.78f);
		m.cell = wobble_circle(45.0f, 0.0f, 1);
		break;
	}
	case KidStarterKind::RANDOM: {
		// Random color (pastel) + 3-5 random parts at sensible anchors.
		std::uniform_real_distribution<float> col_d(0.45f, 0.95f);
		m.color = glm::vec3(col_d(rng), col_d(rng), col_d(rng));
		m.cell = wobble_circle(50.0f, 0.05f, 3 + (int)(rng() % 4));
		std::uniform_int_distribution<int> nparts(3, 5);
		std::uniform_int_distribution<int> pt_d(0, (int)sim::PartType::PART_TYPE_COUNT - 1);
		std::uniform_real_distribution<float> ang_d(0.0f, kid_detail::PI * 2.0f);
		std::uniform_real_distribution<float> sc_d(0.9f, 1.3f);
		int n = nparts(rng);
		for (int i = 0; i < n; ++i) {
			sim::PartType t = (sim::PartType)pt_d(rng);
			float a = ang_d(rng);
			float r = 35.0f + (rng() % 20);
			glm::vec2 pos(std::cos(a) * r, std::sin(a) * r);
			// Mirror across y so it's symmetric.
			m.parts.push_back({t, pos, std::atan2(pos.y, pos.x), sc_d(rng)});
			m.parts.push_back({t, glm::vec2(-pos.x, pos.y), kid_detail::PI - std::atan2(pos.y, pos.x), sc_d(rng)});
		}
		break;
	}
	default: break;
	}
	return m;
}

} // namespace civcraft::cellcraft::monsters
