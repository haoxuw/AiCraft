// CellCraft — gizmo drawing + hit-testing for the Creature Lab.

#include "CellCraft/client/lab_gizmo.h"

#include <cmath>

namespace civcraft::cellcraft {

namespace {
constexpr float HANDLE_PX = 10.0f;          // corner + delete size
constexpr float ROTATE_STEM_PX = 16.0f;     // distance above the top edge
constexpr float ROTATE_HANDLE_R = 5.0f;

bool in_box(glm::vec2 p, float cx, float cy, float half) {
	return std::fabs(p.x - cx) <= half && std::fabs(p.y - cy) <= half;
}

void append_line(std::vector<ChalkStroke>& out, glm::vec2 a, glm::vec2 b,
                 glm::vec3 color, float hw) {
	ChalkStroke s;
	s.color = color;
	s.half_width = hw;
	s.points = { a, b };
	out.push_back(std::move(s));
}

void append_dashed_box(std::vector<ChalkStroke>& out,
                       float x, float y, float w, float h,
                       glm::vec3 color) {
	const float dash = 8.0f, gap = 5.0f;
	auto seg_along = [&](float x0, float y0, float x1, float y1) {
		float dx = x1 - x0, dy = y1 - y0;
		float len = std::sqrt(dx * dx + dy * dy);
		if (len < 1e-3f) return;
		float nx = dx / len, ny = dy / len;
		float t = 0.0f;
		while (t < len) {
			float t2 = std::min(t + dash, len);
			append_line(out,
				{x0 + nx * t,  y0 + ny * t},
				{x0 + nx * t2, y0 + ny * t2},
				color, 1.2f);
			t = t2 + gap;
		}
	};
	seg_along(x,     y,     x + w, y);
	seg_along(x + w, y,     x + w, y + h);
	seg_along(x + w, y + h, x,     y + h);
	seg_along(x,     y + h, x,     y);
}

void append_corner(std::vector<ChalkStroke>& out, glm::vec2 c, glm::vec3 color) {
	float half = HANDLE_PX * 0.5f;
	append_line(out, {c.x - half, c.y - half}, {c.x + half, c.y - half}, color, 1.6f);
	append_line(out, {c.x + half, c.y - half}, {c.x + half, c.y + half}, color, 1.6f);
	append_line(out, {c.x + half, c.y + half}, {c.x - half, c.y + half}, color, 1.6f);
	append_line(out, {c.x - half, c.y + half}, {c.x - half, c.y - half}, color, 1.6f);
}

void append_x(std::vector<ChalkStroke>& out, glm::vec2 c, glm::vec3 color) {
	float half = HANDLE_PX * 0.5f;
	append_line(out, {c.x - half, c.y - half}, {c.x + half, c.y + half}, color, 1.8f);
	append_line(out, {c.x + half, c.y - half}, {c.x - half, c.y + half}, color, 1.8f);
}

void append_circle(std::vector<ChalkStroke>& out, glm::vec2 c, float r,
                   glm::vec3 color) {
	const int N = 12;
	const float TWO_PI = 6.28318530718f;
	ChalkStroke s;
	s.color = color;
	s.half_width = 1.4f;
	for (int i = 0; i <= N; ++i) {
		float a = TWO_PI * (float)i / (float)N;
		s.points.push_back({c.x + std::cos(a) * r, c.y + std::sin(a) * r});
	}
	out.push_back(std::move(s));
}
} // namespace

void lab_gizmo_append_strokes(float x, float y, float w, float h,
                              std::vector<ChalkStroke>& out) {
	glm::vec3 col(0.95f, 0.90f, 0.45f);
	append_dashed_box(out, x, y, w, h, col);
	// Corners TL, TR, BR, BL.
	append_corner(out, {x,     y    }, col);
	append_corner(out, {x + w, y    }, col);
	append_corner(out, {x + w, y + h}, col);
	append_corner(out, {x,     y + h}, col);
	// Rotation handle: stem + circle above top edge at midpoint.
	glm::vec2 top_mid(x + w * 0.5f, y);
	glm::vec2 rot_c(top_mid.x, top_mid.y - ROTATE_STEM_PX);
	append_line(out, top_mid, rot_c, col, 1.3f);
	append_circle(out, rot_c, ROTATE_HANDLE_R, col);
	// Delete X in top-right corner (offset outside the box).
	glm::vec2 del_c(x + w + HANDLE_PX, y - HANDLE_PX);
	append_x(out, del_c, glm::vec3(1.0f, 0.55f, 0.5f));
}

int lab_gizmo_handle_hit(glm::vec2 p, float x, float y, float w, float h) {
	float half = HANDLE_PX * 0.5f + 2.0f; // small slop
	if (in_box(p, x,     y,     half)) return LAB_GIZMO_CORNER_0;
	if (in_box(p, x + w, y,     half)) return LAB_GIZMO_CORNER_1;
	if (in_box(p, x + w, y + h, half)) return LAB_GIZMO_CORNER_2;
	if (in_box(p, x,     y + h, half)) return LAB_GIZMO_CORNER_3;
	// Rotate handle.
	glm::vec2 rot_c(x + w * 0.5f, y - ROTATE_STEM_PX);
	if (std::fabs(p.x - rot_c.x) <= ROTATE_HANDLE_R + 3.0f
	 && std::fabs(p.y - rot_c.y) <= ROTATE_HANDLE_R + 3.0f)
		return LAB_GIZMO_ROTATE;
	// Delete X.
	glm::vec2 del_c(x + w + HANDLE_PX, y - HANDLE_PX);
	if (in_box(p, del_c.x, del_c.y, half)) return LAB_GIZMO_DELETE;
	return LAB_GIZMO_NONE;
}

} // namespace civcraft::cellcraft
