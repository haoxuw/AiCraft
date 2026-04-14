// CellCraft — Creature Lab implementation.
//
// Three modes on one screen:
//   SCULPT — drag on the right half; pushes or pulls the cell boundary.
//   PLATE  — drag along the right-half boundary; paints an armored arc.
//   MODS   — drag a mod from the left palette onto the cell; places and
//            auto-mirrors.
//
// All modes enforce bilateral (vertical-axis) symmetry. Live stats and
// a material budget bar live on the right rail. USE THIS MONSTER is
// gated on being within budget and having a valid cell.

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
#include "CellCraft/sim/tuning.h"
#include "client/text.h"
#include "client/window.h"

namespace civcraft::cellcraft {

namespace {
constexpr float TWO_PI   = 6.28318530718f;
constexpr float PI       = 3.14159265359f;
constexpr float DEG_30   = TWO_PI / 12.0f;
constexpr float CELL_CANVAS_SCALE = 2.2f; // pixel-per-radius-unit visual zoom

// Mod palette — same eleven types as before.
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

float wrap_pi(float a) {
	while (a >  PI) a -= TWO_PI;
	while (a < -PI) a += TWO_PI;
	return a;
}
} // namespace

void LabScreen::init(Window* w, ChalkRenderer* r, TextRenderer* t) {
	window_ = w;
	renderer_ = r;
	text_ = t;
	reset();
}

void LabScreen::reset() {
	mode_ = Mode::SCULPT;
	cell_.init_circle(40.0f);
	rebuild_polygon_();
	plates_.clear();
	parts_.clear();
	brush_sigma_ = 0.18f;
	plate_painting_ = false;
	drag_palette_idx_ = -1;
	ghost_rotation_ = 0.0f;
	flashes_.clear();
	prev_mouse_left_down_sc_ = false;
	prev_mouse_left_down_md_ = false;
	prev_mouse_px_sc_ = glm::vec2(-1.0f);
	prev_right_ = false;
	status_ = "Drag on the right half to sculpt. Shape mirrors automatically.";
	status_color_ = glm::vec3(0.85f);
	time_acc_ = 0.0f;
}

LabScreen::Layout LabScreen::compute_layout_() const {
	Layout l;
	glfwGetFramebufferSize(window_->handle(), &l.fw, &l.fh);
	l.left_w   = (float)l.fw * 0.18f;
	l.right_w  = (float)l.fw * 0.20f;
	l.left_x   = 0.0f;
	l.right_x  = (float)l.fw - l.right_w;
	l.canvas_x = l.left_w;
	l.canvas_w = (float)l.fw - l.left_w - l.right_w;
	l.top_bar_h    = (float)l.fh * 0.10f;
	l.bottom_bar_h = (float)l.fh * 0.09f;
	l.canvas_cx = l.canvas_x + l.canvas_w * 0.5f;
	l.canvas_cy = l.top_bar_h + ((float)l.fh - l.top_bar_h - l.bottom_bar_h) * 0.5f;
	return l;
}

void LabScreen::rebuild_polygon_() {
	local_poly_ = sim::cellToPolygon(cell_, 1);
}

float LabScreen::cell_cost_() const  { return sim::cellPerimeter(cell_) * sim::BODY_COST_PER_PX; }
float LabScreen::plates_cost_() const {
	float s = 0.0f;
	for (const auto& p : plates_) {
		// Arc length = integral of r(θ) dθ (sufficient since plates hug the
		// boundary). Sample midpoint radius as a cheap proxy.
		float a0 = p.theta_start, a1 = p.theta_end;
		if (a1 < a0) a1 += TWO_PI;
		const int SAMPLES = 16;
		float len = 0.0f;
		glm::vec2 prev;
		for (int i = 0; i <= SAMPLES; ++i) {
			float t = (float)i / (float)SAMPLES;
			float a = a0 + (a1 - a0) * t;
			// Map to nearest cell sample radius.
			float fidx = (a / TWO_PI) * sim::RadialCell::N;
			int   idx  = ((int)std::floor(fidx) % sim::RadialCell::N + sim::RadialCell::N)
			             % sim::RadialCell::N;
			float r    = cell_.r[idx];
			glm::vec2 pt(std::cos(a) * r, std::sin(a) * r);
			if (i > 0) len += glm::length(pt - prev);
			prev = pt;
		}
		s += len;
	}
	return s * sim::PLATE_COST_PER_PX;
}
float LabScreen::parts_cost_() const {
	float s = 0.0f;
	for (const auto& p : parts_) s += sim::part_cost(p.type);
	return s;
}
float LabScreen::budget_() const { return sim::BODY_BUDGET_BIOMASS; }

glm::vec2 LabScreen::local_to_canvas_px_(glm::vec2 local, const Layout& l) const {
	// y-up in local → screen y grows downward.
	return glm::vec2(l.canvas_cx + local.x * CELL_CANVAS_SCALE,
	                 l.canvas_cy - local.y * CELL_CANVAS_SCALE);
}

glm::vec2 LabScreen::canvas_px_to_local_(glm::vec2 px, const Layout& l) const {
	return glm::vec2((px.x - l.canvas_cx) / CELL_CANVAS_SCALE,
	                 -(px.y - l.canvas_cy) / CELL_CANVAS_SCALE);
}

float LabScreen::mouse_angle_(glm::vec2 mpx, const Layout& l) const {
	glm::vec2 loc = canvas_px_to_local_(mpx, l);
	return std::atan2(loc.y, loc.x);
}

bool LabScreen::pt_inside_cell_(glm::vec2 local) const {
	return sim::point_in_polygon(local, local_poly_);
}

int LabScreen::part_cap_(sim::PartType t) {
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
}

void LabScreen::push_flash_(glm::vec2 pos_px, glm::vec3 col) {
	Flash f; f.pos_px = pos_px; f.t_left = 0.35f; f.color = col;
	flashes_.push_back(f);
}

// ============================================================================
// SCULPT
// ============================================================================

void LabScreen::handle_sculpt_(const LabInput& in, const Layout& l, float dt) {
	(void)dt;
	bool in_canvas = (in.mouse_px.x >= l.canvas_x
	               && in.mouse_px.x <= l.canvas_x + l.canvas_w
	               && in.mouse_px.y >= l.top_bar_h
	               && in.mouse_px.y <= (float)l.fh - l.bottom_bar_h);
	bool right_half = in.mouse_px.x >= l.canvas_cx;

	// Scroll-wheel changes brush sigma.
	if (in.scroll_y != 0.0f) {
		brush_sigma_ = std::clamp(brush_sigma_ + in.scroll_y * 0.02f, 0.05f, 0.5f);
	}

	bool drag_left  = in.mouse_left_down  && in_canvas && right_half;
	bool drag_right = in.mouse_right_down && in_canvas && right_half;
	if (!drag_left && !drag_right) {
		prev_mouse_left_down_sc_ = in.mouse_left_down;
		prev_mouse_px_sc_ = in.mouse_px;
		return;
	}

	// Determine push or pull:
	//   left-drag outward (away from core) → push out (+delta)
	//   left-drag inward (toward core) → pull in (-delta)
	//   right-drag: always pull in.
	glm::vec2 loc = canvas_px_to_local_(in.mouse_px, l);
	float cursor_r = std::max(1.0f, glm::length(loc));
	float theta = std::atan2(loc.y, loc.x);

	// Delta per-frame based on cursor's radial offset from the cell's
	// current radius at that angle. Clamp so single frames don't blow up.
	float fidx = (theta / TWO_PI) * sim::RadialCell::N;
	if (fidx < 0.0f) fidx += sim::RadialCell::N;
	int idx = ((int)std::floor(fidx) % sim::RadialCell::N + sim::RadialCell::N)
	          % sim::RadialCell::N;
	float cur_r = cell_.r[idx];
	float delta = (cursor_r - cur_r);
	if (drag_right) delta = -std::fabs(delta) * 0.5f;
	delta = std::clamp(delta, -6.0f, 6.0f);

	sim::brushDeform(cell_, theta, delta, brush_sigma_);
	rebuild_polygon_();

	if (std::fabs(delta) > 0.5f) {
		push_flash_(in.mouse_px, glm::vec3(0.9f, 0.95f, 0.5f));
	}
	status_ = drag_right ? "PULL" : "PUSH";
	status_color_ = glm::vec3(0.85f);

	prev_mouse_left_down_sc_ = in.mouse_left_down;
	prev_mouse_px_sc_ = in.mouse_px;
}

// ============================================================================
// PLATE
// ============================================================================

void LabScreen::handle_plate_(const LabInput& in, const Layout& l) {
	bool in_canvas = (in.mouse_px.x >= l.canvas_x
	               && in.mouse_px.x <= l.canvas_x + l.canvas_w
	               && in.mouse_px.y >= l.top_bar_h
	               && in.mouse_px.y <= (float)l.fh - l.bottom_bar_h);
	bool right_half = in.mouse_px.x >= l.canvas_cx;

	// Right-click removes a plate whose arc contains the cursor angle.
	if (in.mouse_right_click && in_canvas) {
		float a = mouse_angle_(in.mouse_px, l);
		for (size_t i = 0; i < plates_.size(); ) {
			if (sim::plate_covers(plates_[i], a, 0.05f)) {
				plates_.erase(plates_.begin() + i);
			} else {
				++i;
			}
		}
		status_ = "plate removed";
		status_color_ = glm::vec3(0.85f);
		return;
	}

	if (in.mouse_left_click && in_canvas && right_half) {
		plate_painting_ = true;
		float a = mouse_angle_(in.mouse_px, l);
		plate_theta_min_ = plate_theta_max_ = a;
	}
	if (plate_painting_ && in.mouse_left_down && in_canvas) {
		float a = mouse_angle_(in.mouse_px, l);
		// Accumulate min/max; keep both angles in the right half (−π/2, π/2).
		if (a >  PI * 0.5f) a =  PI * 0.5f;
		if (a < -PI * 0.5f) a = -PI * 0.5f;
		plate_theta_min_ = std::min(plate_theta_min_, a);
		plate_theta_max_ = std::max(plate_theta_max_, a);
	}
	if (plate_painting_ && !in.mouse_left_down) {
		plate_painting_ = false;
		float span = plate_theta_max_ - plate_theta_min_;
		if (span > 0.05f) {
			sim::Plate p;
			p.theta_start = plate_theta_min_;
			p.theta_end   = plate_theta_max_;
			p.thickness   = 4.0f;
			plates_.push_back(p);
			plates_.push_back(sim::mirrored_plate(p));
			status_ = "plate painted (auto-mirrored)";
			status_color_ = glm::vec3(0.7f, 1.0f, 0.7f);
			push_flash_(in.mouse_px);
		} else {
			status_ = "drag to paint — too small";
			status_color_ = glm::vec3(1.0f, 0.7f, 0.4f);
		}
	}
}

// ============================================================================
// MODS
// ============================================================================

void LabScreen::handle_mods_(const LabInput& in, const Layout& l) {
	bool in_canvas = (in.mouse_px.x >= l.canvas_x
	               && in.mouse_px.x <= l.canvas_x + l.canvas_w
	               && in.mouse_px.y >= l.top_bar_h
	               && in.mouse_px.y <= (float)l.fh - l.bottom_bar_h);

	// Right-click inside canvas removes the nearest placed part + its mirror.
	if (in.mouse_right_click && in_canvas && !parts_.empty()) {
		float best = 32.0f;
		int   best_i = -1;
		for (size_t i = 0; i < parts_.size(); ++i) {
			glm::vec2 sp = local_to_canvas_px_(parts_[i].anchor_local, l);
			float d = glm::length(sp - in.mouse_px);
			if (d < best) { best = d; best_i = (int)i; }
		}
		if (best_i >= 0) {
			sim::Part victim = parts_[best_i];
			parts_.erase(parts_.begin() + best_i);
			// Remove its mirror too (nearest remaining with mirrored anchor).
			glm::vec2 mirror_anchor(-victim.anchor_local.x, victim.anchor_local.y);
			float best2 = 1.0f;
			int   best_j = -1;
			for (size_t j = 0; j < parts_.size(); ++j) {
				if (parts_[j].type != victim.type) continue;
				float d = glm::length(parts_[j].anchor_local - mirror_anchor);
				if (d < best2) { best2 = d; best_j = (int)j; }
			}
			if (best_j >= 0) parts_.erase(parts_.begin() + best_j);
			status_ = "mod removed";
			status_color_ = glm::vec3(0.85f);
		}
		return;
	}

	// Scroll rotates the dragged ghost in 30° steps.
	if (drag_palette_idx_ >= 0 && in.scroll_y != 0.0f) {
		ghost_rotation_ = std::fmod(ghost_rotation_
			+ (in.scroll_y > 0.0f ? DEG_30 : -DEG_30), TWO_PI);
	}

	// Release: if over canvas + inside cell + budget ok, place & mirror.
	if (drag_palette_idx_ >= 0 && !in.mouse_left_down) {
		sim::PartType pt = kPaletteTypes[drag_palette_idx_];
		float cost = sim::part_cost(pt) * 2.0f; // with mirror
		glm::vec2 local = canvas_px_to_local_(in.mouse_px, l);
		bool ok = in_canvas && pt_inside_cell_(local);
		int have = 0;
		for (auto& p : parts_) if (p.type == pt) ++have;
		int cap = part_cap_(pt);
		if (!ok) {
			status_ = "drop on the body";
			status_color_ = glm::vec3(1.0f, 0.7f, 0.4f);
		} else if (total_cost_() + cost > budget_()) {
			status_ = "not enough biomass";
			status_color_ = glm::vec3(1.0f, 0.4f, 0.4f);
			push_flash_(in.mouse_px, glm::vec3(1.0f, 0.4f, 0.4f));
			ok = false;
		} else if (have + 2 > cap) {
			status_ = "stack cap reached";
			status_color_ = glm::vec3(1.0f, 0.4f, 0.4f);
			push_flash_(in.mouse_px, glm::vec3(1.0f, 0.4f, 0.4f));
			ok = false;
		}
		if (ok) {
			sim::Part a;
			a.type = pt;
			a.anchor_local = local;
			a.orientation  = ghost_rotation_;
			parts_.push_back(a);
			sim::Part b = a;
			b.anchor_local = glm::vec2(-local.x, local.y);
			b.orientation  = PI - ghost_rotation_;
			parts_.push_back(b);
			push_flash_(local_to_canvas_px_(a.anchor_local, l));
			push_flash_(local_to_canvas_px_(b.anchor_local, l));
			status_ = "mod placed (auto-mirrored)";
			status_color_ = glm::vec3(0.7f, 1.0f, 0.7f);
		}
		drag_palette_idx_ = -1;
	}

	prev_mouse_left_down_md_ = in.mouse_left_down;
}

// ============================================================================
// Rendering
// ============================================================================

void LabScreen::draw_grid_(std::vector<ChalkStroke>& out, const Layout& l) {
	for (int r = 40; r <= 400; r += 40) {
		ChalkStroke s;
		s.color = glm::vec3(0.22f, 0.24f, 0.24f);
		s.half_width = 0.8f;
		const int N = 48;
		for (int i = 0; i <= N; i += 2) {
			float a = TWO_PI * (float)i / (float)N;
			glm::vec2 p(l.canvas_cx + std::cos(a) * (float)r,
			            l.canvas_cy + std::sin(a) * (float)r);
			s.points.push_back(p);
		}
		if (s.points.size() >= 2) out.push_back(std::move(s));
	}
}

void LabScreen::draw_mirror_line_(std::vector<ChalkStroke>& out, const Layout& l) {
	// Dashed vertical axis through canvas_cx.
	glm::vec3 col(0.95f, 0.88f, 0.45f);
	for (float y = l.top_bar_h + 8.0f;
	     y < (float)l.fh - l.bottom_bar_h - 8.0f; y += 16.0f) {
		ChalkStroke seg;
		seg.color = col;
		seg.half_width = 1.2f;
		seg.points = { {l.canvas_cx, y}, {l.canvas_cx, y + 8.0f} };
		out.push_back(std::move(seg));
	}
}

void LabScreen::draw_cell_(std::vector<ChalkStroke>& out, const Layout& l, float time_s) {
	(void)time_s;
	if (local_poly_.size() < 3) return;
	ChalkStroke body;
	body.color = color_;
	body.half_width = 4.0f;
	for (auto& v : local_poly_) body.points.push_back(local_to_canvas_px_(v, l));
	if (!body.points.empty()) body.points.push_back(body.points.front());
	out.push_back(std::move(body));

	// Core crosshair.
	ChalkStroke c1, c2;
	c1.color = glm::vec3(1.0f); c1.half_width = 1.8f; c2 = c1;
	c1.points = { {l.canvas_cx - 6.0f, l.canvas_cy}, {l.canvas_cx + 6.0f, l.canvas_cy} };
	c2.points = { {l.canvas_cx, l.canvas_cy - 6.0f}, {l.canvas_cx, l.canvas_cy + 6.0f} };
	out.push_back(std::move(c1));
	out.push_back(std::move(c2));
}

void LabScreen::draw_plates_(std::vector<ChalkStroke>& out, const Layout& l) {
	auto radius_at = [&](float ang) {
		float fidx = (ang / TWO_PI) * sim::RadialCell::N;
		if (fidx < 0.0f) fidx += sim::RadialCell::N;
		int idx = ((int)std::floor(fidx) % sim::RadialCell::N + sim::RadialCell::N)
		          % sim::RadialCell::N;
		return cell_.r[idx];
	};
	for (const auto& p : plates_) {
		ChalkStroke pb;
		pb.color = glm::vec3(0.82f, 0.82f, 0.80f);
		pb.half_width = p.thickness;
		const int N = 18;
		float a0 = p.theta_start;
		float a1 = p.theta_end;
		if (a1 < a0) a1 += TWO_PI;
		for (int i = 0; i <= N; ++i) {
			float t = (float)i / (float)N;
			float a = a0 + (a1 - a0) * t;
			float r = radius_at(a);
			glm::vec2 lv(std::cos(a) * r, std::sin(a) * r);
			pb.points.push_back(local_to_canvas_px_(lv, l));
		}
		out.push_back(std::move(pb));
	}
	// If actively painting, preview the arc so far.
	if (plate_painting_ && plate_theta_max_ - plate_theta_min_ > 1e-3f) {
		ChalkStroke pb;
		pb.color = glm::vec3(1.0f, 0.95f, 0.4f);
		pb.half_width = 3.0f;
		const int N = 14;
		for (int i = 0; i <= N; ++i) {
			float t = (float)i / (float)N;
			float a = plate_theta_min_ + (plate_theta_max_ - plate_theta_min_) * t;
			float r = radius_at(a);
			glm::vec2 lv(std::cos(a) * r, std::sin(a) * r);
			pb.points.push_back(local_to_canvas_px_(lv, l));
		}
		out.push_back(std::move(pb));
	}
}

void LabScreen::draw_parts_(std::vector<ChalkStroke>& out, const Layout& l, float time_s) {
	auto to_canvas = [&](glm::vec2 lv) { return local_to_canvas_px_(lv, l); };
	// Inflate glyph about each part anchor for visibility.
	const float SCALE = 1.5f;
	for (const auto& p : parts_) {
		std::vector<sim::Part> single = { p };
		auto xform = [&](glm::vec2 lv) {
			glm::vec2 rel = lv - p.anchor_local;
			return to_canvas(p.anchor_local + rel * SCALE);
		};
		appendPartStrokes(single, color_, xform, 1.0f, time_s, out);
	}
}

void LabScreen::draw_head_tail_markers_(std::vector<ChalkStroke>& out, const Layout& l) {
	float aspect = window_->aspectRatio();
	(void)aspect;
	// Chalk "^" above the cell and "v" below, plus textual labels. Labels
	// are rendered via text_ later — here we only add chalk glyphs.
	float top_r = cell_.r[sim::RadialCell::N / 4]; // θ = π/2 sample
	float bot_r = cell_.r[(3 * sim::RadialCell::N) / 4];
	glm::vec2 head_px = local_to_canvas_px_(glm::vec2(0.0f,  top_r + 18.0f), l);
	glm::vec2 tail_px = local_to_canvas_px_(glm::vec2(0.0f, -bot_r - 18.0f), l);
	// "^"
	{
		ChalkStroke s;
		s.color = glm::vec3(0.9f, 0.95f, 0.5f);
		s.half_width = 2.0f;
		s.points = { {head_px.x - 8.0f, head_px.y + 6.0f},
		             {head_px.x,        head_px.y - 6.0f},
		             {head_px.x + 8.0f, head_px.y + 6.0f} };
		out.push_back(std::move(s));
	}
	// "v"
	{
		ChalkStroke s;
		s.color = glm::vec3(0.65f, 0.7f, 0.9f);
		s.half_width = 2.0f;
		s.points = { {tail_px.x - 8.0f, tail_px.y - 6.0f},
		             {tail_px.x,        tail_px.y + 6.0f},
		             {tail_px.x + 8.0f, tail_px.y - 6.0f} };
		out.push_back(std::move(s));
	}
}

void LabScreen::draw_flashes_(std::vector<ChalkStroke>& out) {
	for (auto& f : flashes_) {
		float k = std::max(0.0f, f.t_left / 0.35f);
		for (int i = 0; i < 6; ++i) {
			float a = TWO_PI * (float)i / 6.0f;
			ChalkStroke seg;
			seg.color = f.color * k;
			seg.half_width = 2.0f * k + 0.5f;
			float r0 = 6.0f + (1.0f - k) * 18.0f;
			float r1 = r0 + 5.0f;
			seg.points = { f.pos_px + glm::vec2(std::cos(a) * r0, std::sin(a) * r0),
			               f.pos_px + glm::vec2(std::cos(a) * r1, std::sin(a) * r1) };
			out.push_back(std::move(seg));
		}
	}
}

// ---- UI rails --------------------------------------------------------------

bool LabScreen::pixel_button_(float x, float y, float w, float h,
                              const char* label, bool enabled,
                              const LabInput& in, bool selected) {
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
	float nx = (x / (float)ww) * 2.0f - 1.0f;
	float ny = 1.0f - ((y + h) / (float)wh) * 2.0f;
	float nw = (w / (float)ww) * 2.0f;
	float nh = (h / (float)wh) * 2.0f;
	bool hover = pt_in_rect_(in.mouse_px, x, y, w, h) && enabled;
	glm::vec4 bg;
	if (!enabled)        bg = glm::vec4(0.06f, 0.06f, 0.06f, 0.7f);
	else if (selected)   bg = glm::vec4(0.25f, 0.35f, 0.18f, 0.9f);
	else if (hover)      bg = glm::vec4(0.22f, 0.22f, 0.25f, 0.9f);
	else                 bg = glm::vec4(0.10f, 0.10f, 0.12f, 0.8f);
	text_->drawRect(nx, ny, nw, nh, bg);
	glm::vec4 edge(0.85f, 0.85f, 0.80f, enabled ? 0.95f : 0.4f);
	float t = 0.003f;
	text_->drawRect(nx, ny, nw, t, edge);
	text_->drawRect(nx, ny + nh - t, nw, t, edge);
	text_->drawRect(nx, ny, t, nh, edge);
	text_->drawRect(nx + nw - t, ny, t, nh, edge);
	float label_w = (float)std::strlen(label) * 0.018f;
	float tx = nx + (nw - label_w) * 0.5f;
	float ty = ny + (nh - 0.032f) * 0.5f;
	glm::vec4 col = enabled ? glm::vec4(0.97f, 0.97f, 0.92f, 1.0f)
	                        : glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
	text_->drawText(label, tx, ty, 1.0f, col, window_->aspectRatio());
	return hover && in.mouse_left_click;
}

void LabScreen::draw_title_bar_() {
	float aspect = window_->aspectRatio();
	text_->drawTitle("CREATURE LAB", -0.22f, 0.88f, 1.3f,
		glm::vec4(1.0f, 1.0f, 0.95f, 1.0f), aspect);
	text_->drawText(status_, -0.25f, 0.78f, 0.85f,
		glm::vec4(status_color_, 1.0f), aspect);
}

void LabScreen::draw_top_tabs_(const Layout& l, const LabInput& in) {
	// Three tabs centered above the canvas.
	float tab_w = 140.0f;
	float tab_h = 36.0f;
	float gap = 10.0f;
	float total = tab_w * 3.0f + gap * 2.0f;
	float x0 = l.canvas_cx - total * 0.5f;
	float y  = l.top_bar_h - tab_h - 8.0f;
	const char* labels[3] = { "SCULPT", "PLATE", "MODS" };
	Mode modes[3] = { Mode::SCULPT, Mode::PLATE, Mode::MODS };
	for (int i = 0; i < 3; ++i) {
		bool sel = (mode_ == modes[i]);
		float bx = x0 + (float)i * (tab_w + gap);
		if (pixel_button_(bx, y, tab_w, tab_h, labels[i], true, in, sel)) {
			mode_ = modes[i];
			drag_palette_idx_ = -1;
			plate_painting_ = false;
			if (mode_ == Mode::SCULPT)
				status_ = "Drag on the right half to push/pull the boundary.";
			else if (mode_ == Mode::PLATE)
				status_ = "Drag along the right-half boundary to paint a plate.";
			else
				status_ = "Drag a mod from the left palette onto the body.";
			status_color_ = glm::vec3(0.85f);
		}
	}
}

void LabScreen::draw_palette_(const Layout& l, const LabInput& in, float time_s) {
	float aspect = window_->aspectRatio();
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);

