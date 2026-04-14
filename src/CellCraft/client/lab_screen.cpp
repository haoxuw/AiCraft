// CellCraft — Spore-style Creature Lab implementation.
//
// Layout is recomputed every frame from the framebuffer so it scales with
// window size. Units in the canvas are pixels; anchor_local values on parts
// are kept as pixel-space offsets (matching the rest of the codebase —
// shape vertices in monster-local space are also pixel-scale).
//
// Direct procedural state machine: one update() does input + layout + input
// handling + draw. Keeping all of it here is the simplest map between the
// UX brief and the code.

#include "CellCraft/client/lab_screen.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "CellCraft/client/part_render.h"
#include "CellCraft/sim/monster.h"
#include "CellCraft/sim/part_stats.h"
#include "CellCraft/sim/polygon_util.h"
#include "CellCraft/sim/shape_smooth.h"
#include "CellCraft/sim/shape_validate.h"
#include "CellCraft/sim/symmetric_body.h"
#include "CellCraft/sim/tuning.h"
#include "client/text.h"
#include "client/window.h"

namespace civcraft::cellcraft {

namespace {
constexpr float TWO_PI = 6.28318530718f;
constexpr float DEG_30 = TWO_PI / 12.0f;

// Palette types shown in the left rail.
const sim::PartType kPaletteTypes[] = {
	sim::PartType::SPIKE,       sim::PartType::TEETH,
	sim::PartType::FLAGELLA,    sim::PartType::POISON,
	sim::PartType::ARMOR,       sim::PartType::CILIA,
	sim::PartType::HORN,        sim::PartType::REGEN,
	sim::PartType::MOUTH,       sim::PartType::VENOM_SPIKE,
	sim::PartType::EYES,
};
constexpr int kPaletteCount = (int)(sizeof(kPaletteTypes) / sizeof(kPaletteTypes[0]));

glm::vec2 rotate_v(glm::vec2 v, float rad) {
	float c = std::cos(rad), s = std::sin(rad);
	return glm::vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}
} // namespace

void LabScreen::init(Window* w, ChalkRenderer* r, TextRenderer* t) {
	window_ = w;
	renderer_ = r;
	text_ = t;
	reset();
}

void LabScreen::reset() {
	mode_ = Mode::DRAWING;
	strokes_.clear();
	live_stroke_ = ChalkStroke{};
	drawing_ = false;
	smoothed_local_.clear();
	validated_local_.clear();
	parts_.clear();
	selected_palette_ = -1;
	ghost_rotation_ = 0.0f;
	mirror_mode_ = false;
	biomass_budget_ = 40.0f;
	highlight_type_ = -1;
	highlight_t_ = 0.0f;
	flashes_.clear();
	symmetric_draw_ = true;
	used_symmetric_ = false;
	status_ = "Left-drag to draw your body. FINALIZE when done.";
	status_color_ = glm::vec3(0.85f);
	time_acc_ = 0.0f;
	prev_right_ = false;
	body_scale_ = 1.0f;
}

LabScreen::Layout LabScreen::compute_layout_() const {
	Layout l;
	glfwGetFramebufferSize(window_->handle(), &l.fw, &l.fh);
	l.left_w   = (float)l.fw * 0.18f;
	l.right_w  = (float)l.fw * 0.18f;
	l.left_x   = 0.0f;
	l.right_x  = (float)l.fw - l.right_w;
	l.canvas_x = l.left_w;
	l.canvas_w = (float)l.fw - l.left_w - l.right_w;
	l.top_bar_h = (float)l.fh * 0.08f;
	l.bottom_bar_h = (float)l.fh * 0.10f;
	l.canvas_cx = l.canvas_x + l.canvas_w * 0.5f;
	l.canvas_cy = l.top_bar_h + ((float)l.fh - l.top_bar_h - l.bottom_bar_h) * 0.5f;
	return l;
}

glm::vec2 LabScreen::px_to_ndc_(glm::vec2 px) const {
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
	float xn = (px.x / (float)ww) * 2.0f - 1.0f;
	float yn = 1.0f - (px.y / (float)wh) * 2.0f;
	return glm::vec2(xn, yn);
}

float LabScreen::parts_cost_total_() const {
	float sum = 0.0f;
	for (auto& p : parts_) sum += sim::part_cost(p.type);
	return sum;
}

void LabScreen::finalize_body_() {
	std::vector<std::vector<glm::vec2>> pool;
	for (auto& s : strokes_) if (s.points.size() >= 2) pool.push_back(s.points);
	if (pool.empty()) {
		status_ = "draw at least one stroke first";
		status_color_ = glm::vec3(1.0f, 0.5f, 0.55f);
		return;
	}
	std::vector<glm::vec2> smoothed_px;
	if (symmetric_draw_) {
		Layout ly = compute_layout_();
		smoothed_px = sim::buildSymmetricBody(pool, ly.canvas_cx);
		used_symmetric_ = true;
	} else {
		smoothed_px = sim::smooth_body(pool, 48);
		used_symmetric_ = false;
	}
	if (smoothed_px.size() < 3) {
		status_ = "strokes too small — try again";
		status_color_ = glm::vec3(1.0f, 0.5f, 0.55f);
		return;
	}

	// Compute centroid in pixel-space; core is ALWAYS the centroid.
	glm::vec2 centroid(0.0f);
	for (auto& p : smoothed_px) centroid += p;
	centroid /= (float)smoothed_px.size();

	std::vector<glm::vec2> poly = smoothed_px;
	auto res = sim::validate_shape(poly, centroid);
	if (res.code != sim::ShapeValidation::OK) {
		status_ = std::string("shape: ") + res.message;
		status_color_ = glm::vec3(1.0f, 0.5f, 0.55f);
		return;
	}

	// Convert to local space (y-up) with centroid at origin.
	smoothed_local_.clear();
	smoothed_local_.reserve(poly.size());
	for (auto& p : poly) {
		smoothed_local_.push_back(glm::vec2(p.x - centroid.x, -(p.y - centroid.y)));
	}
	validated_local_ = smoothed_local_;

	// Pick display scale so the shape fills ~70% of the canvas (capped to 1).
	Layout l = compute_layout_();
	float content_h = (float)l.fh - l.top_bar_h - l.bottom_bar_h;
	float canvas_min = std::min(l.canvas_w, content_h);
	float max_r = 0.0f;
	for (auto& v : smoothed_local_) max_r = std::max(max_r, glm::length(v));
	if (max_r < 1e-3f) max_r = 1.0f;
	body_scale_ = std::min(1.0f, (canvas_min * 0.40f) / max_r);
	// But clamp to at least 1.0 so tiny bodies still get drawn at 1:1.
	body_scale_ = std::max(body_scale_, 1.0f);

	mode_ = Mode::ASSEMBLING;
	if (used_symmetric_) mirror_mode_ = true;
	status_ = "Pick a part (left rail), click on the body to place. R rotates, M mirrors.";
	status_color_ = glm::vec3(0.85f);
}

glm::vec2 LabScreen::local_to_canvas_px_(glm::vec2 local, const Layout& l, float wob) const {
	glm::vec2 r = rotate_v(local * body_scale_, wob);
	// y-up in local → screen y grows downward.
	return glm::vec2(l.canvas_cx + r.x, l.canvas_cy - r.y);
}

glm::vec2 LabScreen::mouse_to_local_(glm::vec2 mpx, const Layout& l, float wob) const {
	glm::vec2 rel(mpx.x - l.canvas_cx, -(mpx.y - l.canvas_cy));
	glm::vec2 r = rotate_v(rel, -wob);
	return r / body_scale_;
}

void LabScreen::try_place_part_(glm::vec2 mpx, const Layout& l) {
	if (selected_palette_ < 0) return;
	sim::PartType pt = (sim::PartType)selected_palette_;
	// Convert to local (ignore wobble for placement — simpler for the user).
	glm::vec2 local = mouse_to_local_(mpx, l, 0.0f);

	// Must be inside polygon (or very near it — 12px margin in local units).
	if (!sim::point_in_polygon(local, smoothed_local_)) {
		float best = 1e9f;
		const size_t n = smoothed_local_.size();
		for (size_t i = 0; i < n; ++i) {
			glm::vec2 a = smoothed_local_[i];
			glm::vec2 b = smoothed_local_[(i + 1) % n];
			glm::vec2 ab = b - a;
			float t = (glm::length(ab) > 1e-3f) ? glm::dot(local - a, ab) / glm::dot(ab, ab) : 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			float d = glm::length(local - (a + ab * t));
			if (d < best) best = d;
		}
		if (best > 12.0f) {
			status_ = "click on the body";
			status_color_ = glm::vec3(1.0f, 0.7f, 0.4f);
			return;
		}
	}

	float cost = sim::part_cost(pt);
	int copies = mirror_mode_ ? 2 : 1;
	if (parts_cost_total_() + cost * copies > biomass_budget_) {
		status_ = "not enough biomass";
		status_color_ = glm::vec3(1.0f, 0.5f, 0.55f);
		return;
	}
	// Stack-cap enforcement — count existing of this type and refuse past cap.
	auto cap_for = [](sim::PartType t) -> int {
		switch (t) {
		case sim::PartType::FLAGELLA:    return sim::PART_FLAGELLA_MAX_STACK;
		case sim::PartType::ARMOR:       return sim::PART_ARMOR_MAX_STACK;
		case sim::PartType::CILIA:       return sim::PART_CILIA_MAX_STACK;
		case sim::PartType::REGEN:       return sim::PART_REGEN_MAX_STACK;
		case sim::PartType::MOUTH:       return sim::PART_MOUTH_MAX_STACK;
		case sim::PartType::HORN:        return sim::PART_HORN_MAX_STACK;
		case sim::PartType::VENOM_SPIKE: return sim::PART_VENOM_MAX_STACK;
		case sim::PartType::EYES:        return sim::PART_EYES_MAX_STACK;
		default: return 99;
		}
	};
	int have = 0;
	for (auto& p : parts_) if (p.type == pt) ++have;
	if (have + copies > cap_for(pt)) {
		status_ = "stack cap reached for that part";
		status_color_ = glm::vec3(1.0f, 0.5f, 0.55f);
		return;
	}

	sim::Part np;
	np.type = pt;
	np.anchor_local = local;
	np.orientation = ghost_rotation_;
	parts_.push_back(np);
	Flash f{ mpx, 0.35f }; flashes_.push_back(f);

	if (mirror_mode_) {
		sim::Part np2 = np;
		np2.anchor_local = glm::vec2(local.x, -local.y);
		np2.orientation = -ghost_rotation_;
		parts_.push_back(np2);
		Flash f2{ local_to_canvas_px_(np2.anchor_local, l, 0.0f), 0.35f };
		flashes_.push_back(f2);
	}
	status_ = "placed " + std::string(sim::part_name(pt));
	status_color_ = glm::vec3(0.7f, 1.0f, 0.7f);
}

void LabScreen::remove_part_at_(glm::vec2 mpx, const Layout& l) {
	if (parts_.empty()) return;
	float best = 28.0f;
	int best_i = -1;
	for (size_t i = 0; i < parts_.size(); ++i) {
		glm::vec2 sp = local_to_canvas_px_(parts_[i].anchor_local, l, 0.0f);
		float d = glm::length(sp - mpx);
		if (d < best) { best = d; best_i = (int)i; }
	}
	if (best_i >= 0) {
		parts_.erase(parts_.begin() + best_i);
		status_ = "removed";
		status_color_ = glm::vec3(0.85f);
	}
}

void LabScreen::draw_chalk_grid_(std::vector<ChalkStroke>& out, const Layout& l) {
	// Faint dotted chalk circles every 40px around the canvas center.
	for (int r = 40; r <= 400; r += 40) {
		ChalkStroke s;
		s.color = glm::vec3(0.25f, 0.28f, 0.28f);
		s.half_width = 0.8f;
		const int N = 48;
		for (int i = 0; i <= N; i += 2) { // skip every other to look dotted
			float a = TWO_PI * (float)i / (float)N;
			glm::vec2 p(l.canvas_cx + std::cos(a) * (float)r, l.canvas_cy + std::sin(a) * (float)r);
			s.points.push_back(p);
		}
		if (s.points.size() >= 2) out.push_back(std::move(s));
	}
}

void LabScreen::draw_mirror_line_(std::vector<ChalkStroke>& out, const Layout& l) {
	bool draw_mode_axis = (mode_ == Mode::DRAWING) && symmetric_draw_;
	bool asm_mode_axis  = (mode_ == Mode::ASSEMBLING) && mirror_mode_;
	if (!draw_mode_axis && !asm_mode_axis) return;
	glm::vec3 col = draw_mode_axis
		? glm::vec3(0.95f, 0.9f, 0.55f)
		: glm::vec3(0.9f, 0.85f, 0.3f);
	// Dashed vertical line through canvas center.
	for (float y = (float)l.top_bar_h + 8.0f; y < (float)l.fh - l.bottom_bar_h - 8.0f; y += 16.0f) {
		ChalkStroke seg;
		seg.color = col;
		seg.half_width = 1.2f;
		seg.points = { {l.canvas_cx, y}, {l.canvas_cx, y + 8.0f} };
		out.push_back(std::move(seg));
	}
}

void LabScreen::draw_body_and_parts_(std::vector<ChalkStroke>& out, const Layout& l,
                                     float time_s, bool include_ghost) {
	float wob = (mode_ == Mode::ASSEMBLING)
		? std::sin(time_s * TWO_PI * 0.5f) * (5.0f * 3.14159f / 180.0f)
		: 0.0f;

	if (mode_ == Mode::DRAWING) {
		// Show live + finalized strokes.
		for (auto& s : strokes_) if (s.points.size() >= 2) out.push_back(s);
		if (!live_stroke_.points.empty()) out.push_back(live_stroke_);

		// Faint template ellipse if no strokes drawn yet.
		if (strokes_.empty() && live_stroke_.points.empty()) {
			ChalkStroke tmpl;
			tmpl.color = glm::vec3(0.4f, 0.4f, 0.4f);
			tmpl.half_width = 1.0f;
			float a_s = std::min(l.canvas_w, (float)l.fh - l.top_bar_h - l.bottom_bar_h) * 0.28f;
			float b_s = a_s * 0.7f;
			const int N = 48;
			for (int i = 0; i <= N; i += 2) {
				float a = TWO_PI * (float)i / (float)N;
				tmpl.points.push_back({ l.canvas_cx + std::cos(a) * a_s,
				                        l.canvas_cy + std::sin(a) * b_s });
			}
			out.push_back(std::move(tmpl));
		}
		return;
	}

	// ASSEMBLING — draw smoothed body + parts in canvas space.
	ChalkStroke body;
	body.color = color_;
	body.half_width = 4.0f;
	for (auto& v : smoothed_local_) body.points.push_back(local_to_canvas_px_(v, l, wob));
	if (!body.points.empty()) body.points.push_back(body.points.front());
	out.push_back(std::move(body));

	// Core crosshair (fixed at canvas center — cannot be moved).
	ChalkStroke cx1, cx2;
	cx1.color = glm::vec3(1.0f); cx1.half_width = 1.8f; cx2 = cx1;
	cx1.points = { {l.canvas_cx - 7.0f, l.canvas_cy}, {l.canvas_cx + 7.0f, l.canvas_cy} };
	cx2.points = { {l.canvas_cx, l.canvas_cy - 7.0f}, {l.canvas_cx, l.canvas_cy + 7.0f} };
	out.push_back(std::move(cx1));
	out.push_back(std::move(cx2));

	// Parts — slightly larger scale in the editor so the user sees them.
	const float PART_EDIT_SCALE = 1.5f;
	auto to_canvas = [&](glm::vec2 local) {
		return local_to_canvas_px_(local, l, wob);
	};
	// Build a scaled copy of parts so glyphs appear larger.
	std::vector<sim::Part> scaled = parts_;
	for (auto& p : scaled) p.anchor_local *= 1.0f; // anchors stay; we scale glyphs via PART_EDIT_SCALE below
	// To "inflate" glyphs, we apply a local-space scaling inside the xform.
	auto to_canvas_glyph = [&](glm::vec2 local) {
		glm::vec2 anchor;  // find nearest anchor for scaling pivot — simplest: scale about origin
		(void)anchor;
		return local_to_canvas_px_(local * PART_EDIT_SCALE, l, wob);
	};
	// But we need glyph scaling around the part's anchor, not the origin. Simpler
	// path: draw each part individually so we can supply a per-part anchor pivot.
	for (auto& p : parts_) {
		std::vector<sim::Part> single = { p };
		auto per_part = [&](glm::vec2 local) {
			glm::vec2 rel = local - p.anchor_local;
			glm::vec2 inflated = p.anchor_local + rel * PART_EDIT_SCALE;
			return local_to_canvas_px_(inflated, l, wob);
		};
		appendPartStrokes(single, color_, per_part, 1.0f, time_s, out);
	}

	// Ghost preview for selected palette part.
	if (include_ghost && selected_palette_ >= 0 && mode_ == Mode::ASSEMBLING) {
		// Ghost is anchored at current mouse (in local coords).
		// The app supplies mouse via the last update — we cached it as last_mouse_px_;
		// but to avoid extra members we piggy-back on flashes_ approach: nothing cached.
		// We'll emit ghost in update() instead, since that's where we have the mouse.
	}
}

void LabScreen::draw_highlight_pulses_(std::vector<ChalkStroke>& out, const Layout& l, float time_s) {
	if (highlight_type_ < 0 || highlight_t_ <= 0.0f) return;
	float k = std::min(1.0f, highlight_t_);
	float wob = std::sin(time_s * TWO_PI * 0.5f) * (5.0f * 3.14159f / 180.0f);
	for (auto& p : parts_) {
		if ((int)p.type != highlight_type_) continue;
		glm::vec2 c = local_to_canvas_px_(p.anchor_local, l, wob);
		ChalkStroke ring;
		ring.color = glm::vec3(1.0f, 0.95f, 0.4f) * k;
		ring.half_width = 1.6f;
		const int N = 24;
		for (int i = 0; i <= N; ++i) {
			float a = TWO_PI * (float)i / (float)N;
			ring.points.push_back({ c.x + std::cos(a) * 18.0f, c.y + std::sin(a) * 18.0f });
		}
		out.push_back(std::move(ring));
	}
}

void LabScreen::draw_top_bar_() {
	float aspect = window_->aspectRatio();
	text_->drawTitle("CREATURE LAB", -0.22f, 0.85f, 1.3f,
		glm::vec4(1.0f, 1.0f, 0.95f, 1.0f), aspect);
	text_->drawText("MY BEAST", -0.05f, 0.78f, 0.9f,
		glm::vec4(0.8f, 0.8f, 0.8f, 1.0f), aspect);
}

void LabScreen::draw_mode_indicator_(const Layout& l) {
	float aspect = window_->aspectRatio();
	const char* label = (mode_ == Mode::DRAWING) ? "DRAWING" : "ASSEMBLING";
	glm::vec4 col = (mode_ == Mode::DRAWING)
		? glm::vec4(0.55f, 0.9f, 1.0f, 1.0f)
		: glm::vec4(0.9f, 1.0f, 0.6f, 1.0f);
	// Position just below top bar — use NDC.
	float ndc_x = (l.canvas_cx / (float)l.fw) * 2.0f - 1.0f - 0.07f;
	float ndc_y = 1.0f - (l.top_bar_h + 24.0f) / (float)l.fh * 2.0f;
	text_->drawText(label, ndc_x, ndc_y, 1.1f, col, aspect);
	// Status line below it.
	text_->drawText(status_, ndc_x - 0.04f, ndc_y - 0.05f, 0.8f,
		glm::vec4(status_color_, 1.0f), aspect);
}

bool LabScreen::pixel_button_(float x, float y, float w, float h,
                              const char* label, bool enabled,
                              const LabInput& in) {
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
	// Convert pixel rect to NDC for drawing with TextRenderer.
	float nx = (x / (float)ww) * 2.0f - 1.0f;
	float ny = 1.0f - ((y + h) / (float)wh) * 2.0f;
	float nw = (w / (float)ww) * 2.0f;
	float nh = (h / (float)wh) * 2.0f;
	bool hover = pt_in_rect_(in.mouse_px, x, y, w, h) && enabled;
	glm::vec4 bg = enabled
		? (hover ? glm::vec4(0.22f, 0.22f, 0.25f, 0.9f) : glm::vec4(0.10f, 0.10f, 0.12f, 0.8f))
		: glm::vec4(0.06f, 0.06f, 0.06f, 0.7f);
	text_->drawRect(nx, ny, nw, nh, bg);
	glm::vec4 edge(0.85f, 0.85f, 0.80f, enabled ? 0.95f : 0.4f);
	float t = 0.003f;
	text_->drawRect(nx, ny, nw, t, edge);
	text_->drawRect(nx, ny + nh - t, nw, t, edge);
	text_->drawRect(nx, ny, t, nh, edge);
	text_->drawRect(nx + nw - t, ny, t, nh, edge);
	// label
	float label_w = (float)std::strlen(label) * 0.018f * 1.0f;
	float tx = nx + (nw - label_w) * 0.5f;
	float ty = ny + (nh - 0.032f * 1.0f) * 0.5f;
	glm::vec4 col = enabled ? glm::vec4(0.97f, 0.97f, 0.92f, 1.0f)
	                        : glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
	text_->drawText(label, tx, ty, 1.0f, col, window_->aspectRatio());
	return hover && in.mouse_left_click;
}

void LabScreen::draw_palette_(const Layout& l, const LabInput& in, float time_s) {
	float aspect = window_->aspectRatio();
	// Header.
	{
		int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
		float nx = (l.left_x / (float)ww) * 2.0f - 1.0f + 0.01f;
		float ny = 1.0f - (l.top_bar_h + 16.0f) / (float)wh * 2.0f;
		text_->drawText("PARTS", nx, ny, 1.1f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), aspect);

		// Biomass bar just below header.
		float cost = parts_cost_total_();
		float frac = std::min(1.0f, cost / std::max(1.0f, biomass_budget_));
		float bar_ny = ny - 0.05f;
		text_->drawRect(nx, bar_ny, 0.16f, 0.014f, glm::vec4(0.15f, 0.15f, 0.15f, 0.8f));
		glm::vec4 bmc = (cost > biomass_budget_)
			? glm::vec4(1.0f, 0.4f, 0.4f, 1.0f) : glm::vec4(0.7f, 0.9f, 0.4f, 1.0f);
		text_->drawRect(nx, bar_ny, 0.16f * frac, 0.014f, bmc);
		char buf[48];
		std::snprintf(buf, sizeof(buf), "%.0f / %.0f bm", cost, biomass_budget_);
		text_->drawText(buf, nx, bar_ny - 0.04f, 0.75f,
			glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), aspect);
	}

	// Rows.
	float row_h = ((float)l.fh - l.top_bar_h - l.bottom_bar_h - 140.0f) / (float)kPaletteCount;
	row_h = std::max(40.0f, std::min(row_h, 60.0f));
	float row_x = l.left_x + 6.0f;
	float row_w = l.left_w - 12.0f;
	float y0 = l.top_bar_h + 120.0f;
	for (int i = 0; i < kPaletteCount; ++i) {
		float ry = y0 + (float)i * row_h;
		sim::PartType pt = kPaletteTypes[i];
		bool is_selected = ((int)pt == selected_palette_);
		bool hover = pt_in_rect_(in.mouse_px, row_x, ry, row_w, row_h - 4.0f);
		// background
		{
			int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
			float nx = (row_x / (float)ww) * 2.0f - 1.0f;
			float ny = 1.0f - ((ry + row_h - 4.0f) / (float)wh) * 2.0f;
			float nw = (row_w / (float)ww) * 2.0f;
			float nh = ((row_h - 4.0f) / (float)wh) * 2.0f;
			glm::vec4 bg = is_selected ? glm::vec4(0.25f, 0.35f, 0.18f, 0.9f)
			             : hover       ? glm::vec4(0.18f, 0.18f, 0.22f, 0.9f)
			                           : glm::vec4(0.08f, 0.08f, 0.10f, 0.8f);
			text_->drawRect(nx, ny, nw, nh, bg);
			// label
			char lbl[64];
			std::snprintf(lbl, sizeof(lbl), "%s  %.0fbm", sim::part_name(pt), sim::part_cost(pt));
			text_->drawText(lbl, nx + 0.06f, ny + nh - 0.04f, 0.85f,
				glm::vec4(0.97f, 0.97f, 0.92f, 1.0f), aspect);
			text_->drawText(sim::part_desc(pt), nx + 0.06f, ny + 0.01f, 0.65f,
				glm::vec4(0.7f, 0.7f, 0.7f, 1.0f), aspect);
		}
		// Icon on the left — render a single glyph with anchor at (0,0) shifted into the row.
		{
			std::vector<ChalkStroke> glyph_strokes;
			sim::Part single;
			single.type = pt;
			single.anchor_local = glm::vec2(0.0f);
			single.orientation = 0.0f;
			glm::vec2 icon_pos(row_x + 22.0f, ry + row_h * 0.45f);
			auto xform = [&](glm::vec2 v) { return icon_pos + glm::vec2(v.x, -v.y) * 1.6f; };
			appendPartStrokes({single}, color_, xform, 1.0f, time_s, glyph_strokes);
			if (!glyph_strokes.empty())
				renderer_->drawStrokes(glyph_strokes, nullptr, l.fw, l.fh);
		}

		if (hover && in.mouse_left_click && mode_ == Mode::ASSEMBLING) {
			selected_palette_ = is_selected ? -1 : (int)pt;
		}
	}
}

