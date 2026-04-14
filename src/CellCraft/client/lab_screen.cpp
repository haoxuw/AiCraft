// CellCraft — Creature Lab implementation (unified canvas, Step 1).
//
// One canvas, no mode tabs. Sculpt + drag-drop parts in place; no
// transform gizmo yet (Step 2 adds gizmo, scale handles, rotate).

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
constexpr float TWO_PI = 6.28318530718f;
constexpr float PI     = 3.14159265359f;
constexpr float DEG_15 = TWO_PI / 24.0f;
constexpr float CELL_CANVAS_SCALE = 2.2f;

const sim::PartType kPaletteTypes[] = {
	sim::PartType::SPIKE,       sim::PartType::TEETH,
	sim::PartType::FLAGELLA,    sim::PartType::POISON,
	sim::PartType::ARMOR,       sim::PartType::CILIA,
	sim::PartType::HORN,        sim::PartType::REGEN,
	sim::PartType::MOUTH,       sim::PartType::VENOM_SPIKE,
	sim::PartType::EYES,
};
constexpr int kPaletteCount = (int)(sizeof(kPaletteTypes) / sizeof(kPaletteTypes[0]));
} // namespace

void LabScreen::init(Window* w, ChalkRenderer* r, TextRenderer* t) {
	window_ = w; renderer_ = r; text_ = t;
	reset();
}

void LabScreen::reset() {
	cell_.init_circle(40.0f);
	rebuild_polygon_();
	parts_.clear();
	brush_sigma_ = 0.18f;
	op_ = Op::NONE;
	drag_palette_idx_ = -1;
	selected_idx_ = -1;
	gizmo_handle_ = -1;
	flashes_.clear();
	status_ = "Left-drag canvas to sculpt. Drag parts from the left palette.";
	status_color_ = glm::vec3(0.85f);
	time_acc_ = 0.0f;
}

LabScreen::Layout LabScreen::compute_layout_() const {
	Layout l;
	glfwGetFramebufferSize(window_->handle(), &l.fw, &l.fh);
	l.left_w  = (float)l.fw * 0.18f;
	l.right_w = (float)l.fw * 0.20f;
	l.left_x  = 0.0f;
	l.right_x = (float)l.fw - l.right_w;
	l.canvas_x = l.left_w;
	l.canvas_w = (float)l.fw - l.left_w - l.right_w;
	l.top_bar_h    = (float)l.fh * 0.10f;
	l.bottom_bar_h = (float)l.fh * 0.09f;
	l.canvas_cx = l.canvas_x + l.canvas_w * 0.5f;
	l.canvas_cy = l.top_bar_h + ((float)l.fh - l.top_bar_h - l.bottom_bar_h) * 0.5f;
	return l;
}

void LabScreen::rebuild_polygon_() { local_poly_ = sim::cellToPolygon(cell_, 1); }

