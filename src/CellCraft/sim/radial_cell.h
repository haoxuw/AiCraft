// CellCraft — RadialCell: playdough-like monster body representation.
//
// The cell stores a radius per discrete angle sample. Brush pushes/pulls
// the boundary; symmetry across the vertical head/tail axis is enforced
// after every edit so the creature always looks bilateral.
//
// Convention: θ = 0 points along +x (screen right). θ = π/2 points along
// +y (head-up in local space). Vertical-axis mirror means r[i] ==
// r[mirror_index(i)] where mirror_index swaps the right/left halves
// across the y-axis.
//
// Polygon output from cellToPolygon() is already in local space with the
// core at origin; pass it straight to Monster::shape.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

namespace civcraft::cellcraft::sim {

struct RadialCell {
	static constexpr int   N        = 64;
	static constexpr float TWO_PI   = 6.28318530718f;
	static constexpr float R_MIN    = 10.0f;
	static constexpr float R_MAX    = 200.0f;

	float r[N];
	float base_r = 40.0f;

	void init_circle(float R) {
		base_r = R;
		for (int i = 0; i < N; ++i) r[i] = R;
	}

	// Mirror across the vertical (y) axis: the angle i → (π − angle_i).
	// angle_i = 2π·i/N → mirror angle = π − 2π·i/N → index (N/2 − i) mod N.
	static int mirror_index(int i) { return ((N / 2 - i) % N + N) % N; }

	void enforce_symmetry_() {
		for (int i = 0; i < N; ++i) {
			int j = mirror_index(i);
			if (j <= i) continue;
			float avg = 0.5f * (r[i] + r[j]);
			r[i] = avg;
			r[j] = avg;
		}
		for (int i = 0; i < N; ++i) {
			if (r[i] < R_MIN) r[i] = R_MIN;
			if (r[i] > R_MAX) r[i] = R_MAX;
		}
	}
};

// Chaikin one iteration on a closed polygon (output 2× verts).
inline std::vector<glm::vec2> chaikin_closed_(const std::vector<glm::vec2>& p) {
	const size_t n = p.size();
	std::vector<glm::vec2> out;
	if (n < 3) return p;
	out.reserve(n * 2);
	for (size_t i = 0; i < n; ++i) {
		const glm::vec2& a = p[i];
		const glm::vec2& b = p[(i + 1) % n];
		out.push_back(a * 0.75f + b * 0.25f);
		out.push_back(a * 0.25f + b * 0.75f);
	}
	return out;
}

// Convert the radial cell to a closed polygon in local space.
// smooth_iters: Chaikin iterations (default 1 — keeps the chalky look).
inline std::vector<glm::vec2> cellToPolygon(const RadialCell& c, int smooth_iters = 1) {
	std::vector<glm::vec2> raw;
	raw.reserve(RadialCell::N);
	for (int i = 0; i < RadialCell::N; ++i) {
		float a = RadialCell::TWO_PI * (float)i / (float)RadialCell::N;
		raw.emplace_back(std::cos(a) * c.r[i], std::sin(a) * c.r[i]);
	}
	std::vector<glm::vec2> cur = raw;
	for (int k = 0; k < smooth_iters; ++k) cur = chaikin_closed_(cur);
	return cur;
}

inline float cellPerimeter(const RadialCell& c) {
	auto poly = cellToPolygon(c, 1);
	const size_t n = poly.size();
	if (n < 2) return 0.0f;
	float s = 0.0f;
	for (size_t i = 0; i < n; ++i) {
		s += glm::length(poly[(i + 1) % n] - poly[i]);
	}
	return s;
}

// Gaussian-falloff brush push/pull at world-relative angle θ.
// Positive delta pushes outward; negative pulls inward. sigma is angular
// stddev in radians. Auto-enforces symmetry.
inline void brushDeform(RadialCell& c, float theta, float delta, float sigma) {
	const float two_sigma_sq = 2.0f * std::max(1e-4f, sigma * sigma);
	for (int i = 0; i < RadialCell::N; ++i) {
		float a = RadialCell::TWO_PI * (float)i / (float)RadialCell::N;
		// Shortest angular distance.
		float d = a - theta;
		while (d >  3.14159265f) d -= RadialCell::TWO_PI;
		while (d < -3.14159265f) d += RadialCell::TWO_PI;
		float w = std::exp(-(d * d) / two_sigma_sq);
		c.r[i] += delta * w;
	}
	c.enforce_symmetry_();
}

} // namespace civcraft::cellcraft::sim