void LabScreen::draw_loadout_(const Layout& l, const LabInput& in) {
	float aspect = window_->aspectRatio();
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
	float nx = (l.right_x / (float)ww) * 2.0f - 1.0f + 0.01f;
	float ny = 1.0f - (l.top_bar_h + 16.0f) / (float)wh * 2.0f;
	text_->drawText("LOADOUT", nx, ny, 1.1f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), aspect);

	// Aggregate counts by type.
	int counts[(int)sim::PartType::PART_TYPE_COUNT] = {0};
	for (auto& p : parts_) {
		int idx = (int)p.type;
		if (idx >= 0 && idx < (int)sim::PartType::PART_TYPE_COUNT) counts[idx]++;
	}

	float row_y = l.top_bar_h + 56.0f;
	float row_h = 28.0f;
	float row_x = l.right_x + 6.0f;
	float row_w = l.right_w - 12.0f;
	for (int i = 0; i < kPaletteCount; ++i) {
		sim::PartType pt = kPaletteTypes[i];
		int c = counts[(int)pt];
		if (c <= 0) continue;
		float ry = row_y;
		row_y += row_h;
		bool hover = pt_in_rect_(in.mouse_px, row_x, ry, row_w, row_h - 4.0f);
		{
			float bx = (row_x / (float)ww) * 2.0f - 1.0f;
			float by = 1.0f - ((ry + row_h - 4.0f) / (float)wh) * 2.0f;
			float bw = (row_w / (float)ww) * 2.0f;
			float bh = ((row_h - 4.0f) / (float)wh) * 2.0f;
			glm::vec4 bg = hover ? glm::vec4(0.22f, 0.22f, 0.25f, 0.9f)
			                     : glm::vec4(0.08f, 0.08f, 0.10f, 0.8f);
			text_->drawRect(bx, by, bw, bh, bg);
			char lbl[48];
			std::snprintf(lbl, sizeof(lbl), "%s x%d", sim::part_name(pt), c);
			text_->drawText(lbl, bx + 0.01f, by + bh * 0.25f, 0.9f,
				glm::vec4(0.97f, 0.97f, 0.92f, 1.0f), aspect);
		}
		if (hover && in.mouse_left_click) {
			highlight_type_ = (int)pt;
			highlight_t_ = 1.0f;
		}
		if (hover && in.mouse_right_click) {
			// Remove ALL of this type.
			parts_.erase(std::remove_if(parts_.begin(), parts_.end(),
				[pt](const sim::Part& pp){ return pp.type == pt; }), parts_.end());
		}
	}
}

