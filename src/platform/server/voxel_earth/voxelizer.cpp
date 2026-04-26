#include "server/voxel_earth/voxelizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace civcraft::voxel_earth {

namespace {

struct Hit {
	uint32_t tri;     // triangle index (global, across all meshes)
	float    d2;      // squared distance from voxel center to triangle plane
};

inline int axis_X(int /*ax*/) { return 0; }  // placeholder

// Pack (x, y, z) within grid into a uint64 key for the hits hashmap.
inline uint64_t key(int x, int y, int z, int nx, int ny) {
	return static_cast<uint64_t>(x)
	     + static_cast<uint64_t>(nx) * (static_cast<uint64_t>(y)
	     + static_cast<uint64_t>(ny) * static_cast<uint64_t>(z));
}

inline float frac(float v) { return v - std::floor(v); }

}  // namespace

VoxelGrid voxelize(const Glb& glb, const Texture& texture, const VoxelizeOptions& opts) {
	VoxelGrid grid;

	// 1. Compute global bbox across all meshes.
	float min_x =  std::numeric_limits<float>::infinity();
	float min_y =  std::numeric_limits<float>::infinity();
	float min_z =  std::numeric_limits<float>::infinity();
	float max_x = -std::numeric_limits<float>::infinity();
	float max_y = -std::numeric_limits<float>::infinity();
	float max_z = -std::numeric_limits<float>::infinity();
	size_t total_tris = 0;
	for (const auto& m : glb.meshes) {
		total_tris += m.indices.size() / 3;
		for (const auto& p : m.positions) {
			min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
			min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
			min_z = std::min(min_z, p.z); max_z = std::max(max_z, p.z);
		}
	}
	if (total_tris == 0 || !std::isfinite(min_x)) return grid;

	grid.bbox_min = { min_x, min_y, min_z };
	grid.bbox_max = { max_x, max_y, max_z };
	const float span_x = max_x - min_x;
	const float span_y = max_y - min_y;
	const float span_z = max_z - min_z;
	const float max_span = std::max({ span_x, span_y, span_z });
	if (max_span <= 0.0f) return grid;

	const float vs = (opts.voxel_size_meters > 0.0f)
		? opts.voxel_size_meters
		: max_span / static_cast<float>(opts.resolution);
	grid.voxel_size = vs;
	grid.dims = {
		std::max(1, static_cast<int>(std::ceil(span_x / vs))),
		std::max(1, static_cast<int>(std::ceil(span_y / vs))),
		std::max(1, static_cast<int>(std::ceil(span_z / vs))),
	};
	const int NX = grid.dims[0], NY = grid.dims[1], NZ = grid.dims[2];

	// 2. Flatten meshes into one global vertex/index list, recording UV +
	//    mesh index for color sampling. Vertices stay in mesh-local meters.
	struct GlobalTri {
		uint32_t i0, i1, i2;
	};
	std::vector<float>    pos;       // size = 3 * vertex count
	std::vector<float>    uv;        // size = 2 * vertex count, default 0
	std::vector<GlobalTri> tris;
	pos.reserve(0);
	uv.reserve(0);
	tris.reserve(total_tris);

	uint32_t vbase = 0;
	for (const auto& m : glb.meshes) {
		const size_t v0 = pos.size() / 3;
		for (const auto& p : m.positions) {
			pos.push_back(p.x); pos.push_back(p.y); pos.push_back(p.z);
		}
		uv.resize((v0 + m.positions.size()) * 2, 0.0f);
		if (!m.uvs.empty()) {
			for (size_t i = 0; i < m.uvs.size(); ++i) {
				uv[(v0 + i) * 2 + 0] = m.uvs[i].u;
				uv[(v0 + i) * 2 + 1] = m.uvs[i].v;
			}
		}
		for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
			tris.push_back({ vbase + m.indices[i + 0],
			                 vbase + m.indices[i + 1],
			                 vbase + m.indices[i + 2] });
		}
		vbase += static_cast<uint32_t>(m.positions.size());
	}

	// 3. Pre-compute vertex positions in voxel space (origin at bbox.min).
	std::vector<float> pvox(pos.size());
	const float inv_vs = 1.0f / vs;
	for (size_t i = 0, n = pos.size() / 3; i < n; ++i) {
		pvox[i * 3 + 0] = (pos[i * 3 + 0] - min_x) * inv_vs;
		pvox[i * 3 + 1] = (pos[i * 3 + 1] - min_y) * inv_vs;
		pvox[i * 3 + 2] = (pos[i * 3 + 2] - min_z) * inv_vs;
	}

	// 4. 2.5D dominant-axis scan converter — ported from JavaCpuVoxelizer.
	std::unordered_map<uint64_t, Hit> hits;
	hits.reserve(static_cast<size_t>(NX) * NY);  // a planar surface fills NX*NY-ish

	const float eps = 1e-6f;

	for (uint32_t ti = 0; ti < tris.size(); ++ti) {
		const auto& t = tris[ti];
		const float x0 = pvox[t.i0 * 3 + 0], y0 = pvox[t.i0 * 3 + 1], z0 = pvox[t.i0 * 3 + 2];
		const float x1 = pvox[t.i1 * 3 + 0], y1 = pvox[t.i1 * 3 + 1], z1 = pvox[t.i1 * 3 + 2];
		const float x2 = pvox[t.i2 * 3 + 0], y2 = pvox[t.i2 * 3 + 1], z2 = pvox[t.i2 * 3 + 2];

		const float e10x = x1 - x0, e10y = y1 - y0, e10z = z1 - z0;
		const float e20x = x2 - x0, e20y = y2 - y0, e20z = z2 - z0;
		const float nx = e10y * e20z - e10z * e20y;
		const float ny = e10z * e20x - e10x * e20z;
		const float nz = e10x * e20y - e10y * e20x;
		const float anx = std::fabs(nx), any = std::fabs(ny), anz = std::fabs(nz);
		const float nn = nx * nx + ny * ny + nz * nz;
		if (nn < 1e-12f) continue;

		// Pick dominant axis (w) and the orthogonal plane (u, v).
		int wAxis, uAxis, vAxis;
		if (anx >= any && anx >= anz)      { wAxis = 0; uAxis = 1; vAxis = 2; }
		else if (any >= anx && any >= anz) { wAxis = 1; uAxis = 2; vAxis = 0; }
		else                                { wAxis = 2; uAxis = 0; vAxis = 1; }

		auto pick = [&](int ax, float a, float b, float c) -> float {
			return ax == 0 ? a : (ax == 1 ? b : c);
		};
		const float U0 = pick(uAxis, x0, y0, z0), V0 = pick(vAxis, x0, y0, z0), W0 = pick(wAxis, x0, y0, z0);
		const float U1 = pick(uAxis, x1, y1, z1), V1 = pick(vAxis, x1, y1, z1), W1 = pick(wAxis, x1, y1, z1);
		const float U2 = pick(uAxis, x2, y2, z2), V2 = pick(vAxis, x2, y2, z2), W2 = pick(wAxis, x2, y2, z2);

		const float denom = (V1 - V2) * (U0 - U2) + (U2 - U1) * (V0 - V2);
		if (std::fabs(denom) < 1e-12f) continue;
		const float invDen = 1.0f / denom;

		const int u_lim = (uAxis == 0 ? NX - 1 : (uAxis == 1 ? NY - 1 : NZ - 1));
		const int v_lim = (vAxis == 0 ? NX - 1 : (vAxis == 1 ? NY - 1 : NZ - 1));
		const int w_lim = (wAxis == 0 ? NX     : (wAxis == 1 ? NY     : NZ));

		const int uMin = std::max(0, static_cast<int>(std::floor(std::min({ U0, U1, U2 }))));
		const int vMin = std::max(0, static_cast<int>(std::floor(std::min({ V0, V1, V2 }))));
		const int uMax = std::min(u_lim, static_cast<int>(std::floor(std::max({ U0, U1, U2 }))));
		const int vMax = std::min(v_lim, static_cast<int>(std::floor(std::max({ V0, V1, V2 }))));
		if (uMin > uMax || vMin > vMax) continue;

		const float nW = (wAxis == 0 ? nx : (wAxis == 1 ? ny : nz));
		const float distScale = (nW * nW) / nn;

		auto store = [&](int u, int v, int w, float d2) {
			int x, y, z;
			if (wAxis == 2)      { x = u; y = v; z = w; }
			else if (wAxis == 1) { z = u; x = v; y = w; }
			else                  { y = u; z = v; x = w; }
			if (x < 0 || y < 0 || z < 0 || x >= NX || y >= NY || z >= NZ) return;
			const uint64_t k = key(x, y, z, NX, NY);
			auto it = hits.find(k);
			if (it == hits.end() || d2 < it->second.d2) {
				hits[k] = { ti, d2 };
			}
		};

		for (int v = vMin; v <= vMax; ++v) {
			const float vv = static_cast<float>(v) + 0.5f;
			for (int u = uMin; u <= uMax; ++u) {
				const float uu = static_cast<float>(u) + 0.5f;
				const float L0 = ((V1 - V2) * (uu - U2) + (U2 - U1) * (vv - V2)) * invDen;
				const float L1 = ((V2 - V0) * (uu - U2) + (U0 - U2) * (vv - V2)) * invDen;
				const float L2 = 1.0f - L0 - L1;
				if (L0 < -eps || L1 < -eps || L2 < -eps) continue;
				const float W = L0 * W0 + L1 * W1 + L2 * W2;
				const int wIdx = static_cast<int>(std::floor(W));
				if (wIdx >= 0 && wIdx < w_lim) {
					const float dW = W - (static_cast<float>(wIdx) + 0.5f);
					store(u, v, wIdx, dW * dW * distScale);
				}
				const float fr = frac(W);
				if (fr < 0.15f || fr > 0.85f) {
					const int w2 = (W - (static_cast<float>(wIdx) + 0.5f)) < 0 ? (wIdx - 1) : (wIdx + 1);
					if (w2 >= 0 && w2 < w_lim) {
						const float dW2 = W - (static_cast<float>(w2) + 0.5f);
						store(u, v, w2, dW2 * dW2 * distScale);
					}
				}
			}
		}
	}

	// 5. Sample colour for each hit voxel from the closest triangle's UV.
	grid.voxels.reserve(hits.size());
	for (const auto& [k, hit] : hits) {
		const int x = static_cast<int>(k % NX);
		const int y = static_cast<int>((k / NX) % NY);
		const int z = static_cast<int>(k / (static_cast<uint64_t>(NX) * NY));

		uint8_t rgba[4] = { 192, 192, 192, 255 };
		if (opts.color_voxels && !texture.empty() && hit.tri < tris.size()) {
			const auto& t = tris[hit.tri];
			// Voxel-center barycentric in the dominant-axis plane: project the
			// voxel center back onto the triangle and interpolate UV.
			const float cx = (static_cast<float>(x) + 0.5f);
			const float cy = (static_cast<float>(y) + 0.5f);
			const float cz = (static_cast<float>(z) + 0.5f);

			const float x0 = pvox[t.i0 * 3 + 0], y0 = pvox[t.i0 * 3 + 1], z0 = pvox[t.i0 * 3 + 2];
			const float x1 = pvox[t.i1 * 3 + 0], y1 = pvox[t.i1 * 3 + 1], z1 = pvox[t.i1 * 3 + 2];
			const float x2 = pvox[t.i2 * 3 + 0], y2 = pvox[t.i2 * 3 + 1], z2 = pvox[t.i2 * 3 + 2];

			// 3D barycentric via projection onto the triangle plane.
			const float ax = x1 - x0, ay = y1 - y0, az = z1 - z0;
			const float bx = x2 - x0, by = y2 - y0, bz = z2 - z0;
			const float px = cx - x0, py = cy - y0, pz = cz - z0;
			const float d00 = ax*ax + ay*ay + az*az;
			const float d01 = ax*bx + ay*by + az*bz;
			const float d11 = bx*bx + by*by + bz*bz;
			const float d20 = px*ax + py*ay + pz*az;
			const float d21 = px*bx + py*by + pz*bz;
			const float den = d00 * d11 - d01 * d01;
			float lu = 1.0f / 3.0f, lv = 1.0f / 3.0f, lw = 1.0f / 3.0f;
			if (std::fabs(den) > 1e-9f) {
				const float v = (d11 * d20 - d01 * d21) / den;
				const float w = (d00 * d21 - d01 * d20) / den;
				lv = std::clamp(v, 0.0f, 1.0f);
				lw = std::clamp(w, 0.0f, 1.0f);
				lu = std::clamp(1.0f - lv - lw, 0.0f, 1.0f);
			}
			const float u_uv = uv[t.i0 * 2 + 0] * lu + uv[t.i1 * 2 + 0] * lv + uv[t.i2 * 2 + 0] * lw;
			const float v_uv = uv[t.i0 * 2 + 1] * lu + uv[t.i1 * 2 + 1] * lv + uv[t.i2 * 2 + 1] * lw;
			texture.sample(u_uv, v_uv, rgba);
		}

		grid.voxels.push_back({
			static_cast<uint16_t>(x),
			static_cast<uint16_t>(y),
			static_cast<uint16_t>(z),
			rgba[0], rgba[1], rgba[2], rgba[3],
		});
	}

	return grid;
}

}  // namespace civcraft::voxel_earth
