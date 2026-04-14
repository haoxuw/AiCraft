#include "CellCraft/client/part_render.h"

#include <cmath>

namespace civcraft::cellcraft {

namespace {

glm::vec3 lighten(glm::vec3 c, float k) {
	return glm::clamp(c + glm::vec3(k), glm::vec3(0.0f), glm::vec3(1.0f));
}

void spike(const sim::Part& p, const glm::vec3& col,
           const std::function<glm::vec2(glm::vec2)>& xform,
           float /*scale*/, std::vector<ChalkStroke>& out) {
	// Outward-pointing triangle from anchor along (anchor direction).
	glm::vec2 a = p.anchor_local;
	float len = std::sqrt(a.x * a.x + a.y * a.y);
	glm::vec2 dir = (len > 1e-3f) ? a / len : glm::vec2(1.0f, 0.0f);
	glm::vec2 perp(-dir.y, dir.x);
	glm::vec2 tip  = a + dir * 10.0f;
	glm::vec2 bl = a + perp * 4.0f;
	glm::vec2 br = a - perp * 4.0f;
	ChalkStroke s;
	s.color = lighten(col, 0.2f);
	s.half_width = 2.0f;
	s.points = { xform(bl), xform(tip), xform(br), xform(bl) };
	out.push_back(std::move(s));
}

void teeth(const sim::Part& p, const glm::vec3& col,
           const std::function<glm::vec2(glm::vec2)>& xform,
           std::vector<ChalkStroke>& out) {
	glm::vec2 a = p.anchor_local;
	ChalkStroke s;
	s.color = lighten(col, 0.25f);
	s.half_width = 1.5f;
	// Small W/zigzag.
	for (int i = -2; i <= 2; ++i) {
		glm::vec2 q = a + glm::vec2((float)i * 2.5f, (i & 1) ? 2.5f : -2.5f);
		s.points.push_back(xform(q));
	}
	out.push_back(std::move(s));
}

void flagella(const sim::Part& p, const glm::vec3& col,
              const std::function<glm::vec2(glm::vec2)>& xform,
              float t, std::vector<ChalkStroke>& out) {
	glm::vec2 a = p.anchor_local;
	float len = std::sqrt(a.x * a.x + a.y * a.y);
	glm::vec2 dir = (len > 1e-3f) ? a / len : glm::vec2(-1.0f, 0.0f);
	glm::vec2 perp(-dir.y, dir.x);
	ChalkStroke s;
	s.color = lighten(col, 0.15f);
	s.half_width = 1.5f;
	const int N = 8;
	for (int i = 0; i < N; ++i) {
		float u = (float)i / (float)(N - 1);
		glm::vec2 base = a + dir * (u * 14.0f);
		float wob = std::sin(u * 9.0f + t * 8.0f) * 2.5f;
		s.points.push_back(xform(base + perp * wob));
	}
	out.push_back(std::move(s));
}

void poison(const sim::Part& p, const glm::vec3& /*col*/,
            const std::function<glm::vec2(glm::vec2)>& xform,
            float t, std::vector<ChalkStroke>& out) {
	glm::vec2 a = p.anchor_local;
	// 3 tiny pulsing dots in a triangle.
	glm::vec3 pcol(0.65f, 1.0f, 0.55f);
	for (int i = 0; i < 3; ++i) {
		float ang = 2.094f * (float)i + t * 0.6f;
		glm::vec2 off(std::cos(ang) * 4.0f, std::sin(ang) * 4.0f);
		ChalkStroke s;
		s.color = pcol;
		s.half_width = 1.8f + 0.4f * std::sin(t * 3.0f + (float)i);
		glm::vec2 c = xform(a + off);
		s.points = { c + glm::vec2(-0.8f, 0.0f), c + glm::vec2(0.8f, 0.0f) };
		out.push_back(std::move(s));
	}
}

void armor(const sim::Part& p, const glm::vec3& col,
           const std::function<glm::vec2(glm::vec2)>& xform,
           std::vector<ChalkStroke>& out) {
	glm::vec2 a = p.anchor_local;
	float len = std::sqrt(a.x * a.x + a.y * a.y);
	glm::vec2 dir = (len > 1e-3f) ? a / len : glm::vec2(1.0f, 0.0f);
	glm::vec2 perp(-dir.y, dir.x);
	ChalkStroke s;
	s.color = lighten(col, 0.3f);
	s.half_width = 2.0f;
	// Small arc: 5 points across ±45° on a shield curve.
	const int N = 6;
	for (int i = 0; i < N; ++i) {
		float u = -1.0f + 2.0f * (float)i / (float)(N - 1);
		glm::vec2 q = a + perp * (u * 5.0f) + dir * (1.0f - u * u) * 3.0f;
		s.points.push_back(xform(q));
	}
	out.push_back(std::move(s));
}

void cilia(const sim::Part& p, const glm::vec3& col,
           const std::function<glm::vec2(glm::vec2)>& xform,
           float t, std::vector<ChalkStroke>& out) {
	glm::vec2 a = p.anchor_local;
	float len = std::sqrt(a.x * a.x + a.y * a.y);
	glm::vec2 dir = (len > 1e-3f) ? a / len : glm::vec2(1.0f, 0.0f);
	glm::vec2 perp(-dir.y, dir.x);
	ChalkStroke s;
	s.color = lighten(col, 0.2f);
	s.half_width = 1.3f;
	// Small curl perpendicular to anchor — row of 3 arcs.
	const int N = 7;
	for (int i = 0; i < N; ++i) {
		float u = -1.0f + 2.0f * (float)i / (float)(N - 1);
		float wob = std::sin(u * 6.0f + t * 5.0f) * 1.2f;
		glm::vec2 q = a + perp * (u * 4.0f) + dir * (wob + 1.5f);
		s.points.push_back(xform(q));
	}
	out.push_back(std::move(s));
}

void horn(const sim::Part& p, const glm::vec3& col,
          const std::function<glm::vec2(glm::vec2)>& xform,
          std::vector<ChalkStroke>& out) {
	glm::vec2 a = p.anchor_local;
	float len = std::sqrt(a.x * a.x + a.y * a.y);
	glm::vec2 dir = (len > 1e-3f) ? a / len : glm::vec2(1.0f, 0.0f);
	glm::vec2 perp(-dir.y, dir.x);
	glm::vec2 tip = a + dir * 18.0f;
	glm::vec2 bl  = a + perp * 6.0f;
	glm::vec2 br  = a - perp * 6.0f;
	ChalkStroke s;
	s.color = lighten(col, 0.35f);
	s.half_width = 2.6f;
	s.points = { xform(bl), xform(tip), xform(br), xform(bl) };
	out.push_back(std::move(s));
}

void regen(const sim::Part& p, const glm::vec3& /*col*/,
           const std::function<glm::vec2(glm::vec2)>& xform,
           std::vector<ChalkStroke>& out) {
	glm::vec2 a = p.anchor_local;
	glm::vec3 pcol(0.55f, 1.0f, 0.55f);
	ChalkStroke sh, sv;
	sh.color = pcol; sh.half_width = 1.8f;
	sv.color = pcol; sv.half_width = 1.8f;
	sh.points = { xform(a + glm::vec2(-3.0f, 0.0f)), xform(a + glm::vec2(3.0f, 0.0f)) };
	sv.points = { xform(a + glm::vec2(0.0f, -3.0f)), xform(a + glm::vec2(0.0f, 3.0f)) };
	out.push_back(std::move(sh));
	out.push_back(std::move(sv));
}

void mouth(const sim::Part& p, const glm::vec3& col,
           const std::function<glm::vec2(glm::vec2)>& xform,
           std::vector<ChalkStroke>& out) {
	glm::vec2 a = p.anchor_local;
	float len = std::sqrt(a.x * a.x + a.y * a.y);
	glm::vec2 dir = (len > 1e-3f) ? a / len : glm::vec2(1.0f, 0.0f);
	glm::vec2 perp(-dir.y, dir.x);
	ChalkStroke s;
	s.color = lighten(col, 0.25f);
	s.half_width = 1.8f;
	// Open-mouth "<" — two segments meeting at the back vertex.
	glm::vec2 back = a - dir * 3.0f;
	glm::vec2 top  = a + dir * 3.0f + perp * 5.0f;
	glm::vec2 bot  = a + dir * 3.0f - perp * 5.0f;
	s.points = { xform(top), xform(back), xform(bot) };
	out.push_back(std::move(s));
}

void venom_spike(const sim::Part& p, const glm::vec3& col,
                 const std::function<glm::vec2(glm::vec2)>& xform,
                 std::vector<ChalkStroke>& out) {
	// Same silhouette as SPIKE but tinted green, with two drop dots at the tip.
	glm::vec2 a = p.anchor_local;
	float len = std::sqrt(a.x * a.x + a.y * a.y);
	glm::vec2 dir = (len > 1e-3f) ? a / len : glm::vec2(1.0f, 0.0f);
	glm::vec2 perp(-dir.y, dir.x);
	glm::vec2 tip  = a + dir * 11.0f;
	glm::vec2 bl = a + perp * 4.0f;
	glm::vec2 br = a - perp * 4.0f;
	ChalkStroke s;
	s.color = glm::vec3(0.55f, 0.95f, 0.55f);
	s.half_width = 2.0f;
	s.points = { xform(bl), xform(tip), xform(br), xform(bl) };
	out.push_back(std::move(s));
	// Drops
	for (int i = 0; i < 2; ++i) {
		ChalkStroke d;
		d.color = glm::vec3(0.7f, 1.0f, 0.5f);
		d.half_width = 1.4f;
		glm::vec2 o = tip + dir * 2.0f + perp * (i == 0 ? 1.8f : -1.8f);
		d.points = { xform(o + glm::vec2(-0.6f, 0.0f)), xform(o + glm::vec2(0.6f, 0.0f)) };
		out.push_back(std::move(d));
	}
	(void)col;
}

} // namespace

void appendPartStrokes(const std::vector<sim::Part>& parts,
                       const glm::vec3& body_color,
                       const std::function<glm::vec2(glm::vec2)>& local_to_screen,
                       float px_per_unit,
                       float time_seconds,
                       std::vector<ChalkStroke>& out) {
	(void)px_per_unit;
	for (const auto& p : parts) {
		switch (p.type) {
		case sim::PartType::SPIKE:       spike(p, body_color, local_to_screen, 1.0f, out); break;
		case sim::PartType::TEETH:       teeth(p, body_color, local_to_screen, out); break;
		case sim::PartType::FLAGELLA:    flagella(p, body_color, local_to_screen, time_seconds, out); break;
		case sim::PartType::POISON:      poison(p, body_color, local_to_screen, time_seconds, out); break;
		case sim::PartType::ARMOR:       armor(p, body_color, local_to_screen, out); break;
		case sim::PartType::CILIA:       cilia(p, body_color, local_to_screen, time_seconds, out); break;
		case sim::PartType::HORN:        horn(p, body_color, local_to_screen, out); break;
		case sim::PartType::REGEN:       regen(p, body_color, local_to_screen, out); break;
		case sim::PartType::MOUTH:       mouth(p, body_color, local_to_screen, out); break;
		case sim::PartType::VENOM_SPIKE: venom_spike(p, body_color, local_to_screen, out); break;
		case sim::PartType::PART_TYPE_COUNT: break;
		}
	}
}

} // namespace civcraft::cellcraft
