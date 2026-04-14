// CellCraft — shape validation for drawn polygons submitted by the
// player. Server-side gate before a shape becomes a breed template or
// a spawned monster body.

#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/sim/polygon_util.h"
#include "CellCraft/sim/tuning.h"

namespace civcraft::cellcraft::sim {

enum class ShapeValidation {
	OK,
	NOT_CLOSED,
	SELF_INTERSECT,
	TOO_SMALL,
	TOO_LARGE,
	CORE_OUTSIDE,
	TOO_FEW_VERTS,
	TOO_MANY_VERTS,
};

struct ShapeValidationResult {
	ShapeValidation code = ShapeValidation::OK;
	std::string     message;
};

// Validate that `poly` (world-space) is a usable monster body around `core`.
// If the polygon ends with a duplicate of its first vertex within CLOSE_EPS,
// the duplicate is stripped and the polygon is auto-closed. `poly` is modified
// in place in that case.
inline ShapeValidationResult validate_shape(std::vector<glm::vec2>& poly,
                                            const glm::vec2& core) {
	ShapeValidationResult r;

	if ((int)poly.size() < MIN_VERTS) {
		r.code = ShapeValidation::TOO_FEW_VERTS;
		r.message = "shape has fewer than " + std::to_string(MIN_VERTS) + " vertices";
		return r;
	}
	if ((int)poly.size() > MAX_VERTS) {
		r.code = ShapeValidation::TOO_MANY_VERTS;
		r.message = "shape exceeds " + std::to_string(MAX_VERTS) + " vertices";
		return r;
	}

	// Auto-close if first and last are within CLOSE_EPS.
	if (poly.size() >= 2) {
		float d = glm::length(poly.front() - poly.back());
		if (d <= CLOSE_EPS && poly.size() > 3) {
			poly.pop_back();
		} else if (d > CLOSE_EPS * 8.0f) {
			// Significantly unclosed — reject. CLOSE_EPS*8 is arbitrary slack;
			// we expect the client renderer to emit roughly closed loops.
			r.code = ShapeValidation::NOT_CLOSED;
			r.message = "shape is not closed (endpoint gap " + std::to_string(d) + ")";
			return r;
		}
	}

	float perim = polygon_perimeter(poly);
	if (perim < MIN_PERIMETER) {
		r.code = ShapeValidation::TOO_SMALL;
		r.message = "perimeter too small";
		return r;
	}
	if (perim > MAX_PERIMETER) {
		r.code = ShapeValidation::TOO_LARGE;
		r.message = "perimeter too large";
		return r;
	}

	if (!point_in_polygon(core, poly)) {
		r.code = ShapeValidation::CORE_OUTSIDE;
		r.message = "core is outside the shape";
		return r;
	}

	// O(n²) self-intersection scan — non-adjacent edges only.
	const size_t n = poly.size();
	for (size_t i = 0; i < n; ++i) {
		const glm::vec2& a1 = poly[i];
		const glm::vec2& a2 = poly[(i + 1) % n];
		for (size_t j = i + 1; j < n; ++j) {
			// Skip edges sharing a vertex.
			if (j == i) continue;
			if ((j + 1) % n == i) continue;
			if (j == (i + 1) % n) continue;
			const glm::vec2& b1 = poly[j];
			const glm::vec2& b2 = poly[(j + 1) % n];
			if (segments_intersect(a1, a2, b1, b2)) {
				r.code = ShapeValidation::SELF_INTERSECT;
				r.message = "shape self-intersects";
				return r;
			}
		}
	}

	return r;
}

} // namespace civcraft::cellcraft::sim