glm::vec2 LabScreen::local_to_canvas_px_(glm::vec2 local, const Layout& l) const {
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

float LabScreen::cell_cost_() const { return sim::cellPerimeter(cell_) * sim::BODY_COST_PER_PX; }
float LabScreen::parts_cost_() const {
	float s = 0.0f;
	for (const auto& p : parts_) s += sim::part_cost(p.type);
	return s;
}
float LabScreen::budget_() const { return sim::BODY_BUDGET_BIOMASS; }

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

int LabScreen::palette_hit_(const LabInput& in, const Layout& l) const {
	float y0 = l.top_bar_h + 48.0f;
	float row_h = 44.0f;
	float row_x = l.left_x + 6.0f;
	float row_w = l.left_w - 12.0f;
	for (int i = 0; i < kPaletteCount; ++i) {
		float ry = y0 + (float)i * row_h;
		if (pt_in_rect_(in.mouse_px, row_x, ry, row_w, row_h - 4.0f)) return i;
	}
	return -1;
}

int LabScreen::placed_part_hit_(glm::vec2 px, const Layout& l) const {
	int best = -1;
	float best_d = 1e9f;
	for (size_t i = 0; i < parts_.size(); ++i) {
		glm::vec2 sp = local_to_canvas_px_(parts_[i].anchor_local, l);
		float d = glm::length(sp - px);
		if (d < 18.0f && d < best_d) { best_d = d; best = (int)i; }
	}
	return best;
}

int LabScreen::gizmo_handle_hit_(glm::vec2, const Layout&) const { return -1; }
bool LabScreen::gizmo_bbox_hit_(glm::vec2, const Layout&) const { return false; }
void LabScreen::compute_bbox_px_(int, const Layout&, float& x, float& y, float& w, float& h) const {
	x = y = w = h = 0.0f;
}
float LabScreen::max_scale_within_budget_(int) const { return 1.0f; }
bool LabScreen::in_canvas_drag_() const {
	return op_ == Op::SCULPT || op_ == Op::GIZMO_TRANSLATE
	    || op_ == Op::GIZMO_SCALE || op_ == Op::GIZMO_ROTATE;
}

int LabScreen::mirror_partner_(int i) const {
	if (i < 0 || i >= (int)parts_.size()) return -1;
	const sim::Part& p = parts_[i];
	if (std::fabs(p.anchor_local.x) < 1e-3f) return -1;
	glm::vec2 m = mirror_anchor_(p.anchor_local);
	int best = -1;
	float best_d = 1.5f;
	for (size_t j = 0; j < parts_.size(); ++j) {
		if ((int)j == i) continue;
		if (parts_[j].type != p.type) continue;
		float d = glm::length(parts_[j].anchor_local - m);
		if (d < best_d) { best_d = d; best = (int)j; }
	}
	return best;
}

bool LabScreen::place_part_pair_(sim::PartType pt, glm::vec2 anchor_local,
                                 float orientation, float scale) {
	(void)scale;
	float cost = sim::part_cost(pt) * 2.0f;
	int have = 0;
	for (auto& p : parts_) if (p.type == pt) ++have;
	int cap = part_cap_(pt);
	if (cell_cost_() + parts_cost_() + cost > budget_()) {
		status_ = "not enough biomass";
		status_color_ = glm::vec3(1.0f, 0.4f, 0.4f);
		return false;
	}
	if (have + 2 > cap) {
		status_ = "stack cap reached";
		status_color_ = glm::vec3(1.0f, 0.4f, 0.4f);
		return false;
	}
	sim::Part a; a.type = pt; a.anchor_local = anchor_local; a.orientation = orientation;
	sim::Part b = a;
	b.anchor_local = mirror_anchor_(anchor_local);
	b.orientation  = PI - orientation;
	parts_.push_back(a); parts_.push_back(b);
	status_ = "mod placed (auto-mirrored)";
	status_color_ = glm::vec3(0.7f, 1.0f, 0.7f);
	return true;
}

void LabScreen::handle_input_(const LabInput& in, const Layout& l, float dt) {
	(void)dt;
	bool in_canvas = (in.mouse_px.x >= l.canvas_x
	               && in.mouse_px.x <= l.canvas_x + l.canvas_w
	               && in.mouse_px.y >= l.top_bar_h
	               && in.mouse_px.y <= (float)l.fh - l.bottom_bar_h);

	if (in.scroll_y != 0.0f) {
		if (op_ == Op::DRAG_FROM_PALETTE) {
			op_start_snapshot_.orientation = std::fmod(op_start_snapshot_.orientation
				+ (in.scroll_y > 0.0f ? DEG_15 : -DEG_15), TWO_PI);
		} else {
			brush_sigma_ = std::clamp(brush_sigma_ + in.scroll_y * 0.02f, 0.05f, 0.5f);
		}
	}

	if (in.mouse_right_click && in_canvas && op_ == Op::NONE) {
		int hit = placed_part_hit_(in.mouse_px, l);
		if (hit >= 0) {
			int mi = mirror_partner_(hit);
			std::vector<int> victims = { hit };
			if (mi >= 0 && mi != hit) victims.push_back(mi);
			std::sort(victims.begin(), victims.end(), std::greater<int>());
			for (int v : victims) parts_.erase(parts_.begin() + v);
			status_ = "mod removed";
			return;
		}
	}

	if (in.mouse_left_click && op_ == Op::NONE) {
		int pal = palette_hit_(in, l);
		if (pal >= 0) {
			op_ = Op::DRAG_FROM_PALETTE;
			drag_palette_idx_ = pal;
			op_start_snapshot_ = sim::Part{};
			op_start_snapshot_.type = kPaletteTypes[pal];
			return;
		}
		if (!in_canvas) return;
		int hit = placed_part_hit_(in.mouse_px, l);
		if (hit >= 0) {
			selected_idx_ = hit;
			return;
		}
		selected_idx_ = -1;
		op_ = Op::SCULPT;
	}

	if (op_ == Op::SCULPT && in.mouse_left_down && in_canvas) {
		glm::vec2 loc = canvas_px_to_local_(in.mouse_px, l);
		float cursor_r = std::max(1.0f, glm::length(loc));
		float theta = std::atan2(loc.y, loc.x);
		float fidx = (theta / TWO_PI) * sim::RadialCell::N;
		if (fidx < 0.0f) fidx += sim::RadialCell::N;
		int idx = ((int)std::floor(fidx) % sim::RadialCell::N + sim::RadialCell::N)
		          % sim::RadialCell::N;
		float cur_r = cell_.r[idx];
		float delta = std::clamp(cursor_r - cur_r, -6.0f, 6.0f);
		sim::brushDeform(cell_, theta, delta, brush_sigma_);
		rebuild_polygon_();
		status_ = "sculpting";
		status_color_ = glm::vec3(0.85f);
	}

	if (!in.mouse_left_down && op_ != Op::NONE) {
		if (op_ == Op::DRAG_FROM_PALETTE) {
			sim::PartType pt = kPaletteTypes[drag_palette_idx_];
			glm::vec2 local = canvas_px_to_local_(in.mouse_px, l);
			if (in_canvas && pt_inside_cell_(local)) {
				place_part_pair_(pt, local, op_start_snapshot_.orientation, 1.0f);
				push_flash_(local_to_canvas_px_(local, l));
			} else {
				status_ = "drop on the body";
				status_color_ = glm::vec3(1.0f, 0.7f, 0.4f);
			}
			drag_palette_idx_ = -1;
		}
		op_ = Op::NONE;
	}
}

void LabScreen::push_flash_(glm::vec2 pos_px, glm::vec3 col) {
	Flash f; f.pos_px = pos_px; f.t_left = 0.35f; f.color = col;
	flashes_.push_back(f);
}

void LabScreen::draw_grid_(std::vector<ChalkStroke>& out, const Layout& l) {
	for (int r = 40; r <= 400; r += 40) {
		ChalkStroke s;
		s.color = glm::vec3(0.22f, 0.24f, 0.24f);
		s.half_width = 0.8f;
		const int N = 48;
		for (int i = 0; i <= N; i += 2) {
			float a = TWO_PI * (float)i / (float)N;
			s.points.push_back({l.canvas_cx + std::cos(a) * (float)r,
			                    l.canvas_cy + std::sin(a) * (float)r});
		}
		if (s.points.size() >= 2) out.push_back(std::move(s));
	}
}

void LabScreen::draw_mirror_line_(std::vector<ChalkStroke>& out, const Layout& l) {
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

void LabScreen::draw_cell_(std::vector<ChalkStroke>& out, const Layout& l, float) {
	if (local_poly_.size() < 3) return;
	ChalkStroke body;
	body.color = color_;
	body.half_width = 4.0f;
	for (auto& v : local_poly_) body.points.push_back(local_to_canvas_px_(v, l));
	if (!body.points.empty()) body.points.push_back(body.points.front());
	out.push_back(std::move(body));

	ChalkStroke c1, c2;
	c1.color = glm::vec3(1.0f); c1.half_width = 1.8f; c2 = c1;
	c1.points = { {l.canvas_cx - 6.0f, l.canvas_cy}, {l.canvas_cx + 6.0f, l.canvas_cy} };
	c2.points = { {l.canvas_cx, l.canvas_cy - 6.0f}, {l.canvas_cx, l.canvas_cy + 6.0f} };
	out.push_back(std::move(c1));
	out.push_back(std::move(c2));
}

void LabScreen::draw_parts_(std::vector<ChalkStroke>& out, const Layout& l, float time_s) {
	auto to_canvas = [&](glm::vec2 lv) { return local_to_canvas_px_(lv, l); };
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
	float top_r = cell_.r[sim::RadialCell::N / 4];
	float bot_r = cell_.r[(3 * sim::RadialCell::N) / 4];
	glm::vec2 head_px = local_to_canvas_px_(glm::vec2(0.0f,  top_r + 18.0f), l);
	glm::vec2 tail_px = local_to_canvas_px_(glm::vec2(0.0f, -bot_r - 18.0f), l);
	ChalkStroke s;
	s.color = glm::vec3(0.9f, 0.95f, 0.5f);
	s.half_width = 2.0f;
	s.points = { {head_px.x - 8.0f, head_px.y + 6.0f},
	             {head_px.x,        head_px.y - 6.0f},
	             {head_px.x + 8.0f, head_px.y + 6.0f} };
	out.push_back(std::move(s));
	ChalkStroke t;
	t.color = glm::vec3(0.65f, 0.7f, 0.9f);
	t.half_width = 2.0f;
	t.points = { {tail_px.x - 8.0f, tail_px.y - 6.0f},
	             {tail_px.x,        tail_px.y + 6.0f},
	             {tail_px.x + 8.0f, tail_px.y - 6.0f} };
	out.push_back(std::move(t));
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

void LabScreen::draw_brush_cursor_(std::vector<ChalkStroke>&, const Layout&, const LabInput&) {}
void LabScreen::draw_gizmo_(std::vector<ChalkStroke>&, const Layout&) {}

void LabScreen::draw_ghost_(std::vector<ChalkStroke>& out, const Layout& l,
                            const LabInput& in, float time_s) {
	if (op_ != Op::DRAG_FROM_PALETTE || drag_palette_idx_ < 0) return;
	sim::Part ghost;
	ghost.type = kPaletteTypes[drag_palette_idx_];
	glm::vec2 local = canvas_px_to_local_(in.mouse_px, l);
	ghost.anchor_local = local;
	ghost.orientation = op_start_snapshot_.orientation;
	auto xform = [&](glm::vec2 lv) {
		glm::vec2 rel = lv - local;
		return local_to_canvas_px_(local + rel * 1.5f, l);
	};
	std::vector<ChalkStroke> gh;
	appendPartStrokes({ghost}, glm::vec3(0.9f, 0.9f, 0.55f), xform, 1.0f, time_s, gh);
	for (auto& s : gh) { s.half_width *= 0.7f; out.push_back(std::move(s)); }
}

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
	if (!enabled)      bg = glm::vec4(0.06f, 0.06f, 0.06f, 0.7f);
	else if (selected) bg = glm::vec4(0.25f, 0.35f, 0.18f, 0.9f);
	else if (hover)    bg = glm::vec4(0.22f, 0.22f, 0.25f, 0.9f);
	else               bg = glm::vec4(0.10f, 0.10f, 0.12f, 0.8f);
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
	text_->drawText(status_, -0.28f, 0.78f, 0.85f,
		glm::vec4(status_color_, 1.0f), aspect);
}

void LabScreen::draw_brush_label_(const Layout& l, const LabInput&) {
	float aspect = window_->aspectRatio();
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
	float nx = (l.canvas_x / (float)ww) * 2.0f - 1.0f + 0.02f;
	float ny = 1.0f - (((float)l.fh - l.bottom_bar_h - 16.0f) / (float)wh) * 2.0f;
	text_->drawText("left-drag canvas to sculpt · drag parts from left palette · right-click a part to remove",
		nx, ny, 0.6f, glm::vec4(0.75f, 0.75f, 0.7f, 1.0f), aspect);
}

void LabScreen::draw_palette_(const Layout& l, const LabInput& in, float time_s) {
	float aspect = window_->aspectRatio();
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
	{
		float nx = (l.left_x / (float)ww) * 2.0f - 1.0f + 0.01f;
		float ny = 1.0f - (l.top_bar_h + 16.0f) / (float)wh * 2.0f;
		text_->drawText("PARTS", nx, ny, 1.1f, glm::vec4(1.0f), aspect);
	}
	float y0 = l.top_bar_h + 48.0f;
	float row_h = 44.0f;
	float row_x = l.left_x + 6.0f;
	float row_w = l.left_w - 12.0f;
	for (int i = 0; i < kPaletteCount; ++i) {
		float ry = y0 + (float)i * row_h;
		sim::PartType pt = kPaletteTypes[i];
		bool hover = pt_in_rect_(in.mouse_px, row_x, ry, row_w, row_h - 4.0f);
		bool dragging = (op_ == Op::DRAG_FROM_PALETTE && drag_palette_idx_ == i);
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
		std::vector<ChalkStroke> glyph;
		sim::Part single; single.type = pt; single.anchor_local = glm::vec2(0.0f);
		glm::vec2 icon_pos(row_x + 20.0f, ry + row_h * 0.45f);
		auto xform = [&](glm::vec2 v) { return icon_pos + glm::vec2(v.x, -v.y) * 1.4f; };
		appendPartStrokes({single}, color_, xform, 1.0f, time_s, glyph);
		if (!glyph.empty())
			renderer_->drawStrokes(glyph, nullptr, l.fw, l.fh);
	}
}

void LabScreen::draw_loadout_and_stats_(const Layout& l, const LabInput&) {
	float aspect = window_->aspectRatio();
	int ww, wh; glfwGetWindowSize(window_->handle(), &ww, &wh);
	float nx = (l.right_x / (float)ww) * 2.0f - 1.0f + 0.01f;
	float ny_top = 1.0f - (l.top_bar_h + 16.0f) / (float)wh * 2.0f;
	text_->drawText("LOADOUT", nx, ny_top, 1.1f, glm::vec4(1.0f), aspect);

	sim::Monster probe;
	probe.shape = local_poly_;
	probe.biomass = 25.0f;
	probe.parts = parts_;
	probe.refresh_stats();

	auto bar = [&](float y_ndc, const char* label, float frac, glm::vec4 col) {
		text_->drawText(label, nx, y_ndc, 0.75f,
			glm::vec4(0.85f, 0.85f, 0.85f, 1.0f), aspect);
		float bar_y = y_ndc - 0.012f;
		text_->drawRect(nx, bar_y, 0.18f, 0.010f, glm::vec4(0.15f, 0.15f, 0.15f, 0.8f));
		text_->drawRect(nx, bar_y, 0.18f * std::clamp(frac, 0.0f, 1.0f), 0.010f, col);
	};
	auto norm01 = [](float v, float lo, float hi) {
		return std::clamp((v - lo) / std::max(1e-3f, (hi - lo)), 0.0f, 1.0f);
	};
	float y = ny_top - 0.08f;
	bar(y,          "SPEED", norm01(probe.move_speed, sim::MOVE_MIN, sim::MOVE_MAX),
		glm::vec4(0.55f, 0.82f, 1.0f, 1.0f));
	bar(y - 0.055f, "TURN",  norm01(probe.turn_speed, sim::TURN_MIN, sim::TURN_MAX),
		glm::vec4(0.55f, 1.0f, 0.65f, 1.0f));
	bar(y - 0.110f, "HP",    std::clamp(probe.hp_max / 200.0f, 0.0f, 1.0f),
		glm::vec4(1.0f, 0.45f, 0.45f, 1.0f));
	bar(y - 0.165f, "DMG",   std::clamp((probe.part_effect.damage_mult - 1.0f) / 2.0f + 0.33f,
		0.0f, 1.0f), glm::vec4(1.0f, 0.75f, 0.35f, 1.0f));
	bar(y - 0.220f, "REGEN", std::clamp(probe.part_effect.regen_hps / 6.0f, 0.0f, 1.0f),
		glm::vec4(0.7f, 1.0f, 0.7f, 1.0f));

	// Budget bar — 2 segments (body / mods).
	float ybud = y - 0.30f;
	text_->drawText("BUDGET", nx, ybud, 0.9f, glm::vec4(1.0f), aspect);
	float bar_y = ybud - 0.022f;
	float bud = budget_();
	float body_w = (cell_cost_() / bud) * 0.18f;
	float parts_w = (parts_cost_() / bud) * 0.18f;
	text_->drawRect(nx, bar_y, 0.18f, 0.014f, glm::vec4(0.15f, 0.15f, 0.15f, 0.8f));
	text_->drawRect(nx,          bar_y, std::min(0.18f, body_w),  0.014f, glm::vec4(0.55f, 0.82f, 1.0f, 1.0f));
	text_->drawRect(nx + body_w, bar_y, std::min(0.18f - body_w, parts_w), 0.014f, glm::vec4(1.0f, 0.75f, 0.35f, 1.0f));
	char bb[80];
	std::snprintf(bb, sizeof(bb), "%.0f / %.0f bm", cell_cost_() + parts_cost_(), bud);
	glm::vec4 bcol = (cell_cost_() + parts_cost_() > bud)
		? glm::vec4(1.0f, 0.5f, 0.5f, 1.0f) : glm::vec4(0.85f, 0.85f, 0.85f, 1.0f);
	text_->drawText(bb, nx, bar_y - 0.030f, 0.75f, bcol, aspect);
}

void LabScreen::draw_bottom_buttons_(const Layout& l, const LabInput& in, LabOutcome& outc) {
	float by = (float)l.fh - l.bottom_bar_h + 8.0f;
	float bh = l.bottom_bar_h - 16.0f;
	float bw = std::min(200.0f, l.canvas_w * 0.22f);
	float gap = 12.0f;
	float total = bw * 3.0f + gap * 2.0f;
	float bx = l.canvas_cx - total * 0.5f;
	if (pixel_button_(bx + 0 * (bw + gap), by, bw, bh, "BACK", true, in)) outc = LabOutcome::BACK;
	if (pixel_button_(bx + 1 * (bw + gap), by, bw, bh, "RESET", true, in)) reset();
	bool use_ok = total_cost_() <= budget_() && !local_poly_.empty();
	if (pixel_button_(bx + 2 * (bw + gap), by, bw, bh, "USE THIS MONSTER", use_ok, in))
		outc = LabOutcome::USE;
}

LabOutcome LabScreen::update(float dt, const LabInput& in) {
	time_acc_ += dt;
	Layout l = compute_layout_();
	LabOutcome outc = LabOutcome::NONE;

	for (int key : in.keys_pressed) {
		if (key == GLFW_KEY_ESCAPE) outc = LabOutcome::BACK;
	}

	handle_input_(in, l, dt);

	for (auto& f : flashes_) f.t_left -= dt;
	flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(),
		[](const Flash& f){ return f.t_left <= 0.0f; }), flashes_.end());

	std::vector<ChalkStroke> strokes;
	draw_grid_(strokes, l);
	draw_mirror_line_(strokes, l);
	draw_cell_(strokes, l, time_acc_);
	draw_parts_(strokes, l, time_acc_);
	draw_head_tail_markers_(strokes, l);
	draw_flashes_(strokes);
	draw_ghost_(strokes, l, in, time_acc_);

	if (!strokes.empty()) renderer_->drawStrokes(strokes, nullptr, l.fw, l.fh);

	draw_title_bar_();
	draw_palette_(l, in, time_acc_);
	draw_loadout_and_stats_(l, in);
	draw_brush_label_(l, in);
	draw_bottom_buttons_(l, in, outc);
	return outc;
}

void LabScreen::seed_lab_for_screenshot() {
	reset();
	sim::brushDeform(cell_,  PI * 0.5f,  22.0f, 0.22f);
	sim::brushDeform(cell_, -PI * 0.5f,  20.0f, 0.22f);
	sim::brushDeform(cell_,  0.0f,      -8.0f,  0.30f);
	rebuild_polygon_();
	auto place_pair = [&](sim::PartType t, glm::vec2 ra, float rot) {
		sim::Part a; a.type = t; a.anchor_local = ra; a.orientation = rot;
		sim::Part b = a; b.anchor_local = mirror_anchor_(ra); b.orientation = PI - rot;
		parts_.push_back(a); parts_.push_back(b);
	};
	place_pair(sim::PartType::SPIKE,    glm::vec2( 8.0f,  42.0f),  PI * 0.5f);
	place_pair(sim::PartType::FLAGELLA, glm::vec2( 6.0f, -40.0f), -PI * 0.5f);
	place_pair(sim::PartType::ARMOR,    glm::vec2(24.0f,   0.0f),  0.0f);
	status_ = "seeded: spike, flagella, armor (auto-mirrored)";
}

} // namespace civcraft::cellcraft
