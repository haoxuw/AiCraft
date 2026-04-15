#include "CellCraft/client/ambient_particles.h"

#include <cmath>

#include "CellCraft/client/chalk_renderer.h"

namespace civcraft::cellcraft {

static constexpr int TARGET_MOTES = 24;

float AmbientParticles::rand01_() {
	// xorshift32 → float in [0,1).
	rng_state_ ^= rng_state_ << 13;
	rng_state_ ^= rng_state_ >> 17;
	rng_state_ ^= rng_state_ << 5;
	return (rng_state_ & 0xFFFFFF) / float(0x1000000);
}

void AmbientParticles::init(unsigned seed) {
	rng_state_ = seed ? seed : 0xA11CEu;
	motes_.clear();
	motes_.reserve(TARGET_MOTES);
}

void AmbientParticles::spawn_one_(int w, int h, bool initial) {
	Mote m;
	m.p = glm::vec2(rand01_() * w, rand01_() * h);
	float ang = rand01_() * 6.28318f;
	float spd = 6.0f + rand01_() * 14.0f;        // pixels/s — slow drift
	m.v = glm::vec2(std::cos(ang), std::sin(ang)) * spd;
	m.life_max = 3.0f + rand01_() * 4.0f;
	m.life = initial ? rand01_() * m.life_max : m.life_max;
	m.half_width = 0.8f + rand01_() * 1.4f;
	m.shade = 0.35f + rand01_() * 0.35f;
	motes_.push_back(m);
}

void AmbientParticles::update(float dt, int w, int h) {
	last_w_ = w; last_h_ = h;
	if (motes_.empty()) {
		for (int i = 0; i < TARGET_MOTES; ++i) spawn_one_(w, h, true);
	}
	for (auto& m : motes_) {
		m.p += m.v * dt;
		m.life -= dt;
	}
	// Cull expired / off-screen; respawn to keep count stable.
	const float pad = 40.0f;
	size_t dst = 0;
	for (size_t i = 0; i < motes_.size(); ++i) {
		const auto& m = motes_[i];
		bool alive = m.life > 0.0f
			&& m.p.x > -pad && m.p.x < w + pad
			&& m.p.y > -pad && m.p.y < h + pad;
		if (alive) {
			if (dst != i) motes_[dst] = motes_[i];
			++dst;
		}
	}
	motes_.resize(dst);
	while ((int)motes_.size() < TARGET_MOTES) spawn_one_(w, h, false);
}

void AmbientParticles::draw(ChalkRenderer* r, int w, int h) {
	if (!r) return;
	strokes_.clear();
	strokes_.reserve(motes_.size());
	for (const auto& m : motes_) {
		// Fade in first 20% of life, fade out last 40%.
		float t = m.life / m.life_max;         // 1 → 0
		float fade = 1.0f;
		if (t > 0.8f) fade = (1.0f - t) / 0.2f;
		else if (t < 0.4f) fade = t / 0.4f;
		fade = glm::clamp(fade, 0.0f, 1.0f);
		float shade = m.shade * fade;

		// Short segment in the velocity direction for a "streak".
		glm::vec2 d = m.v;
		float vl = glm::length(d);
		if (vl < 1e-3f) d = glm::vec2(1.0f, 0.0f);
		else            d = d / vl;

		ChalkStroke s;
		s.half_width = m.half_width;
		s.color = glm::vec3(shade, shade, shade * 0.95f);
		s.points = { m.p - d * 3.0f, m.p + d * 3.0f };
		strokes_.push_back(std::move(s));
	}
	r->drawStrokes(strokes_, nullptr, w, h);
}

} // namespace civcraft::cellcraft