void LabScreen::draw_stats_readout_(const Layout& l) {
	if (mode_ == Mode::DRAWING) return;
	// Build a probe monster to read derived stats.
	sim::Monster probe;
	probe.shape = validated_local_;
	probe.biomass = 25.0f;
	probe.parts = parts_;
	probe.refresh_stats();
	char buf[160];
	std::snprintf(buf, sizeof(buf), "SPEED %.0f   TURN %.1f   HP %.0f   DMGx%.2f   REGEN %.1f/s",
		probe.move_speed, probe.turn_speed, probe.hp_max,
		probe.part_effect.damage_mult, probe.part_effect.regen_hps);
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
	float nx = (l.canvas_cx / (float)ww) * 2.0f - 1.0f - 0.35f;
	float ny = 1.0f - (l.top_bar_h + 56.0f) / (float)wh * 2.0f;
	text_->drawText(buf, nx, ny, 0.85f, glm::vec4(0.9f, 0.95f, 0.9f, 1.0f),
		window_->aspectRatio());
}

void LabScreen::draw_canvas_buttons_(const Layout& l, const LabInput& in, LabOutcome& outc) {
	// Row of buttons at the bottom of the canvas.
	float by = (float)l.fh - l.bottom_bar_h + 8.0f;
	float bh = l.bottom_bar_h - 16.0f;
	float bw = std::min(180.0f, l.canvas_w * 0.18f);
	float gap = 10.0f;

	if (mode_ == Mode::DRAWING) {
		float total = bw * 4.0f + gap * 3.0f;
		float bx = l.canvas_cx - total * 0.5f;
		const char* sym_lbl = symmetric_draw_ ? "SYMMETRIC [ON]" : "SYMMETRIC [OFF]";
		if (pixel_button_(bx + 0 * (bw + gap), by, bw, bh, sym_lbl, true, in)) {
			symmetric_draw_ = !symmetric_draw_;
			// Changing mode invalidates existing freeform strokes if they
			// violate the new constraint, but we keep them (user can CLEAR).
			status_ = symmetric_draw_
				? "SYMMETRIC: draw on the right half; body will mirror."
				: "FREEFORM: draw anywhere.";
			status_color_ = glm::vec3(0.85f);
		}
		if (pixel_button_(bx + 1 * (bw + gap), by, bw, bh, "UNDO", !strokes_.empty(), in)) {
			if (!strokes_.empty()) strokes_.pop_back();
		}
		if (pixel_button_(bx + 2 * (bw + gap), by, bw, bh, "CLEAR", !strokes_.empty(), in)) {
			strokes_.clear();
			live_stroke_ = ChalkStroke{};
			drawing_ = false;
		}
		if (pixel_button_(bx + 3 * (bw + gap), by, bw, bh, "FINALIZE", !strokes_.empty(), in)) {
			finalize_body_();
		}
	} else {
		float total = bw * 3.0f + gap * 2.0f;
		float bx = l.canvas_cx - total * 0.5f;
		if (pixel_button_(bx + 0 * (bw + gap), by, bw, bh, "BACK TO BODY", true, in)) {
			mode_ = Mode::DRAWING;
			status_ = "Edit your strokes. FINALIZE again to rebuild.";
			status_color_ = glm::vec3(0.85f);
		}
		if (pixel_button_(bx + 1 * (bw + gap), by, bw, bh, "CLEAR PARTS", !parts_.empty(), in)) {
			parts_.clear();
		}
		bool use_ok = parts_cost_total_() <= biomass_budget_ && !validated_local_.empty();
		if (pixel_button_(bx + 2 * (bw + gap), by, bw, bh, "USE THIS MONSTER", use_ok, in)) {
			outc = LabOutcome::USE;
		}
	}

	// Menu button, top-right of top bar.
	float mx = (float)l.fw - 130.0f;
	float my = 12.0f;
	if (pixel_button_(mx, my, 120.0f, 32.0f, "MENU", true, in)) {
		outc = LabOutcome::BACK;
	}
}

