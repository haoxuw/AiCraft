// LifeCraft — Parts. Spore-cell-stage style modular bits attached to
// a monster body. Each part has a local-space anchor (in the monster's
// own coordinate frame) + gameplay effects aggregated in PartEffect.
//
// Rule 1 compliance: costs and numbers live in part_stats.h alongside
// the aggregation logic — easy target for a future Python artifact mod.

#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace civcraft::lifecraft::sim {

enum class PartType : uint8_t {
	SPIKE    = 0,  // +damage along outward direction from core
	TEETH    = 1,  // +contact damage, doubles pointiness at nearest vertex
	FLAGELLA = 2,  // +speed (stacks)
	POISON   = 3,  // aura dps to nearby hostiles
	ARMOR    = 4,  // +hp, local damage reduction near anchor
};

struct Part {
	PartType  type = PartType::SPIKE;
	glm::vec2 anchor_local = glm::vec2(0.0f);
	float     orientation  = 0.0f; // radians, for visual rotation (outward direction)
};

// Aggregated stat block computed from a monster's parts. Cached on
// refresh_stats(); re-read by sim.cpp each contact resolution.
struct PartEffect {
	float damage_mult  = 1.0f;
	float speed_mult   = 1.0f;
	float hp_mult      = 1.0f;
	float poison_dps   = 0.0f;
	float poison_radius = 0.0f;
	float armor_dr     = 0.0f; // global damage reduction 0..1

	// For per-contact bonuses we need location data too.
	std::vector<glm::vec2> spike_dirs;         // unit vectors in local space
	std::vector<glm::vec2> teeth_anchors_local; // local-space positions
	std::vector<glm::vec2> armor_anchors_local;
};

} // namespace civcraft::lifecraft::sim