	{
		float nx = (l.left_x / (float)ww) * 2.0f - 1.0f + 0.01f;
		float ny = 1.0f - (l.top_bar_h + 16.0f) / (float)wh * 2.0f;
		text_->drawText("PARTS", nx, ny, 1.1f, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), aspect);
	}

	float y0   = l.top_bar_h + 48.0f;
	float row_h = 44.0f;
	float row_x = l.left_x + 6.0f;
	float row_w = l.left_w - 12.0f;
	for (int i = 0; i < kPaletteCount; ++i) {
		float ry = y0 + (float)i * row_h;
		sim::PartType pt = kPaletteTypes[i];
		bool hover = pt_in_rect_(in.mouse_px, row_x, ry, row_w, row_h - 4.0f);
		bool dragging = (drag_palette_idx_ == i);
		{
			float nx = (row_x / (float)ww) * 2.0f - 1.0f;
			float ny = 1.0f - ((ry + row_h - 4.0f) / (float)wh) * 2.0f;
			float nw = (row_w / (float)ww) * 2.0f;
			float nh = ((row_h - 4.0f) / (float)wh) * 2.0f;
			glm::vec4 bg = dragging ? glm::vec4(0.35f, 0.30f, 0.10f, 0.9f)
			             : hover    ? glm::vec4(0.20f, 0.20f, 0.22f, 0.9f)
			                        : glm::vec4(0.08f, 0.08f, 0.10f, 0.8f);
			text_->drawRect(nx, ny, nw, nh, bg);
			char lbl[64];
			std::snprintf(lbl, sizeof(lbl), "%s  %.0fbm",
				sim::part_name(pt), sim::part_cost(pt));
			text_->drawText(lbl, nx + 0.06f, ny + nh - 0.045f, 0.9f,
				glm::vec4(0.97f, 0.97f, 0.92f, 1.0f), aspect);
			text_->drawText(sim::part_desc(pt), nx + 0.06f, ny + 0.008f, 0.65f,
				glm::vec4(0.7f, 0.7f, 0.7f, 1.0f), aspect);
		}
		// Icon glyph (left of the row).
		{
			std::vector<ChalkStroke> glyph;
			sim::Part single; single.type = pt; single.anchor_local = glm::vec2(0.0f);
			glm::vec2 icon_pos(row_x + 20.0f, ry + row_h * 0.45f);
			auto xform = [&](glm::vec2 v) { return icon_pos + glm::vec2(v.x, -v.y) * 1.4f; };
			appendPartStrokes({single}, color_, xform, 1.0f, time_s, glyph);
			if (!glyph.empty())
				renderer_->drawStrokes(glyph, nullptr, l.fw, l.fh);
		}
		// Start drag in MODS mode on press-inside-row.
		if (mode_ == Mode::MODS && hover && in.mouse_left_click
		 && drag_palette_idx_ < 0) {
			drag_palette_idx_ = i;
			ghost_rotation_ = 0.0f;
		}
	}
}

