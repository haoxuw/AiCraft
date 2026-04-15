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
	// Restrict spawn range to a viewport-ish radius. With only a handful of
	// creatures, uniform over the full 1500-unit map_radius leaves most
	// off-screen. They'll still drift + wrap through the full map over time.
	(void)map_radius;
	const float SPAWN_R = 900.0f;

	// Depth-layer preset: three planes of depth with parallax, stroke width,
	// and cream-tint mix tuned so that NEAR creatures read as crisp, MID
	// recedes a step, and FAR feels like looming shadow behind a frosted
	// pane. Fake-blur (stacked ribbons + wide half-width) replaces a real
	// FBO blur pass. Keyed by layer index 0/1/2.
	struct LayerStyle {
		float parallax;
		float stroke_width;
		float tint_mix;
	};
	static const LayerStyle STYLE[3] = {
		{1.00f,  4.0f, 0.00f},  // 0 NEAR:  world-locked, crisp tan outline
		{0.65f,  9.0f, 0.30f},  // 1 MID:   2/3 parallax, softer + paler
		{0.35f, 16.0f, 0.55f},  // 2 FAR:   looming, heavily blurred-out
	};
	auto push = [&](SilhouetteType t, float scale, int layer) {
		BgCreature c = spawn_(t, SPAWN_R, scale, layer);
		c.parallax     = STYLE[layer].parallax;
		c.stroke_width = STYLE[layer].stroke_width;
		c.tint_mix     = STYLE[layer].tint_mix;
		pool_.push_back(c);
	};

	if (tier <= 4) {
		// CELL silhouettes in three depth planes.
		// NEAR: next-tier creatures (base = 4× * 2^(tier-1)).
		// MID:  two-tiers-ahead (2× base).
		// FAR:  "way later" loom — fixed 32× regardless of player tier so
		//       every player from the start can sense a macro-world beyond.
		float base = 4.0f * std::pow(2.0f, float(std::max(1, tier) - 1));
		// 2 near, 2 mid, 1 far — sparse so the arena reads as the hero layer.
		for (int i = 0; i < 2; ++i) push(SilhouetteType::CELL_GENERIC, base,        0);
		for (int i = 0; i < 2; ++i) push(SilhouetteType::CELL_GENERIC, base * 2.0f, 1);
		for (int i = 0; i < 1; ++i) push(SilhouetteType::CELL_GENERIC, 32.0f,       2);
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
		const Spawn spawns[5] = {
			{SilhouetteType::FISH,   glm::vec2(-300.0f,  120.0f), 0,  6.0f},
			{SilhouetteType::TURTLE, glm::vec2( 520.0f, -320.0f), 1,  9.0f},
			{SilhouetteType::JELLY,  glm::vec2( 200.0f,  380.0f), 0,  6.0f},
			// Looming macro layer — a distant whale-shark and jelly swarm
			// at ~32× baseline. Heavy blur + low parallax reads as "layer
			// of the world you haven't grown into yet".
			{SilhouetteType::FISH,   glm::vec2(-800.0f, -550.0f), 2, 28.0f},
			{SilhouetteType::JELLY,  glm::vec2( 700.0f,  600.0f), 2, 26.0f},
		};
		for (const auto& s : spawns) {
			BgCreature c = spawn_(s.t, SPAWN_R, s.s, s.layer);
			c.pos = s.pos;  // override random position
			c.parallax     = STYLE[s.layer].parallax;
			c.stroke_width = STYLE[s.layer].stroke_width;
			c.tint_mix     = STYLE[s.layer].tint_mix;
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
