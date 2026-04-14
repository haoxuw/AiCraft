// CellCraft — Plate: a painted armor ribbon on a cell's boundary.
//
// Plates are angular ranges [theta_start, theta_end] in the monster's
// local angle space (same convention as RadialCell: θ = 0 → +x). They
// act as distributed ARMOR: contacts whose world-relative angle falls
// inside a plate's arc get damage halved.
//
// Plates are always stored in canonical order (theta_start <= theta_end
// modulo wrap). Mirroring is the caller's responsibility — adding a
// plate at [a, b] should also add one at [π − b, π − a] to preserve
// bilateral symmetry.

#pragma once

#include <cmath>
#include <vector>

namespace civcraft::cellcraft::sim {

struct Plate {
	float theta_start = 0.0f;  // radians
	float theta_end   = 0.0f;  // radians
	float thickness   = 4.0f;  // render thickness (cost ignores this)
};

// Cost per pixel of arc length.
constexpr float PLATE_COST_PER_PX = 0.05f;

// Returns true if `angle` lies within plate's [start, end] arc (with
// small tolerance `tol`). Handles wrap at ±π.
inline bool plate_covers(const Plate& p, float angle, float tol = 0.05f) {
	const float TWO_PI = 6.28318530718f;
	auto norm = [TWO_PI](float a) {
		a = std::fmod(a, TWO_PI);
		if (a < 0.0f) a += TWO_PI;
		return a;
	};
	float s = norm(p.theta_start - tol);
	float e = norm(p.theta_end   + tol);
	float x = norm(angle);
	if (s <= e) return x >= s && x <= e;
	return x >= s || x <= e;
}

inline bool any_plate_covers(const std::vector<Plate>& plates, float angle,
                             float tol = 0.05f) {
	for (const auto& p : plates) if (plate_covers(p, angle, tol)) return true;
	return false;
}

// Auto-mirror helper: given a plate on the right half, produce the
// mirrored plate across the vertical axis.
inline Plate mirrored_plate(const Plate& p) {
	const float PI = 3.14159265359f;
	Plate m;
	m.theta_start = PI - p.theta_end;
	m.theta_end   = PI - p.theta_start;
	m.thickness   = p.thickness;
	return m;
}

} // namespace civcraft::cellcraft::sim