void LabScreen::draw_loadout_and_stats_(const Layout& l, const LabInput& in) {
	(void)in;
	float aspect = window_->aspectRatio();
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
	float nx = (l.right_x / (float)ww) * 2.0f - 1.0f + 0.01f;
	float ny_top = 1.0f - (l.top_bar_h + 16.0f) / (float)wh * 2.0f;
	text_->drawText("LOADOUT", nx, ny_top, 1.1f,
		glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), aspect);

	// Probe monster for stats.
	sim::Monster probe;
	probe.shape = local_poly_;
	probe.biomass = 25.0f;
	probe.parts = parts_;
	probe.plates = plates_;
	probe.refresh_stats();

	// Stat bars.
	auto bar = [&](float y_ndc, const char* label, float frac, glm::vec4 col) {
		text_->drawText(label, nx, y_ndc, 0.75f,
			glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), aspect);
		float bar_y = y_ndc - 0.012f;
		text_->drawRect(nx, bar_y, 0.18f, 0.010f, glm::vec4(0.15f, 0.15f, 0.15f, 0.8f));
		text_->drawRect(nx, bar_y, 0.18f * std::clamp(frac, 0.0f, 1.0f),
			0.010f, col);
	};
	auto norm01 = [](float v, float lo, float hi) {
		return std::clamp((v - lo) / std::max(1e-3f, (hi - lo)), 0.0f, 1.0f);
	};
	float y = ny_top - 0.08f;
	bar(y,        "SPEED", norm01(probe.move_speed, sim::MOVE_MIN, sim::MOVE_MAX),
		glm::vec4(0.55f, 0.82f, 1.0f, 1.0f));
	bar(y - 0.055f, "TURN",  norm01(probe.turn_speed, sim::TURN_MIN, sim::TURN_MAX),
		glm::vec4(0.55f, 1.0f, 0.65f, 1.0f));
	bar(y - 0.110f, "HP",    std::clamp(probe.hp_max / 200.0f, 0.0f, 1.0f),
		glm::vec4(1.0f, 0.45f, 0.45f, 1.0f));
	bar(y - 0.165f, "DMG",   std::clamp((probe.part_effect.damage_mult - 1.0f) / 2.0f + 0.33f,
		0.0f, 1.0f), glm::vec4(1.0f, 0.75f, 0.35f, 1.0f));
	bar(y - 0.220f, "REGEN", std::clamp(probe.part_effect.regen_hps / 6.0f, 0.0f, 1.0f),
		glm::vec4(0.7f, 1.0f, 0.7f, 1.0f));

	// Budget bar — 3 segments (body / plates / mods) against total.
	float ybud = y - 0.30f;
	text_->drawText("BUDGET", nx, ybud, 0.9f,
		glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), aspect);
	float bar_y = ybud - 0.022f;
	float bud = budget_();
	float body_w = (cell_cost_() / bud) * 0.18f;
	float plate_w = (plates_cost_() / bud) * 0.18f;
	float parts_w = (parts_cost_() / bud) * 0.18f;
	float total_w = body_w + plate_w + parts_w;
	glm::vec4 col_body (0.55f, 0.82f, 1.0f, 1.0f);
	glm::vec4 col_plate(0.80f, 0.80f, 0.80f, 1.0f);
	glm::vec4 col_mods (1.0f, 0.75f, 0.35f, 1.0f);
	text_->drawRect(nx, bar_y, 0.18f, 0.014f, glm::vec4(0.15f, 0.15f, 0.15f, 0.8f));
	text_->drawRect(nx,                       bar_y, std::min(0.18f, body_w),  0.014f, col_body);
	text_->drawRect(nx + body_w,              bar_y, std::min(0.18f - body_w, plate_w), 0.014f, col_plate);
	text_->drawRect(nx + body_w + plate_w,    bar_y,
		std::min(0.18f - body_w - plate_w, parts_w), 0.014f, col_mods);
	char bb[80];
	std::snprintf(bb, sizeof(bb), "%.0f / %.0f bm", total_cost_(), bud);
	glm::vec4 bcol = (total_cost_() > bud)
		? glm::vec4(1.0f, 0.5f, 0.5f, 1.0f) : glm::vec4(0.85f, 0.85f, 0.85f, 1.0f);
	text_->drawText(bb, nx, bar_y - 0.030f, 0.75f, bcol, aspect);
	// (void)total_w;
	(void)total_w;
}

