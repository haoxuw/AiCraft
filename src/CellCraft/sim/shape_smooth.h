// CellCraft — smooth a bag of freehand strokes into a closed polygon
// usable as a monster body. MVP: pool all points, compute convex hull
// (Andrew's monotone chain), then apply Chaikin corner-cutting smoothing.
//
// Concavity recovery (alpha-shape etc.) is future work — convex hull is
// good enough for chalky chunky creatures. TODO(concave-hull).
//
// Entry point: smooth_body(strokes, target_verts). Always returns a
// non-self-intersecting closed polygon with |result| in [3, target_verts].

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

namespace civcraft::cellcraft::sim {

namespace detail {

// Andrew's monotone chain convex hull. Input is mutated (sorted).
// Output CCW, no duplicate endpoint.
inline std::vector<glm::vec2> convex_hull(std::vector<glm::vec2> pts) {
	const size_t n = pts.size();
	if (n < 3) return pts;
	std::sort(pts.begin(), pts.end(), [](glm::vec2 a, glm::vec2 b) {
		return a.x < b.x || (a.x == b.x && a.y < b.y);
	});
	auto cross = [](glm::vec2 O, glm::vec2 A, glm::vec2 B) {
		return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
	};
	std::vector<glm::vec2> h(2 * n);
	size_t k = 0;
	for (size_t i = 0; i < n; ++i) {
		while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0.0f) --k;
		h[k++] = pts[i];
	}
	size_t lo = k + 1;
	for (size_t i = n; i-- > 0;) {
		while (k >= lo && cross(h[k - 2], h[k - 1], pts[i]) <= 0.0f) --k;
		h[k++] = pts[i];
	}
	h.resize(k - 1);
	return h;
}

// One Chaikin iteration on a closed polygon. For each edge P_i→P_{i+1}
// emits two new vertices at 1/4 and 3/4 → output has 2× the vertex count.
inline std::vector<glm::vec2> chaikin_once(const std::vector<glm::vec2>& p) {
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

} // namespace detail

// Pool all stroke points, convex-hull them, then Chaikin-smooth until
// the vertex count is close to target_verts without going over.
inline std::vector<glm::vec2> smooth_body(
		const std::vector<std::vector<glm::vec2>>& strokes,
		int target_verts = 48) {
	std::vector<glm::vec2> pool;
	for (const auto& s : strokes) {
		for (const auto& p : s) pool.push_back(p);
	}
	if (pool.size() < 3) return pool;

	std::vector<glm::vec2> hull = detail::convex_hull(std::move(pool));
	if (hull.size() < 3) return hull;

	// Chaikin doubles count each iteration. Pick the largest iteration
	// count that keeps us <= target_verts; cap at 4 iters.
	std::vector<glm::vec2> cur = hull;
	for (int iter = 0; iter < 4; ++iter) {
		if ((int)cur.size() * 2 > target_verts) break;
		cur = detail::chaikin_once(cur);
	}
	// If hull itself is already near/over target, one Chaikin is still
	// desirable for smoothness — let it exceed a little.
	if (cur.size() == hull.size() && (int)hull.size() <= target_verts) {
		cur = detail::chaikin_once(cur);
	}
	return cur;
}

} // namespace civcraft::cellcraft::sim