LabOutcome LabScreen::update(float dt, const LabInput& in) {
	time_acc_ += dt;
	Layout l = compute_layout_();

	LabOutcome outc = LabOutcome::NONE;

	// --- Keyboard (mode-independent first).
	for (int key : in.keys_pressed) {
		if (key == GLFW_KEY_ESCAPE) {
			if (selected_palette_ >= 0) selected_palette_ = -1;
			else outc = LabOutcome::BACK;
		}
	}
	if (mode_ == Mode::ASSEMBLING) {
		for (int key : in.keys_pressed) {
			if (key == GLFW_KEY_R) ghost_rotation_ = std::fmod(ghost_rotation_ + DEG_30, TWO_PI);
			if (key == GLFW_KEY_M) mirror_mode_ = !mirror_mode_;
		}
	}

	// --- Mouse: drawing strokes (DRAWING mode, left-button held inside canvas).
	bool in_canvas = (in.mouse_px.x >= l.canvas_x
	               && in.mouse_px.x <= l.canvas_x + l.canvas_w
	               && in.mouse_px.y >= l.top_bar_h
	               && in.mouse_px.y <= (float)l.fh - l.bottom_bar_h);

	if (mode_ == Mode::DRAWING) {
		// In symmetric mode, clamp cursor to right half of canvas; snap points
		// near the axis to the axis so endpoints lie exactly on it.
		auto sample_px = [&](glm::vec2 mpx) {
			if (symmetric_draw_) {
				if (mpx.x < l.canvas_cx) mpx.x = l.canvas_cx;
				if (mpx.x < l.canvas_cx + 6.0f) mpx.x = l.canvas_cx;
			}
			return mpx;
		};
		if (in.mouse_left_click && in_canvas) {
			drawing_ = true;
			live_stroke_ = ChalkStroke{};
			live_stroke_.color = color_;
			live_stroke_.half_width = 3.0f;
			glm::vec2 p0 = sample_px(in.mouse_px);
			if (symmetric_draw_) p0.x = l.canvas_cx; // start on axis
			live_stroke_.points.push_back(p0);
		}
		if (drawing_ && in.mouse_left_down) {
			glm::vec2 p = sample_px(in.mouse_px);
			if (live_stroke_.points.empty()
			    || glm::length(p - live_stroke_.points.back()) >= 2.0f) {
				live_stroke_.points.push_back(p);
			}
		}
		if (!in.mouse_left_down && drawing_) {
			drawing_ = false;
			if (symmetric_draw_ && live_stroke_.points.size() >= 2) {
				live_stroke_.points.back().x = l.canvas_cx; // end on axis
			}
			if (live_stroke_.points.size() >= 2) {
				live_stroke_.simplify(1.5f);
				strokes_.push_back(live_stroke_);
			}
			live_stroke_ = ChalkStroke{};
		}
	} else {
		// ASSEMBLING mode click-to-place / right-click to remove (in canvas).
		if (in.mouse_left_click && in_canvas && selected_palette_ >= 0) {
			try_place_part_(in.mouse_px, l);
		}
		// Edge-triggered right-click remove.
		bool rc_edge = in.mouse_right_down && !prev_right_;
		if (rc_edge && in_canvas) {
			remove_part_at_(in.mouse_px, l);
		}
	}
	prev_right_ = in.mouse_right_down;

	// --- Tick ephemeral effects.
	highlight_t_ -= dt;
	for (auto& f : flashes_) f.t_left -= dt;
	flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(),
		[](const Flash& f){ return f.t_left <= 0.0f; }), flashes_.end());

	// --- Render pass (single big drawStrokes batch for the chalk layer).
	std::vector<ChalkStroke> strokes;
	draw_chalk_grid_(strokes, l);
	draw_mirror_line_(strokes, l);
	draw_body_and_parts_(strokes, l, time_acc_, true);
	draw_highlight_pulses_(strokes, l, time_acc_);

	// Ghost preview on cursor when a palette part is selected (ASSEMBLING).
	if (mode_ == Mode::ASSEMBLING && selected_palette_ >= 0 && in_canvas) {
		sim::Part ghost;
		ghost.type = (sim::PartType)selected_palette_;
		// Anchor: the mouse pixel → local (ignore wobble), then we scale glyph about that anchor.
		glm::vec2 local = mouse_to_local_(in.mouse_px, l, 0.0f);
		ghost.anchor_local = local;
		ghost.orientation = ghost_rotation_;
		std::vector<sim::Part> single = { ghost };
		auto xform = [&](glm::vec2 lv) {
			glm::vec2 rel = lv - local;
			// rotate glyph around anchor by ghost_rotation_
			rel = rotate_v(rel, ghost_rotation_);
			glm::vec2 inflated = local + rel * 1.5f;
			return local_to_canvas_px_(inflated, l, 0.0f);
		};
		std::vector<ChalkStroke> gh;
		appendPartStrokes(single, glm::vec3(0.9f, 0.9f, 0.6f), xform, 1.0f, time_acc_, gh);
		for (auto& s : gh) { s.half_width *= 0.6f; s.color *= 0.7f; strokes.push_back(std::move(s)); }
		// And a mirrored ghost if mirror_mode_.
		if (mirror_mode_) {
			glm::vec2 local_m(local.x, -local.y);
			auto xform2 = [&](glm::vec2 lv) {
				glm::vec2 rel = lv - local_m;
				rel = rotate_v(rel, -ghost_rotation_);
				glm::vec2 inflated = local_m + rel * 1.5f;
				return local_to_canvas_px_(inflated, l, 0.0f);
			};
			std::vector<sim::Part> single2 = single;
			single2[0].anchor_local = local_m;
			std::vector<ChalkStroke> gh2;
			appendPartStrokes(single2, glm::vec3(0.9f, 0.9f, 0.6f), xform2, 1.0f, time_acc_, gh2);
			for (auto& s : gh2) { s.half_width *= 0.6f; s.color *= 0.5f; strokes.push_back(std::move(s)); }
		}
	}

	// Flash bursts.
	for (auto& f : flashes_) {
		float k = std::max(0.0f, f.t_left / 0.35f);
		ChalkStroke s;
		s.color = glm::vec3(1.0f, 0.95f, 0.4f) * k;
		s.half_width = 3.0f * k + 0.5f;
		for (int i = 0; i < 6; ++i) {
			float a = TWO_PI * (float)i / 6.0f;
			ChalkStroke seg;
			seg.color = s.color; seg.half_width = s.half_width;
			float r0 = 6.0f + (1.0f - k) * 20.0f;
			float r1 = r0 + 6.0f;
			seg.points = { f.pos_px + glm::vec2(std::cos(a) * r0, std::sin(a) * r0),
			               f.pos_px + glm::vec2(std::cos(a) * r1, std::sin(a) * r1) };
			strokes.push_back(std::move(seg));
		}
	}

	if (!strokes.empty()) renderer_->drawStrokes(strokes, nullptr, l.fw, l.fh);

	// --- Overlay UI with text renderer.
	draw_top_bar_();
	draw_mode_indicator_(l);
	draw_palette_(l, in, time_acc_);
	draw_loadout_(l, in);
	draw_stats_readout_(l);
	draw_canvas_buttons_(l, in, outc);

	return outc;
}

} // namespace civcraft::cellcraft