void LabScreen::draw_bottom_buttons_(const Layout& l, const LabInput& in,
                                     LabOutcome& outc) {
	float by = (float)l.fh - l.bottom_bar_h + 8.0f;
	float bh = l.bottom_bar_h - 16.0f;
	float bw = std::min(200.0f, l.canvas_w * 0.22f);
	float gap = 12.0f;

	// BACK + RESET + USE (and, in SCULPT mode, RESET CELL).
	float total = bw * 3.0f + gap * 2.0f;
	float bx = l.canvas_cx - total * 0.5f;

	if (pixel_button_(bx + 0 * (bw + gap), by, bw, bh, "BACK", true, in)) {
		outc = LabOutcome::BACK;
	}
	if (pixel_button_(bx + 1 * (bw + gap), by, bw, bh, "RESET", true, in)) {
		reset();
	}
	bool use_ok = total_cost_() <= budget_() && !local_poly_.empty();
	if (pixel_button_(bx + 2 * (bw + gap), by, bw, bh,
	                  "USE THIS MONSTER", use_ok, in)) {
		outc = LabOutcome::USE;
	}
}

// ============================================================================
// update()
// ============================================================================

LabOutcome LabScreen::update(float dt, const LabInput& in) {
	time_acc_ += dt;
	Layout l = compute_layout_();
	LabOutcome outc = LabOutcome::NONE;

	for (int key : in.keys_pressed) {
		if (key == GLFW_KEY_ESCAPE) outc = LabOutcome::BACK;
		else if (key == GLFW_KEY_1) mode_ = Mode::SCULPT;
		else if (key == GLFW_KEY_2) mode_ = Mode::PLATE;
		else if (key == GLFW_KEY_3) mode_ = Mode::MODS;
	}

	switch (mode_) {
	case Mode::SCULPT: handle_sculpt_(in, l, dt); break;
	case Mode::PLATE:  handle_plate_(in, l);     break;
	case Mode::MODS:   handle_mods_(in, l);      break;
	}

	// Tick flashes.
	for (auto& f : flashes_) f.t_left -= dt;
	flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(),
		[](const Flash& f){ return f.t_left <= 0.0f; }), flashes_.end());

	// --- Chalk layer.
	std::vector<ChalkStroke> strokes;
	draw_grid_(strokes, l);
	draw_mirror_line_(strokes, l);
	draw_cell_(strokes, l, time_acc_);
	draw_plates_(strokes, l);
	draw_parts_(strokes, l, time_acc_);
	draw_head_tail_markers_(strokes, l);
	draw_flashes_(strokes);

	// MODS ghost at cursor.
	if (mode_ == Mode::MODS && drag_palette_idx_ >= 0) {
		sim::Part ghost;
		ghost.type = kPaletteTypes[drag_palette_idx_];
		glm::vec2 local = canvas_px_to_local_(in.mouse_px, l);
		ghost.anchor_local = local;
		ghost.orientation = ghost_rotation_;
		std::vector<sim::Part> single = { ghost };
		auto xform = [&](glm::vec2 lv) {
			glm::vec2 rel = lv - local;
			rel = rotate_v(rel, ghost_rotation_);
			return local_to_canvas_px_(local + rel * 1.5f, l);
		};
		std::vector<ChalkStroke> gh;
		appendPartStrokes(single, glm::vec3(0.9f, 0.9f, 0.55f), xform,
			1.0f, time_acc_, gh);
		for (auto& s : gh) { s.half_width *= 0.7f; s.color *= 0.8f; strokes.push_back(std::move(s)); }
	}

	if (!strokes.empty()) renderer_->drawStrokes(strokes, nullptr, l.fw, l.fh);

	// --- Text overlay.
	draw_title_bar_();
	// Head/Tail text labels placed relative to canvas center.
	{
		int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
		float aspect = window_->aspectRatio();
		float head_r_px = cell_.r[sim::RadialCell::N / 4] * CELL_CANVAS_SCALE;
		float tail_r_px = cell_.r[(3 * sim::RadialCell::N) / 4] * CELL_CANVAS_SCALE;
		float hx = (l.canvas_cx / (float)ww) * 2.0f - 1.0f - 0.03f;
		float hy = 1.0f - ((l.canvas_cy - head_r_px - 32.0f) / (float)wh) * 2.0f;
		text_->drawText("HEAD", hx, hy, 0.9f,
			glm::vec4(0.95f, 1.0f, 0.55f, 1.0f), aspect);
		float tx = hx;
		float ty = 1.0f - ((l.canvas_cy + tail_r_px + 36.0f) / (float)wh) * 2.0f;
		text_->drawText("TAIL", tx, ty, 0.9f,
			glm::vec4(0.65f, 0.7f, 0.9f, 1.0f), aspect);
	}

	draw_top_tabs_(l, in);
	draw_palette_(l, in, time_acc_);
	draw_loadout_and_stats_(l, in);
	draw_bottom_buttons_(l, in, outc);

	prev_right_ = in.mouse_right_down;
	return outc;
}

