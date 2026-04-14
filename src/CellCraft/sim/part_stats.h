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

inline PartEffect computePartEffects(const std::vector<Part>& parts) {
	PartEffect e;
	int flagella = 0, armor = 0, cilia = 0, regen = 0, mouth = 0, venom = 0, horn = 0, eyes = 0;
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
		case PartType::CILIA: {
			if (cilia < PART_CILIA_MAX_STACK) {
				e.turn_mult += PART_CILIA_TURN_ADD;
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
				e.horn_dirs.push_back(d);
				++horn;
			}
			break;
		}
		case PartType::REGEN: {
			if (regen < PART_REGEN_MAX_STACK) {
				e.regen_hps += PART_REGEN_HPS;
				++regen;
			}
			break;
		}
		case PartType::MOUTH: {
			if (mouth < PART_MOUTH_MAX_STACK) {
				e.pickup_radius_mult += PART_MOUTH_RADIUS_ADD;
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
				e.perception_mult += PART_EYES_PERCEPTION_ADD;
				++eyes;
			}
			break;
		}
		case PartType::PART_TYPE_COUNT: break;
		}
	}
	if (have_poison) {
		int poison_count = 0;
		for (const auto& p : parts) if (p.type == PartType::POISON) ++poison_count;
		e.poison_dps = PART_POISON_DPS * (float)poison_count;
		e.poison_radius = PART_POISON_RADIUS;
	}
	return e;
}

} // namespace civcraft::cellcraft::sim
