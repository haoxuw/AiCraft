#pragma once

// EvolveCraft Creature — the only entity kind that matters.
//
// Designed per docs/DESIGN.md §6: Creature has DNA traits, a (future) BodyPlan,
// energy, age, and a SpeciesId. No inventory. Players don't inhabit a creature;
// they own a species that controls a swarm of them.
//
// Stages 1–3 use a flat DNA of numeric traits. Stage 7 introduces BodyPlan.

#include "shared/body_plan.h"

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace evolvecraft {

using CreatureId = uint32_t;
using SpeciesId  = uint16_t;
using OwnerId    = uint16_t;  // which player owns the species (0 = NPC / environment)

// Built-in species IDs (stage 5).
namespace Species {
	constexpr SpeciesId Wanderer = 1;
	constexpr SpeciesId Predator = 2;
	constexpr SpeciesId Prey     = 3;
}

// Per-creature DNA: numeric traits only for early stages.
struct DNA {
	float size          = 1.0f;   // torso scale multiplier
	float baseSpeed     = 3.0f;   // m/s
	float baseSense     = 16.0f;  // perception radius
	float metabolism    = 0.4f;   // energy burned per second
	float splitThresh   = 10.0f;  // energy required to split
	float aggression    = 0.4f;   // behavior tuning knob
	float caution       = 0.6f;   // behavior tuning knob
	float colorHue      = 0.6f;   // 0..1 mapped to HSV
};

struct Creature {
	CreatureId id = 0;
	SpeciesId  species = Species::Wanderer;
	OwnerId    owner = 0;

	// World
	glm::vec3 pos     = {0, 0, 0};
	glm::vec3 vel     = {0, 0, 0};
	glm::vec3 goal    = {0, 0, 0};   // current AI target (for debug display)
	float     yaw     = 0.0f;        // facing direction (radians)
	float     bobPhase = 0.0f;       // animation phase for visual bobbing

	// Stats
	int   hp      = 10;
	int   maxHp   = 10;
	float energy  = 5.0f;            // food reserve
	float age     = 0.0f;

	DNA dna;
	BodyPlan body;

	// Flags
	bool alive = true;

	// Cached stats (recomputed from DNA/BodyPlan on spawn or mutation)
	float effectiveSpeed = 3.0f;
	float effectiveSense = 16.0f;
};

} // namespace evolvecraft