// ============================================================================
// Screenshot seeders
// ============================================================================

void LabScreen::seed_sculpt_for_screenshot() {
	reset();
	mode_ = Mode::SCULPT;
	// Pinch into an oblong head-up body via a few brush strokes.
	sim::brushDeform(cell_,  PI * 0.5f,  30.0f, 0.22f);
	sim::brushDeform(cell_, -PI * 0.5f,  28.0f, 0.22f);
	sim::brushDeform(cell_,  0.0f,      -14.0f, 0.30f);
	sim::brushDeform(cell_,  PI * 0.33f, 10.0f, 0.14f);
	rebuild_polygon_();
	status_ = "SCULPT: pushed the head and tail, pinched the waist.";
}

void LabScreen::seed_plate_for_screenshot() {
	reset();
	mode_ = Mode::PLATE;
	// Inflate a bit first so plates are visible.
	sim::brushDeform(cell_, 0.0f, 12.0f, 0.35f);
	rebuild_polygon_();
	sim::Plate p1; p1.theta_start = -PI * 0.10f; p1.theta_end =  PI * 0.10f; p1.thickness = 5.0f;
	sim::Plate p2; p2.theta_start =  PI * 0.28f; p2.theta_end =  PI * 0.44f; p2.thickness = 5.0f;
	plates_.push_back(p1); plates_.push_back(sim::mirrored_plate(p1));
	plates_.push_back(p2); plates_.push_back(sim::mirrored_plate(p2));
	status_ = "PLATE: two armored arcs painted (auto-mirrored).";
}

