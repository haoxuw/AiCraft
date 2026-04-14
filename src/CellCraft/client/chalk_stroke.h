#pragma once

// ChalkStroke: polyline of screen-space points + color + derived ribbon mesh.
//
// The mesh is two rows of vertices — left and right edges of a constant-width
// ribbon centered on each joint, using the classic miter computation:
//
//	   p_{i-1} ---e1--> p_i ---e2--> p_{i+1}
//	              left edge  = p_i + normal_i * half_width
//	              right edge = p_i - normal_i * half_width
//	   normal_i = normalize(perp(e1) + perp(e2))   (bisector of edge normals)
//	   length   = half_width / max(dot(normal_i, perp(e1)), 0.25)  (miter)
//
// With a bounded miter length (cap at 0.25 of dot to prevent spikes on sharp
// corners), this produces a continuous strip with clean rounded joints when
// stroked over the chalk shader's feathered edges.
//
// Smoothing: `simplify()` is Douglas-Peucker with epsilon 1.0 px (same as
// NumptyPhysics Config.h:33). Applied once when a stroke is finalized.

#include <glm/glm.hpp>
#include <vector>
#include <cmath>

namespace civcraft::cellcraft {

struct RibbonVertex {
	glm::vec2 pos;     // pixel space
	float     across;  // -1 .. +1
	float     along;   // pixels from stroke start
};

struct ChalkStroke {
	std::vector<glm::vec2> points;  // screen-space polyline
	glm::vec3              color = {0.95f, 0.95f, 0.92f};
	float                  half_width = 3.5f;  // pixels

	// ---- Douglas-Peucker ---------------------------------------------
	static float perpDist(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
		glm::vec2 ab = b - a;
		float len2 = glm::dot(ab, ab);
		if (len2 < 1e-4f) return glm::length(p - a);
		float t = glm::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f);
		glm::vec2 c = a + ab * t;
		return glm::length(p - c);
	}

	static void dpRecurse(const std::vector<glm::vec2>& in, int i0, int i1,
	                      float eps, std::vector<glm::vec2>& out) {
		float maxD = 0.0f;
		int   idx  = -1;
		for (int i = i0 + 1; i < i1; ++i) {
			float d = perpDist(in[i], in[i0], in[i1]);
			if (d > maxD) { maxD = d; idx = i; }
		}
		if (maxD > eps && idx != -1) {
			dpRecurse(in, i0, idx, eps, out);
			out.pop_back();  // don't duplicate mid-point
			dpRecurse(in, idx, i1, eps, out);
		} else {
			out.push_back(in[i0]);
			out.push_back(in[i1]);
		}
	}

	void simplify(float eps = 1.0f) {
		if (points.size() < 3) return;
		std::vector<glm::vec2> out;
		out.reserve(points.size());
		dpRecurse(points, 0, (int)points.size() - 1, eps, out);
		points.swap(out);
	}

	// ---- Ribbon mesh -------------------------------------------------
	// Emits two vertices per input point (left edge, right edge). Draw as
	// GL_TRIANGLE_STRIP with 2*N vertices.
	void buildRibbon(std::vector<RibbonVertex>& out) const {
		out.clear();
		if (points.size() < 2) return;
		out.reserve(points.size() * 2);

		auto perp = [](glm::vec2 v) { return glm::vec2(-v.y, v.x); };

		float along = 0.0f;
		for (size_t i = 0; i < points.size(); ++i) {
			glm::vec2 p = points[i];
			glm::vec2 n;
			if (i == 0) {
				glm::vec2 e = glm::normalize(points[1] - p);
				n = perp(e);
			} else if (i == points.size() - 1) {
				glm::vec2 e = glm::normalize(p - points[i - 1]);
				n = perp(e);
				along += glm::length(p - points[i - 1]);
			} else {
				glm::vec2 e1 = glm::normalize(p - points[i - 1]);
				glm::vec2 e2 = glm::normalize(points[i + 1] - p);
				glm::vec2 bis = perp(e1) + perp(e2);
				float bl = glm::length(bis);
				n = (bl > 1e-4f) ? bis / bl : perp(e1);
				// Miter length — cap at 4x half_width for sharp joints.
				float d = glm::dot(n, perp(e1));
				float m = (d > 0.25f) ? (1.0f / d) : 4.0f;
				n *= m;
				along += glm::length(p - points[i - 1]);
			}
			glm::vec2 off = n * half_width;
			out.push_back({ p + off, +1.0f, along });
			out.push_back({ p - off, -1.0f, along });
		}
	}
};

} // namespace civcraft::cellcraft
