// LifeCraft — aggregate a list of Parts into a PartEffect the sim can
// consume quickly. Each part's gameplay numbers live here so a modder
// only has one place to tune.

#pragma once

#include <algorithm>
#include <cmath>

#include "LifeCraft/sim/part.h"

namespace civcraft::lifecraft::sim {

// Stack caps (design note: the game prompt asks for FLAGELLA×3 and ARMOR×2).
constexpr int   PART_FLAGELLA_MAX_STACK = 3;
constexpr int   PART_ARMOR_MAX_STACK    = 2;
constexpr float PART_FLAGELLA_SPEED_ADD = 0.15f;
constexpr float PART_ARMOR_HP_ADD       = 0.50f;
constexpr float PART_ARMOR_DR_PER       = 0.15f;
constexpr float PART_SPIKE_DMG_ADD      = 1.00f; // stacks additively in bonus zone
constexpr float PART_TEETH_DMG_ADD      = 0.50f;
constexpr float PART_POISON_DPS         = 1.0f;
constexpr float PART_POISON_RADIUS      = 60.0f;

// Biomass costs (match the prompt's table).
constexpr float PART_COST_SPIKE    = 5.0f;
constexpr float PART_COST_TEETH    = 5.0f;
constexpr float PART_COST_FLAGELLA = 4.0f;
constexpr float PART_COST_POISON   = 8.0f;
constexpr float PART_COST_ARMOR    = 6.0f;

inline float part_cost(PartType t) {
	switch (t) {
	case PartType::SPIKE:    return PART_COST_SPIKE;
	case PartType::TEETH:    return PART_COST_TEETH;
	case PartType::FLAGELLA: return PART_COST_FLAGELLA;
	case PartType::POISON:   return PART_COST_POISON;
	case PartType::ARMOR:    return PART_COST_ARMOR;
	}
	return 0.0f;
}

inline const char* part_name(PartType t) {
	switch (t) {
	case PartType::SPIKE:    return "SPIKE";
	case PartType::TEETH:    return "TEETH";
	case PartType::FLAGELLA: return "FLAGELLA";
	case PartType::POISON:   return "POISON";
	case PartType::ARMOR:    return "ARMOR";
	}
	return "?";
}

inline PartEffect computePartEffects(const std::vector<Part>& parts) {
	PartEffect e;
	int flagella = 0, armor = 0;
	bool have_poison = false;
	for (const auto& p : parts) {
		switch (p.type) {
		case PartType::FLAGELLA: {
			if (flagella < PART_FLAGELLA_MAX_STACK) {
				e.speed_mult += PART_FLAGELLA_SPEED_ADD;
				++flagella;
			}
			break;
		}
		case PartType::ARMOR: {
			if (armor < PART_ARMOR_MAX_STACK) {
				e.hp_mult += PART_ARMOR_HP_ADD;
				e.armor_dr = std::min(0.6f, e.armor_dr + PART_ARMOR_DR_PER);
				++armor;
			}
			e.armor_anchors_local.push_back(p.anchor_local);
			break;
		}
		case PartType::SPIKE: {
			// Spikes radiate outward from core: use the anchor direction
			// as the spike direction (normalized).
			glm::vec2 d = p.anchor_local;
			float len = std::sqrt(d.x * d.x + d.y * d.y);
			if (len > 1e-3f) d /= len;
			else             d = glm::vec2(std::cos(p.orientation), std::sin(p.orientation));
			e.spike_dirs.push_back(d);
			break;
		}
		case PartType::TEETH: {
			e.teeth_anchors_local.push_back(p.anchor_local);
			break;
		}
		case PartType::POISON: {
			have_poison = true;
			break;
		}
		}
	}
	if (have_poison) {
		// One poison part gives a cloud; more parts stack dps linearly.
		int poison_count = 0;
		for (const auto& p : parts) if (p.type == PartType::POISON) ++poison_count;
		e.poison_dps = PART_POISON_DPS * (float)poison_count;
		e.poison_radius = PART_POISON_RADIUS;
	}
	return e;
}

} // namespace civcraft::lifecraft::sim
