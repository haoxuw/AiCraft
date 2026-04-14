// CellCraft — polygon helpers. Header-only, glm-based.
//
// MVP implementations: O(n*m) overlap, ray-cast point-in-polygon, shoelace
// area. Good enough for ~dozens of monsters on screen; replace with a
// broad-phase + SAT when it starts showing up in profiles.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace civcraft::cellcraft::sim {

inline float polygon_area(const std::vector<glm::vec2>& poly) {
	const size_t n = poly.size();
	if (n < 3) return 0.0f;
	float s = 0.0f;
	for (size_t i = 0; i < n; ++i) {
		const glm::vec2& a = poly[i];
		const glm::vec2& b = poly[(i + 1) % n];
		s += a.x * b.y - b.x * a.y;
	}
	return std::fabs(s) * 0.5f;
}

inline float polygon_perimeter(const std::vector<glm::vec2>& poly) {
	const size_t n = poly.size();
	if (n < 2) return 0.0f;
	float p = 0.0f;
	for (size_t i = 0; i < n; ++i) {
		p += glm::length(poly[(i + 1) % n] - poly[i]);
	}
	return p;
}

inline bool point_in_polygon(const glm::vec2& p, const std::vector<glm::vec2>& poly) {
	const size_t n = poly.size();
	if (n < 3) return false;
	bool inside = false;
	for (size_t i = 0, j = n - 1; i < n; j = i++) {
		const glm::vec2& a = poly[i];
		const glm::vec2& b = poly[j];
		const bool cross_y = (a.y > p.y) != (b.y > p.y);
		if (cross_y) {
			const float x_at = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
			if (p.x < x_at) inside = !inside;
		}
	}
	return inside;
}

// Segment-segment intersection (open segments; shared endpoints don't count).
inline bool segments_intersect(const glm::vec2& p1, const glm::vec2& p2,
                               const glm::vec2& p3, const glm::vec2& p4) {
	const glm::vec2 r = p2 - p1;
	const glm::vec2 s = p4 - p3;
	const float denom = r.x * s.y - r.y * s.x;
	if (std::fabs(denom) < 1e-6f) return false; // parallel / colinear — treat as non-intersecting
	const glm::vec2 d = p3 - p1;
	const float t = (d.x * s.y - d.y * s.x) / denom;
	const float u = (d.x * r.y - d.y * r.x) / denom;
	const float eps = 1e-4f;
	return (t > eps && t < 1.0f - eps && u > eps && u < 1.0f - eps);
}

inline std::vector<glm::vec2> transform_to_world(const std::vector<glm::vec2>& local,
                                                 const glm::vec2& pos, float heading) {
	const float c = std::cos(heading);
	const float s = std::sin(heading);
	std::vector<glm::vec2> out;
	out.reserve(local.size());
	for (const auto& v : local) {
		out.emplace_back(pos.x + c * v.x - s * v.y,
		                 pos.y + s * v.x + c * v.y);
	}
	return out;
}

// Approximate overlap test: any vertex of A inside B, or any vertex of B inside A,
// or any edge of A intersects any edge of B. Good enough for MVP.
inline bool polygons_overlap(const std::vector<glm::vec2>& a,
                             const std::vector<glm::vec2>& b) {
	if (a.size() < 3 || b.size() < 3) return false;
	for (const auto& v : a) if (point_in_polygon(v, b)) return true;
	for (const auto& v : b) if (point_in_polygon(v, a)) return true;
	const size_t na = a.size(), nb = b.size();
	for (size_t i = 0; i < na; ++i) {
		const glm::vec2& a1 = a[i];
		const glm::vec2& a2 = a[(i + 1) % na];
		for (size_t j = 0; j < nb; ++j) {
			const glm::vec2& b1 = b[j];
			const glm::vec2& b2 = b[(j + 1) % nb];
			if (segments_intersect(a1, a2, b1, b2)) return true;
		}
	}
	return false;
}

// Compute a rough contact point between two overlapping polygons:
// average of all A-vertices inside B, and B-vertices inside A.
// Returns {contact, ok}. ok=false if polygons do not overlap by vertex
// containment (rare if overlap is pure edge-crossing).
inline bool rough_contact_point(const std::vector<glm::vec2>& a,
                                const std::vector<glm::vec2>& b,
                                glm::vec2& out_point, size_t& out_vert_idx_of_a) {
	glm::vec2 sum(0.0f);
	int count = 0;
	size_t best_a = 0;
	// We want to know which vertex of A is inside B (for pointiness at that vertex).
	bool have_a_vertex = false;
	for (size_t i = 0; i < a.size(); ++i) {
		if (point_in_polygon(a[i], b)) {
			sum += a[i];
			++count;
			if (!have_a_vertex) {
				best_a = i;
				have_a_vertex = true;
			}
		}
	}
	for (const auto& v : b) {
		if (point_in_polygon(v, a)) { sum += v; ++count; }
	}
	if (count == 0) return false;
	out_point = sum / float(count);
	out_vert_idx_of_a = best_a;
	return have_a_vertex;
}

// Pointiness at a vertex of a closed polygon: 1 - cos(interior_angle).
// Sharp spike → ~1; flat edge → ~0. Range [0,1] clamped.
inline float vertex_pointiness(const std::vector<glm::vec2>& poly, size_t i) {
	const size_t n = poly.size();
	if (n < 3) return 0.0f;
	const glm::vec2& prev = poly[(i + n - 1) % n];
	const glm::vec2& cur  = poly[i];
	const glm::vec2& next = poly[(i + 1) % n];
	glm::vec2 e1 = prev - cur;
	glm::vec2 e2 = next - cur;
	float l1 = glm::length(e1), l2 = glm::length(e2);
	if (l1 < 1e-5f || l2 < 1e-5f) return 0.0f;
	float d = glm::dot(e1, e2) / (l1 * l2);
	// d = cos(angle); d near 1 → edges nearly parallel same dir → no body → pointy
	// d near -1 → edges opposite → flat edge → not pointy
	// pointiness grows as d → +1; use (1+d)/2 shifted so flat(d=-1)=0, straight(d=0)=0.5, spike(d=1)=1
	float p = (d + 1.0f) * 0.5f;
	if (p < 0.0f) p = 0.0f;
	if (p > 1.0f) p = 1.0f;
	return p;
}

// Local-space bounding extents for a polygon centered on origin:
// returns { half_length_along_x, half_width_along_y } using AABB as proxy.
inline glm::vec2 polygon_local_halfextents(const std::vector<glm::vec2>& poly) {
	if (poly.empty()) return glm::vec2(0.0f);
	float minx = poly[0].x, maxx = poly[0].x;
	float miny = poly[0].y, maxy = poly[0].y;
	for (const auto& v : poly) {
		if (v.x < minx) minx = v.x;
		if (v.x > maxx) maxx = v.x;
		if (v.y < miny) miny = v.y;
		if (v.y > maxy) maxy = v.y;
	}
	return glm::vec2(std::max(std::fabs(minx), std::fabs(maxx)),
	                 std::max(std::fabs(miny), std::fabs(maxy)));
}

inline float polygon_max_radius_from_origin(const std::vector<glm::vec2>& poly) {
	float best = 0.0f;
	for (const auto& v : poly) {
		float r = glm::length(v);
		if (r > best) best = r;
	}
	return best;
}

} // namespace civcraft::cellcraft::sim
