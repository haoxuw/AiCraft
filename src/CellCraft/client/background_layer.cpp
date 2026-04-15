// CellCraft — BackgroundLayer impl. See header for design notes.
//
// Population rules (commit 6 spec):
//   tier 1 → 12 × CELL @ 4× baseline, all "near"
//   tier 2 → 12 × CELL @ 8× baseline (mix near 8×/far 16×)
//   tier 3 → 12 × CELL @ 16× baseline (mix)
//   tier 4 → 12 × CELL @ 32× baseline (mix)
//   tier 5 → 12 × {FISH, TURTLE, JELLY} @ 32× baseline
//
// Baseline silhouette unit ≈ 28 px cell radius in world units (we treat 1
// world unit = 1 px of scale). At 32× this yields ~900 px-ish silhouettes —
// comfortably "looming" on a 1280×800 canvas.

#include "CellCraft/client/background_layer.h"

#include <cmath>

namespace civcraft::cellcraft {

static float frand(std::mt19937& rng, float lo, float hi) {
	std::uniform_real_distribution<float> d(lo, hi);
	return d(rng);
}

void BackgroundLayer::init(unsigned seed) {
	rng_.seed(seed);
	pool_.clear();
	current_tier_ = -1;
}

BgCreature BackgroundLayer::spawn_(SilhouetteType t, float map_radius,
                                   float scale, int layer) {
	BgCreature c;
	c.type    = t;
	c.layer   = layer;
	c.scale   = scale * (layer == 0 ? 1.0f : 1.35f);  // far layer is bigger (perspective)
	c.pos     = glm::vec2(frand(rng_, -map_radius, map_radius),
	                      frand(rng_, -map_radius, map_radius));
	// Slow drift 5–15 world units/sec in a random direction.
	float spd = frand(rng_, 5.0f, 15.0f);
	float ang = frand(rng_, 0.0f, 6.28318530718f);
	c.vel     = glm::vec2(std::cos(ang), std::sin(ang)) * spd;
	c.rot     = frand(rng_, 0.0f, 6.28318530718f);
	c.rot_vel = frand(rng_, -0.10f, 0.10f);
	c.wobble_ph = frand(rng_, 0.0f, 6.28318530718f);
	return c;
}

void BackgroundLayer::repopulate_(int tier, float map_radius) {
	pool_.clear();
	// Restrict spawn range to a viewport-ish radius. With only 5-7 creatures,
	// uniform over the full 1500-unit map_radius leaves most off-screen.
	// They'll still drift + wrap through the full map over time.
	(void)map_radius;
	const float SPAWN_R = 900.0f;

	if (tier <= 4) {
		// CELL silhouettes. Scale = 4× * 2^(tier-1): 4, 8, 16, 32.
		// Keep count modest so the board still breathes — 7 cells.
		const int TOTAL = 7;
		float base = 4.0f * std::pow(2.0f, float(std::max(1, tier) - 1));
		for (int i = 0; i < TOTAL; ++i) {
			// For tiers 2+, split into a near-common and far-rare layer.
			int layer = 0;
			float scale = base;
			if (tier >= 2) {
				bool far_layer = (i % 3 == 0);  // 1/3 far, 2/3 near
				layer = far_layer ? 1 : 0;
				scale = far_layer ? base * 1.5f : base * 0.9f;
			}
			pool_.push_back(spawn_(SilhouetteType::CELL_GENERIC, SPAWN_R,
				scale, layer));
		}
	} else {
		// APEX (tier 5): sparse animal menagerie — like real fauna in an
		// enormous aquarium, they should feel rare and looming, not crowded.
		// Three archetypes, placed deterministically so the scene always reads
		// as "big fish off to one side, turtle in a far corner, jelly trailing
		// down". Random drift carries them around from there.
		//
		// At 1 world-unit = 1 screen-px on a 1632×1020 canvas, a base fish
		// silhouette is ~80×36 units. 32× turned every creature into a mural
		// wider than the window — only edge-fragments were visible, reading
		// as abstract scribbles. 6× → ~500px silhouettes, looming but whole.
		struct Spawn { SilhouetteType t; glm::vec2 pos; int layer; float s; };
		const Spawn spawns[3] = {
			{SilhouetteType::FISH,   glm::vec2(-300.0f,  120.0f), 0, 6.0f},
			{SilhouetteType::TURTLE, glm::vec2( 520.0f, -320.0f), 1, 7.2f},
			{SilhouetteType::JELLY,  glm::vec2( 200.0f,  380.0f), 0, 6.0f},
		};
		for (const auto& s : spawns) {
			BgCreature c = spawn_(s.t, SPAWN_R, s.s, s.layer);
			c.pos = s.pos;  // override random position
			// Gentle, readable orientations: fish roughly horizontal,
			// turtle slightly tilted, jelly upright (rot=0, top-up).
			if (s.t == SilhouetteType::FISH)   c.rot = frand(rng_, -0.3f, 0.3f);
			if (s.t == SilhouetteType::TURTLE) c.rot = frand(rng_, -0.6f, 0.6f);
			if (s.t == SilhouetteType::JELLY)  c.rot = 0.0f;
			c.rot_vel = frand(rng_, -0.05f, 0.05f);  // slower wobble
			pool_.push_back(c);
		}
	}
}

void BackgroundLayer::setPlayerTier(int player_tier, float map_radius) {
	if (player_tier == current_tier_) return;
	current_tier_ = player_tier;
	repopulate_(player_tier, map_radius);
}

void BackgroundLayer::update(float dt, float map_radius) {
	const float R = map_radius * 1.2f;  // slightly outside the arena so wraps are soft
	for (auto& c : pool_) {
		c.pos += c.vel * dt;
		c.rot += c.rot_vel * dt;
		// Wrap around — teleport to opposite side when we exit.
		if (c.pos.x >  R) c.pos.x -= 2.0f * R;
		if (c.pos.x < -R) c.pos.x += 2.0f * R;
		if (c.pos.y >  R) c.pos.y -= 2.0f * R;
		if (c.pos.y < -R) c.pos.y += 2.0f * R;
	}
}

} // namespace civcraft::cellcraft
