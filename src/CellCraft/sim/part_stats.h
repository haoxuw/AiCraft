// CellCraft — aggregate a list of Parts into a PartEffect the sim can
// consume quickly. Each part's gameplay numbers live here so a modder
// only has one place to tune.

#pragma once

#include <algorithm>
#include <cmath>

#include "CellCraft/sim/part.h"

namespace civcraft::cellcraft::sim {

// Stack caps (design note: the game prompt asks for FLAGELLA×3 and ARMOR×2).
constexpr int   PART_FLAGELLA_MAX_STACK = 3;
constexpr int   PART_ARMOR_MAX_STACK    = 2;
constexpr int   PART_CILIA_MAX_STACK    = 3;
constexpr int   PART_REGEN_MAX_STACK    = 3;
constexpr int   PART_MOUTH_MAX_STACK    = 2;
constexpr int   PART_HORN_MAX_STACK     = 1;
constexpr int   PART_VENOM_MAX_STACK    = 2;
constexpr int   PART_EYES_MAX_STACK     = 2;

constexpr float PART_FLAGELLA_SPEED_ADD = 0.15f;
constexpr float PART_ARMOR_HP_ADD       = 0.50f;
constexpr float PART_ARMOR_DR_PER       = 0.15f;
constexpr float PART_SPIKE_DMG_ADD      = 1.00f; // stacks additively in bonus zone
constexpr float PART_TEETH_DMG_ADD      = 0.50f;
constexpr float PART_POISON_DPS         = 1.0f;
constexpr float PART_POISON_RADIUS      = 60.0f;
constexpr float PART_CILIA_TURN_ADD     = 0.20f;
constexpr float PART_HORN_DMG_MULT      = 3.0f;
constexpr float PART_HORN_DMG_SIDE      = 0.15f; // away from cone
constexpr float PART_HORN_CONE_COS      = 0.9659258f; // cos(15°)
constexpr float PART_REGEN_HPS          = 1.5f;
constexpr float PART_MOUTH_RADIUS_ADD   = 0.50f; // +50% per mouth
constexpr float PART_VENOM_DPS          = 2.0f;
constexpr float PART_VENOM_DURATION     = 3.0f;
constexpr float PART_EYES_PERCEPTION_ADD = 0.50f; // +50% per EYES stack

// Biomass costs (match the prompt's table).
constexpr float PART_COST_SPIKE       = 5.0f;
constexpr float PART_COST_TEETH       = 5.0f;
constexpr float PART_COST_FLAGELLA    = 4.0f;
constexpr float PART_COST_POISON      = 8.0f;
constexpr float PART_COST_ARMOR       = 6.0f;
constexpr float PART_COST_CILIA       = 4.0f;
constexpr float PART_COST_HORN        = 10.0f;
constexpr float PART_COST_REGEN       = 6.0f;
constexpr float PART_COST_MOUTH       = 4.0f;
constexpr float PART_COST_VENOM_SPIKE = 7.0f;
constexpr float PART_COST_EYES        = 5.0f;

inline float part_cost(PartType t) {
	switch (t) {
	case PartType::SPIKE:       return PART_COST_SPIKE;
	case PartType::TEETH:       return PART_COST_TEETH;
	case PartType::FLAGELLA:    return PART_COST_FLAGELLA;
	case PartType::POISON:      return PART_COST_POISON;
	case PartType::ARMOR:       return PART_COST_ARMOR;
	case PartType::CILIA:       return PART_COST_CILIA;
	case PartType::HORN:        return PART_COST_HORN;
	case PartType::REGEN:       return PART_COST_REGEN;
	case PartType::MOUTH:       return PART_COST_MOUTH;
	case PartType::VENOM_SPIKE: return PART_COST_VENOM_SPIKE;
	case PartType::EYES:        return PART_COST_EYES;
	case PartType::PART_TYPE_COUNT: break;
	}
	return 0.0f;
}

inline const char* part_name(PartType t) {
	switch (t) {
	case PartType::SPIKE:       return "SPIKE";
	case PartType::TEETH:       return "TEETH";
	case PartType::FLAGELLA:    return "FLAGELLA";
	case PartType::POISON:      return "POISON";
	case PartType::ARMOR:       return "ARMOR";
	case PartType::CILIA:       return "CILIA";
	case PartType::HORN:        return "HORN";
	case PartType::REGEN:       return "REGEN";
	case PartType::MOUTH:       return "MOUTH";
	case PartType::VENOM_SPIKE: return "VENOM";
	case PartType::EYES:        return "EYES";
	case PartType::PART_TYPE_COUNT: break;
	}
	return "?";
}

inline const char* part_desc(PartType t) {
	switch (t) {
	case PartType::SPIKE:       return "+dmg in spike cone";
	case PartType::TEETH:       return "+50% bite damage";
	case PartType::FLAGELLA:    return "+15% speed (x3)";
	case PartType::POISON:      return "aura DoT nearby";
	case PartType::ARMOR:       return "+50% HP, DR (x2)";
	case PartType::CILIA:       return "+20% turn (x3)";
	case PartType::HORN:        return "3x dmg in front cone";
	case PartType::REGEN:       return "+1.5 hp/s (x3)";
	case PartType::MOUTH:       return "+50% pickup radius (x2)";
	case PartType::VENOM_SPIKE: return "bite applies venom DoT";
	case PartType::EYES:        return "+50% perception (x2)";
	case PartType::PART_TYPE_COUNT: break;
	}
	return "";
}

// Effects scale with Part::scale per the prompt's design. Stack caps
// still apply; a scaled part still only counts as one stack.
inline PartEffect computePartEffects(const std::vector<Part>& parts) {
	PartEffect e;
	int flagella = 0, armor = 0, cilia = 0, regen = 0, mouth = 0, venom = 0, horn = 0, eyes = 0;
	bool have_poison = false;
	float poison_scale_sum = 0.0f;
	float poison_radius_max = 0.0f;

	// Diet signal per part. Carnivore-leaning parts add +scale, herbivore
	// -leaning subtract. MOUTH is neutral (every eater has one, so it
	// must not bias the score). FLAGELLA/EYES are neutral. Final
	// classification uses ±0.8 thresholds → CARNIVORE / HERBIVORE / OMNIVORE.
	float diet_accum = 0.0f;
	for (const auto& p : parts) {
		const float s = std::max(0.1f, p.scale);
		switch (p.type) {
		case PartType::TEETH:
		case PartType::SPIKE:
		case PartType::VENOM_SPIKE:
		case PartType::HORN:
		case PartType::POISON:
			diet_accum += s; break;
		case PartType::REGEN:
		case PartType::ARMOR:
		case PartType::CILIA:
			diet_accum -= s; break;
		case PartType::MOUTH:
		case PartType::FLAGELLA:
		case PartType::EYES:
		case PartType::PART_TYPE_COUNT:
			break;
		}
	}
	e.diet_score = diet_accum;
	if      (diet_accum >  0.8f) e.diet = Diet::CARNIVORE;
	else if (diet_accum < -0.8f) e.diet = Diet::HERBIVORE;
	else                          e.diet = Diet::OMNIVORE;

	for (const auto& p : parts) {
		const float s = std::max(0.1f, p.scale);
		switch (p.type) {
		case PartType::FLAGELLA: {
			if (flagella < PART_FLAGELLA_MAX_STACK) {
				e.speed_mult += PART_FLAGELLA_SPEED_ADD * s;
				++flagella;
			}
			break;
		}
		case PartType::ARMOR: {
			if (armor < PART_ARMOR_MAX_STACK) {
				e.hp_mult += PART_ARMOR_HP_ADD * s;
				e.armor_dr = std::min(0.8f, e.armor_dr + PART_ARMOR_DR_PER * s);
				++armor;
			}
			e.armor_anchors_local.push_back(p.anchor_local);
			break;
		}
		case PartType::SPIKE: {
			glm::vec2 d = p.anchor_local;
			float len = std::sqrt(d.x * d.x + d.y * d.y);
			if (len > 1e-3f) d /= len;
			else             d = glm::vec2(std::cos(p.orientation), std::sin(p.orientation));
			// scale bakes into damage_mult here; sim.cpp reads spike_dirs for cone.
			e.damage_mult += (PART_SPIKE_DMG_ADD * s - PART_SPIKE_DMG_ADD);
			e.spike_dirs.push_back(d);
			break;
		}
		case PartType::TEETH: {
			e.damage_mult += PART_TEETH_DMG_ADD * (s - 1.0f);
			e.teeth_anchors_local.push_back(p.anchor_local);
			break;
		}
		case PartType::POISON: {
			have_poison = true;
			poison_scale_sum += s;
			poison_radius_max = std::max(poison_radius_max, PART_POISON_RADIUS * s);
			break;
		}
		case PartType::CILIA: {
			if (cilia < PART_CILIA_MAX_STACK) {
				e.turn_mult += PART_CILIA_TURN_ADD * s;
				++cilia;
			}
			break;
		}
		case PartType::HORN: {
			if (horn < PART_HORN_MAX_STACK) {
				glm::vec2 d = p.anchor_local;
				float len = std::sqrt(d.x * d.x + d.y * d.y);
				if (len > 1e-3f) d /= len;
				else             d = glm::vec2(1.0f, 0.0f);
				// horn damage scaling: sim.cpp uses PART_HORN_DMG_MULT directly;
				// keep parity by boosting damage_mult proportional to scale.
				e.damage_mult += (PART_HORN_DMG_MULT * 0.25f) * (s - 1.0f);
				e.horn_dirs.push_back(d);
				++horn;
			}
			break;
		}
		case PartType::REGEN: {
			if (regen < PART_REGEN_MAX_STACK) {
				e.regen_hps += PART_REGEN_HPS * s;
				++regen;
			}
			break;
		}
		case PartType::MOUTH: {
			e.has_mouth = true;
			if (mouth < PART_MOUTH_MAX_STACK) {
				e.pickup_radius_mult += PART_MOUTH_RADIUS_ADD * s;
				++mouth;
			}
			break;
		}
		case PartType::VENOM_SPIKE: {
			if (venom < PART_VENOM_MAX_STACK) {
				++e.venom_stacks;
				++venom;
			}
			break;
		}
		case PartType::EYES: {
			if (eyes < PART_EYES_MAX_STACK) {
				e.perception_mult += PART_EYES_PERCEPTION_ADD * s;
				++eyes;
			}
			break;
		}
		case PartType::PART_TYPE_COUNT: break;
		}
	}
	if (have_poison) {
		e.poison_dps = PART_POISON_DPS * poison_scale_sum;
		e.poison_radius = poison_radius_max;
	}
	return e;
}

} // namespace civcraft::cellcraft::sim
