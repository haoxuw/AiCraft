// CellCraft — build a symmetric closed body from half-strokes drawn on the
// right side of a vertical mirror axis.
//
// Pipeline:
//   1. Concatenate strokes in draw order; snap first + last to axis.
//   2. Resample to uniform arc-length (~3px).
//   3. Detect sharp corners to preserve (angle > 45°, local max over ±5).
//   4. Segment-wise Chaikin (2 iters) with corners anchored.
//   5. Mirror across x = axis_x; dedupe axis endpoints.
//
// The result is a closed polygon (first vertex != last). Suitable input to
// sim::validate_shape().

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

namespace civcraft::cellcraft::sim {

namespace detail_sym {

inline std::vector<glm::vec2> concat_and_snap(
		const std::vector<std::vector<glm::vec2>>& half_strokes,
		float axis_x) {
	std::vector<glm::vec2> out;
	for (const auto& s : half_strokes) {
		for (const auto& p : s) out.push_back(p);
	}
	if (out.size() >= 2) {
		out.front().x = axis_x;
		out.back().x  = axis_x;
	}
	return out;
}

// Uniform arc-length resample. Keeps endpoints.
inline std::vector<glm::vec2> resample(const std::vector<glm::vec2>& in, float step) {
	std::vector<glm::vec2> out;
	if (in.size() < 2) return in;
	out.push_back(in.front());
	float accum = 0.0f;
	for (size_t i = 1; i < in.size(); ++i) {
		glm::vec2 a = in[i - 1];
		glm::vec2 b = in[i];
		float seg = glm::length(b - a);
		if (seg < 1e-6f) continue;
		float t = step - accum;
		while (t < seg) {
			float u = t / seg;
			out.push_back(a + (b - a) * u);
			t += step;
		}
		accum = seg - (t - step);
	}
	out.push_back(in.back());
	return out;
}

// Detect preserved corners (indices into samples). Endpoints (0, n-1) always.
inline std::vector<int> detect_corners(const std::vector<glm::vec2>& pts) {
	std::vector<int> corners;
	const int n = (int)pts.size();
	if (n < 3) {
		for (int i = 0; i < n; ++i) corners.push_back(i);
		return corners;
	}
	std::vector<float> ang(n, 0.0f);
	for (int i = 1; i < n - 1; ++i) {
		glm::vec2 d1 = pts[i] - pts[i - 1];
		glm::vec2 d2 = pts[i + 1] - pts[i];
		float l1 = glm::length(d1), l2 = glm::length(d2);
		if (l1 < 1e-6f || l2 < 1e-6f) { ang[i] = 0.0f; continue; }
		glm::vec2 u1 = d1 / l1, u2 = d2 / l2;
		float dot = std::clamp(u1.x * u2.x + u1.y * u2.y, -1.0f, 1.0f);
		ang[i] = std::acos(dot); // 0 = straight, pi = U-turn
	}
	const float kThresh = 0.7854f; // 45°
	const int   kWin    = 5;
	corners.push_back(0);
	for (int i = 1; i < n - 1; ++i) {
		if (ang[i] <= kThresh) continue;
		bool is_max = true;
		for (int j = std::max(1, i - kWin); j <= std::min(n - 2, i + kWin); ++j) {
			if (j == i) continue;
			if (ang[j] > ang[i]) { is_max = false; break; }
		}
		if (is_max) corners.push_back(i);
	}
	corners.push_back(n - 1);
	return corners;
}

// Chaikin iteration on an open polyline with endpoints anchored.
inline std::vector<glm::vec2> chaikin_open(const std::vector<glm::vec2>& p) {
	const int n = (int)p.size();
	std::vector<glm::vec2> out;
	if (n < 3) return p;
	out.reserve(n * 2);
	out.push_back(p.front());
	for (int i = 0; i + 1 < n; ++i) {
		const glm::vec2& a = p[i];
		const glm::vec2& b = p[i + 1];
		// Only emit the mid-offset vertices for interior edges; endpoints
		// stay anchored (preserve the segment's start/end corners).
		if (i > 0)       out.push_back(a * 0.75f + b * 0.25f);
		if (i + 2 < n)   out.push_back(a * 0.25f + b * 0.75f);
	}
	out.push_back(p.back());
	return out;
}

inline std::vector<glm::vec2> smooth_arc(const std::vector<glm::vec2>& arc, int iters) {
	std::vector<glm::vec2> cur = arc;
	for (int k = 0; k < iters; ++k) cur = chaikin_open(cur);
	return cur;
}

} // namespace detail_sym

inline std::vector<glm::vec2> buildSymmetricBody(
		const std::vector<std::vector<glm::vec2>>& half_strokes,
		float axis_x) {
	using namespace detail_sym;

	std::vector<glm::vec2> raw = concat_and_snap(half_strokes, axis_x);
	if (raw.size() < 2) return {};

	// Resample step ~6px — keeps vertex budget tight after Chaikin+mirror.
	std::vector<glm::vec2> samples = resample(raw, 6.0f);
	if (samples.size() < 2) return {};

	// Force endpoints onto the axis (resample may nudge them).
	samples.front().x = axis_x;
	samples.back().x  = axis_x;

	std::vector<int> corners = detect_corners(samples);
	// Build segments between consecutive corners, smooth each, concat.
	std::vector<glm::vec2> half;
	for (size_t c = 0; c + 1 < corners.size(); ++c) {
		int i0 = corners[c], i1 = corners[c + 1];
		std::vector<glm::vec2> arc(samples.begin() + i0, samples.begin() + i1 + 1);
		auto smoothed = smooth_arc(arc, 1);
		if (c == 0) half.insert(half.end(), smoothed.begin(), smoothed.end());
		else        half.insert(half.end(), smoothed.begin() + 1, smoothed.end());
	}

	// Snap the two axis endpoints exactly.
	if (!half.empty()) {
		half.front().x = axis_x;
		half.back().x  = axis_x;
	}

	// Mirror across axis_x. Full polygon = half + reverse(mirror(half_interior)).
	// Interior only — drop the two axis endpoints during mirror to avoid
	// duplicates where the halves meet.
	std::vector<glm::vec2> poly = half;
	if (half.size() >= 2) {
		for (int i = (int)half.size() - 2; i >= 1; --i) {
			glm::vec2 m(2.0f * axis_x - half[i].x, half[i].y);
			poly.push_back(m);
		}
	}
	// poly is closed implicitly (first vertex != last).
	return poly;
}

} // namespace civcraft::cellcraft::sim