void LabScreen::seed_mods_for_screenshot() {
	reset();
	mode_ = Mode::MODS;
	sim::brushDeform(cell_,  PI * 0.5f, 18.0f, 0.25f);
	sim::brushDeform(cell_, -PI * 0.5f, 18.0f, 0.25f);
	rebuild_polygon_();
	auto place_pair = [&](sim::PartType t, glm::vec2 right_anchor, float rot) {
		sim::Part a; a.type = t; a.anchor_local = right_anchor; a.orientation = rot;
		sim::Part b; b.type = t; b.anchor_local = glm::vec2(-right_anchor.x, right_anchor.y);
		b.orientation = PI - rot;
		parts_.push_back(a); parts_.push_back(b);
	};
	place_pair(sim::PartType::SPIKE,    glm::vec2( 8.0f,  42.0f),  PI * 0.5f);
	place_pair(sim::PartType::FLAGELLA, glm::vec2( 6.0f, -40.0f), -PI * 0.5f);
	place_pair(sim::PartType::ARMOR,    glm::vec2(24.0f,   0.0f),  0.0f);
	// Simulate a ghost currently being dragged (so the screenshot shows it).
	drag_palette_idx_ = 5; // CILIA
	ghost_rotation_ = DEG_30;
	status_ = "MODS: dragging a mod onto the body (ghost at cursor).";
}

} // namespace civcraft::cellcraft
