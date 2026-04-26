#include "server/voxel_earth/rotate.h"

#include <cmath>

namespace solarium::voxel_earth {

namespace {

// Internal double-precision math types — ECEF coords are 6.4e6 m magnitude,
// float (7 sig figs) gives ~1m precision which is too coarse. Vertices are
// downcast to float only at the end.
struct Vec3d { double x, y, z; };
struct Quat  { double x, y, z, w; };

inline double clampd(double v, double lo, double hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}
inline double dot(const Vec3d& a, const Vec3d& b)   { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3d  cross(const Vec3d& a, const Vec3d& b) {
	return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
inline Vec3d normalize(const Vec3d& v) {
	double L = std::sqrt(dot(v, v));
	if (L < 1e-12) return { 0, 1, 0 };
	return { v.x / L, v.y / L, v.z / L };
}

// Shortest-arc quaternion that rotates unit vector `from` to unit vector `to`.
Quat quat_from_unit_vectors(const Vec3d& from, const Vec3d& to) {
	double d = clampd(dot(from, to), -1.0, 1.0);
	if (d > 0.999999)  return { 0, 0, 0, 1 };
	if (d < -0.999999) {
		Vec3d axis = cross({ 1, 0, 0 }, from);
		if (std::sqrt(dot(axis, axis)) < 1e-5) axis = cross({ 0, 1, 0 }, from);
		axis = normalize(axis);
		const double s = 1.0;             // sin(π/2)
		const double c = 0.0;             // cos(π/2)
		return { axis.x*s, axis.y*s, axis.z*s, c };
	}
	Vec3d a = cross(from, to);
	double s = std::sqrt((1.0 + d) * 2.0);
	double inv = 1.0 / s;
	return { a.x*inv, a.y*inv, a.z*inv, s * 0.5 };
}

inline Vec3d rotate(const Quat& q, const Vec3d& v) {
	// v' = v + 2·q.w·(q.xyz × v) + 2·(q.xyz × (q.xyz × v))
	Vec3d qv = { q.x, q.y, q.z };
	Vec3d t  = cross(qv, v);
	t = { 2.0 * t.x, 2.0 * t.y, 2.0 * t.z };
	Vec3d c  = cross(qv, t);
	return { v.x + q.w*t.x + c.x, v.y + q.w*t.y + c.y, v.z + q.w*t.z + c.z };
}

// Apply column-major 4×4 to (v, 1).
inline Vec3d transform_point(const std::array<float, 16>& M, const Vec3d& v) {
	return {
		M[0]*v.x + M[4]*v.y + M[8] *v.z + M[12],
		M[1]*v.x + M[5]*v.y + M[9] *v.z + M[13],
		M[2]*v.x + M[6]*v.y + M[10]*v.z + M[14],
	};
}

// 3×3 part of M applied to a vector (no translation, no normalize).
inline Vec3d transform_dir(const std::array<float, 16>& M, const Vec3d& v) {
	return {
		M[0]*v.x + M[4]*v.y + M[8] *v.z,
		M[1]*v.x + M[5]*v.y + M[9] *v.z,
		M[2]*v.x + M[6]*v.y + M[10]*v.z,
	};
}

}  // namespace

std::array<double, 3> origin_from_root(const Glb& glb) {
	return {
		static_cast<double>(glb.root_translation[0]),
		static_cast<double>(glb.root_translation[1]),
		static_cast<double>(glb.root_translation[2]),
	};
}

void rotate_to_local_enu(Glb& glb, const std::array<double, 3>& origin) {
	Vec3d C { origin[0], origin[1], origin[2] };
	Vec3d up_ecef = normalize(C);
	Quat  q = quat_from_unit_vectors(up_ecef, { 0, 1, 0 });

	for (auto& mesh : glb.meshes) {
		for (auto& p : mesh.positions) {
			Vec3d ecef     = transform_point(glb.root_matrix, { p.x, p.y, p.z });
			Vec3d centered = { ecef.x - C.x, ecef.y - C.y, ecef.z - C.z };
			Vec3d local    = rotate(q, centered);
			p.x = static_cast<float>(local.x);
			p.y = static_cast<float>(local.y);
			p.z = static_cast<float>(local.z);
		}
		for (auto& n : mesh.normals) {
			Vec3d dir     = transform_dir(glb.root_matrix, { n.x, n.y, n.z });
			Vec3d rotated = rotate(q, dir);
			Vec3d nu      = normalize(rotated);
			n.x = static_cast<float>(nu.x);
			n.y = static_cast<float>(nu.y);
			n.z = static_cast<float>(nu.z);
		}
	}

	glb.root_translation = { 0.0f, 0.0f, 0.0f };
	glb.root_matrix = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
	};
}

}  // namespace solarium::voxel_earth
