#pragma once

// Default body plans for the three stage-5 species.
//
// Each species gets a distinctive silhouette so it's instantly identifiable
// in the swarm. These are the "founder" plans — mutation can drift them.

#include "shared/body_plan.h"
#include "shared/creature.h"

#include <cmath>

namespace evolvecraft {

inline BodyPlan presetWandererBody(const DNA& d) {
	BodyPlan b;
	b.torsoColor = {0.70f + 0.2f * d.colorHue, 0.85f, 0.95f};
	b.torsoScale = {0.9f, 0.8f, 1.05f};
	b.addPart(parts::flagellum({0.85f, 0.92f, 1.0f}));
	b.addPart(parts::eye({0.15f, 0.70f, 1.0f}));
	return b;
}

inline BodyPlan presetPreyBody(const DNA& d) {
	BodyPlan b;
	b.torsoColor = {0.35f, 0.80f, 0.45f};
	b.torsoScale = {0.85f, 0.70f, 1.15f};
	b.addPart(parts::tail({0.55f, 0.90f, 0.55f}));
	b.addPart(parts::dorsalFin({0.50f, 0.95f, 0.50f}));
	b.addPart(parts::eye({0.95f, 0.95f, 0.10f}));
	return b;
}

inline BodyPlan presetPredatorBody(const DNA& d) {
	BodyPlan b;
	b.torsoColor = {0.85f, 0.22f, 0.18f};
	b.torsoScale = {1.05f, 0.85f, 1.35f};
	b.addPart(parts::jaw({1.0f, 0.30f, 0.20f}));
	b.addPart(parts::flagellum({0.95f, 0.55f, 0.30f}));
	b.addPart(parts::spikeLeft({1.0f, 0.85f, 0.55f}));
	b.addPart(parts::spikeRight({1.0f, 0.85f, 0.55f}));
	b.addPart(parts::eye({1.0f, 0.95f, 0.70f}));
	return b;
}

inline BodyPlan presetBodyFor(SpeciesId sp, const DNA& d) {
	switch (sp) {
	case Species::Wanderer: return presetWandererBody(d);
	case Species::Prey:     return presetPreyBody(d);
	case Species::Predator: return presetPredatorBody(d);
	}
	return presetWandererBody(d);
}

} // namespace evolvecraft
