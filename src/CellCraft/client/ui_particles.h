#pragma once

// Lightweight UI particle system (CPU-side, renders through
// TextRenderer::drawRect). Separate from the existing
// ambient_particles + gameplay particles so UI feedback doesn't pollute
// the world simulation.
//
// Three flavours:
//   - sparkle: short radial burst of small squares (~6-10 pieces) in
//              the triggering color — used for button click feedback
//              and part-place moments.
//   - confetti: big falling pieces with gravity + rotation, used in
//               the celebration screen and end-screen WIN.
//   - puff: red dusty poof for budget-overflow. (Minimal; see shake.)
//
// Particles live in NDC space; positions update each frame and die off
// after their ttl. Caller ticks, then draws.

#include "client/text.h"
#include "CellCraft/client/ui_theme.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <vector>
#include <random>
#include <cmath>

namespace civcraft::cellcraft::ui {

struct UIParticle {
	glm::vec2 pos{0.0f};       // NDC
	glm::vec2 vel{0.0f};       // NDC / s
	glm::vec2 acc{0.0f};       // NDC / s^2 (gravity for confetti)
	glm::vec4 color{1.0f};
	float     size = 0.012f;   // NDC half-size (square)
	float     ttl  = 0.4f;
	float     ttl_max = 0.4f;
	float     rot = 0.0f;      // unused by rect drawer, but handy for docs
	float     rot_vel = 0.0f;
};

class UIParticleSystem {
public:
	void sparkle(glm::vec2 center_ndc, glm::vec3 color, std::mt19937& rng, int n = 8) {
		std::uniform_real_distribution<float> a(0.0f, 6.28318530718f);
		std::uniform_real_distribution<float> s(0.35f, 0.70f);   // NDC/s
		for (int i = 0; i < n; ++i) {
			float ang = a(rng);
			float sp  = s(rng);
			UIParticle p;
			p.pos = center_ndc;
			p.vel = glm::vec2(std::cos(ang), std::sin(ang)) * sp;
			p.acc = glm::vec2(0.0f, -0.4f);  // slight downward pull
			p.color = glm::vec4(color, 1.0f);
			p.size = 0.008f;
			p.ttl = p.ttl_max = 0.45f;
			parts_.push_back(p);
		}
	}

	// Burst of colorful confetti from above the screen; 60 pieces by
	// default. Used by the celebration + end-screen WIN.
	void confetti(std::mt19937& rng, int n = 60) {
		std::uniform_real_distribution<float> x(-1.0f, 1.0f);
		std::uniform_real_distribution<float> yv(-0.45f, -0.15f);
		std::uniform_real_distribution<float> xv(-0.25f, 0.25f);
		glm::vec4 colors[6] = {
			ACCENT_PINK, ACCENT_CYAN, ACCENT_LIME,
			ACCENT_GOLD, ACCENT_MAGENTA, ACCENT_ORANGE,
		};
		std::uniform_int_distribution<int> ci(0, 5);
		for (int i = 0; i < n; ++i) {
			UIParticle p;
			p.pos = glm::vec2(x(rng), 1.05f);  // start above screen
			p.vel = glm::vec2(xv(rng), yv(rng));
			p.acc = glm::vec2(0.0f, -0.45f);   // gravity (NDC)
			p.color = colors[ci(rng)];
			p.size = 0.010f;
			p.ttl = p.ttl_max = 2.4f;
			p.rot_vel = (xv(rng)) * 12.0f;
			parts_.push_back(p);
		}
	}

	// Small red dust puff for jar overflow.
	void puff(glm::vec2 center_ndc, std::mt19937& rng) {
		std::uniform_real_distribution<float> a(0.0f, 6.28318530718f);
		for (int i = 0; i < 10; ++i) {
			float ang = a(rng);
			UIParticle p;
			p.pos = center_ndc;
			p.vel = glm::vec2(std::cos(ang), std::sin(ang)) * 0.25f;
			p.color = glm::vec4(1.0f, 0.35f, 0.25f, 1.0f);
			p.size = 0.012f;
			p.ttl = p.ttl_max = 0.6f;
			parts_.push_back(p);
		}
	}

	void tick(float dt) {
		for (auto& p : parts_) {
			p.vel += p.acc * dt;
			p.pos += p.vel * dt;
			p.ttl -= dt;
			p.rot += p.rot_vel * dt;
		}
		parts_.erase(std::remove_if(parts_.begin(), parts_.end(),
			[](const UIParticle& p) { return p.ttl <= 0.0f; }), parts_.end());
	}

	void draw(::civcraft::TextRenderer* text) const {
		for (const auto& p : parts_) {
			float a = std::min(1.0f, p.ttl / p.ttl_max);
			glm::vec4 c = p.color; c.a *= a;
			text->drawRect(p.pos.x - p.size, p.pos.y - p.size,
			               p.size * 2.0f, p.size * 2.0f, c);
		}
	}

	bool empty() const { return parts_.empty(); }
	void clear()       { parts_.clear(); }
	size_t count() const { return parts_.size(); }

private:
	std::vector<UIParticle> parts_;
};

} // namespace civcraft::cellcraft::ui
