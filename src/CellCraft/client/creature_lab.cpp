// CellCraft — creature lab. See creature_lab.h.

#include "CellCraft/client/creature_lab.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "CellCraft/client/name_generator.h"
#include "CellCraft/client/part_render.h"
#include "CellCraft/client/ui_theme.h"
#include "CellCraft/sim/monster.h"
#include "CellCraft/sim/part_stats.h"
#include "CellCraft/sim/tuning.h"
#include "client/text.h"
#include "client/window.h"

namespace civcraft::cellcraft {

namespace {
constexpr float TWO_PI = 6.28318530718f;
constexpr float PI     = 3.14159265359f;
constexpr float PX_PER_UNIT = 2.6f; // canvas zoom

const sim::PartType kPaletteTypes[] = {
	sim::PartType::SPIKE,       sim::PartType::TEETH,
	sim::PartType::FLAGELLA,    sim::PartType::CILIA,
	sim::PartType::POISON,      sim::PartType::ARMOR,
	sim::PartType::VENOM_SPIKE, sim::PartType::REGEN,
	sim::PartType::EYES,        sim::PartType::HORN,
	sim::PartType::MOUTH,
};
constexpr int kPaletteCount = (int)(sizeof(kPaletteTypes) / sizeof(kPaletteTypes[0]));

// Saturated modern kids-game palette (6 slots + warm white + charcoal).
// Mirrors ui::STROKE_PALETTE with two neutrals for creature recoloring.
const glm::vec3 kColorPalette[8] = {
	{1.00f, 0.31f, 0.55f}, // hot pink  #FF4F8B
	{0.31f, 0.76f, 1.00f}, // cyan      #4FC1FF
	{0.50f, 0.83f, 0.31f}, // lime      #7FD44F
	{1.00f, 0.76f, 0.31f}, // gold      #FFC34F
	{0.77f, 0.31f, 1.00f}, // magenta   #C44FFF
	{1.00f, 0.56f, 0.31f}, // orange    #FF8F4F
	{1.00f, 0.99f, 0.96f}, // warm white
	{0.20f, 0.18f, 0.26f}, // charcoal
};
constexpr int kColorPaletteCount = 8;

const char* kSpeechLines[4] = { "HIYA!", "PICK ME!", "LET'S GO!", "I'M READY!" };

int part_stack_count(const std::vector<sim::Part>& parts, sim::PartType t) {
	int n = 0;
	for (auto& p : parts) if (p.type == t) ++n;
	return n;
}

int part_stack_cap(sim::PartType t) {
	using P = sim::PartType;
	switch (t) {
	case P::FLAGELLA:    return sim::PART_FLAGELLA_MAX_STACK * 2;
	case P::ARMOR:       return sim::PART_ARMOR_MAX_STACK * 2;
	case P::CILIA:       return sim::PART_CILIA_MAX_STACK * 2;
	case P::REGEN:       return sim::PART_REGEN_MAX_STACK * 2;
	case P::MOUTH:       return sim::PART_MOUTH_MAX_STACK * 2;
	case P::HORN:        return sim::PART_HORN_MAX_STACK * 2;
	case P::VENOM_SPIKE: return sim::PART_VENOM_MAX_STACK * 2;
	case P::EYES:        return sim::PART_EYES_MAX_STACK * 2;
	case P::SPIKE:       return 8;
	case P::TEETH:       return 4;
	case P::POISON:      return 2;
	default: return 4;
	}
}

bool point_in_rect(glm::vec2 p, float x, float y, float w, float h) {
	return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
}

} // namespace

void CreatureLab::init(Window* w, ChalkRenderer* r, TextRenderer* t) {
	window_ = w; renderer_ = r; text_ = t;
	reset();
}

void CreatureLab::reset() {
	cell_.init_circle(40.0f);
	parts_.clear();
	rebuild_polygon_();
	color_ = glm::vec3(0.95f, 0.65f, 0.85f);
	name_ = "Bloopy Blob";
	drawer_ = Drawer::PARTS;
	selected_part_idx_ = -1;
	sculpting_ = false;
	undo_.clear();
	speech_t_left_ = 0.0f;
	speech_t_next_ = 8.0f;
	jar_shake_t_ = 0.0f;
	boing_part_idx_ = -1; boing_t_left_ = 0.0f;
	time_acc_ = 0.0f;
	wobble_phase_ = 0.0f;
	refresh_stats_();
}

void CreatureLab::load_starter(const sim::RadialCell& c, const std::vector<sim::Part>& p,
                          glm::vec3 col, const std::string& nm) {
	reset();
	cell_ = c;
	parts_ = p;
	color_ = col;
	name_ = nm;
	rebuild_polygon_();
	clamp_part_to_canvas_();
	refresh_stats_();
}

void CreatureLab::rebuild_polygon_() {
	local_poly_ = sim::cellToPolygon(cell_, 1);
}

void CreatureLab::push_undo_() {
	UndoEntry e; e.cell = cell_; e.parts = parts_; e.color = color_;
	undo_.push_back(std::move(e));
	if (undo_.size() > 32) undo_.erase(undo_.begin());
}

CreatureLab::Layout CreatureLab::compute_layout_() const {
	Layout l;
	glfwGetFramebufferSize(window_->handle(), &l.fw, &l.fh);
	l.left_w = (float)l.fw * 0.16f;
	l.right_w = (float)l.fw * 0.20f;
	l.left_x = 0.0f;
	l.right_x = (float)l.fw - l.right_w;
	l.canvas_x = l.left_w;
	l.canvas_w = (float)l.fw - l.left_w - l.right_w;
	l.top_bar_h = (float)l.fh * 0.10f;
	l.bottom_bar_h = (float)l.fh * 0.13f;
	l.canvas_cx = l.canvas_x + l.canvas_w * 0.5f;
	l.canvas_cy = l.top_bar_h + ((float)l.fh - l.top_bar_h - l.bottom_bar_h) * 0.5f;
	// Drawer occupies the lower half of the left rail.
	l.drawer_y = l.top_bar_h + 3.0f * 96.0f + 30.0f;
	l.drawer_h = (float)l.fh - l.bottom_bar_h - l.drawer_y - 10.0f;
	return l;
}

glm::vec2 CreatureLab::local_to_canvas_px_(glm::vec2 v, const Layout& l) const {
	return glm::vec2(l.canvas_cx + v.x * PX_PER_UNIT,
	                 l.canvas_cy - v.y * PX_PER_UNIT);
}
glm::vec2 CreatureLab::canvas_px_to_local_(glm::vec2 px, const Layout& l) const {
	return glm::vec2((px.x - l.canvas_cx) / PX_PER_UNIT,
	                 -(px.y - l.canvas_cy) / PX_PER_UNIT);
}

float CreatureLab::total_cost_() const {
	float perim = sim::cellPerimeter(cell_);
	float c = perim * sim::BODY_COST_PER_PX;
	for (auto& p : parts_) c += sim::part_cost(p.type) * p.scale * p.scale;
	return c;
}
float CreatureLab::budget_() const { return sim::BODY_BUDGET_BIOMASS; }
float CreatureLab::fullness_frac_() const {
	float b = budget_();
	if (b <= 0.0f) return 0.0f;
	float f = total_cost_() / b;
	return std::clamp(f, 0.0f, 1.0f);
}

void CreatureLab::refresh_stats_() {
	// Build a probe Monster to derive numbers consistently with sim.
	sim::Monster m;
	m.shape = local_poly_;
	m.parts = parts_;
	m.biomass = 30.0f;
	m.refresh_stats();
	stat_speed_ = std::clamp((m.move_speed - 20.0f) / (400.0f - 20.0f), 0.0f, 1.0f);
	stat_tough_ = std::clamp((m.hp_max     - 40.0f) / (200.0f - 40.0f), 0.0f, 1.0f);
	int spike_count = 0;
	for (auto& p : parts_) if (p.type == sim::PartType::SPIKE || p.type == sim::PartType::HORN) ++spike_count;
	float bite = m.part_effect.damage_mult * (1.0f + spike_count * 0.1f);
	stat_bite_  = std::clamp(bite / 2.0f, 0.0f, 1.0f);
}

bool CreatureLab::tap_place_part_(sim::PartType t) {
	int cap = part_stack_cap(t);
	if (part_stack_count(parts_, t) + 2 > cap) {
		// Stacks paired (mirror), two slots needed. Boing.
		jar_shake_t_ = 0.4f;
		return false;
	}
	// Sensible anchors (in local space).
	// Cell radius rough: average max along axes
	float head_r = cell_.r[sim::RadialCell::N / 4];     // θ ≈ π/2 → +y
	float tail_r = cell_.r[3 * sim::RadialCell::N / 4]; // θ ≈ -π/2 → -y
	float side_r = cell_.r[0];                          // θ = 0 → +x
	(void)tail_r; (void)side_r;

	glm::vec2 base; float ori = 0.0f; float scale = 1.0f;
	int existing_same = part_stack_count(parts_, t);
	using P = sim::PartType;
	switch (t) {
	case P::SPIKE:
	case P::HORN:
	case P::TEETH:
	case P::VENOM_SPIKE: {
		// Front: ±15° offsets from +y based on existing count.
		int slot = existing_same / 2; // each pair occupies one slot
		float off = (slot + 1) * (PI / 12.0f); // ±15° per slot
		float ang = PI * 0.5f - off; // right-side
		float r   = head_r * 0.92f;
		base = glm::vec2(std::cos(ang) * r, std::sin(ang) * r);
		ori  = ang;
		break;
	}
	case P::EYES: {
		int slot = existing_same / 2;
		float off = (slot + 1) * (PI / 14.0f);
		float ang = PI * 0.5f - off;
		float r   = head_r * 0.55f;
		base = glm::vec2(std::cos(ang) * r, std::sin(ang) * r);
		ori  = ang;
		break;
	}
	case P::MOUTH: {
		float ang = PI * 0.5f - PI / 18.0f;
		float r   = head_r * 0.45f;
		base = glm::vec2(std::cos(ang) * r, std::sin(ang) * r);
		ori  = ang;
		break;
	}
	case P::FLAGELLA:
	case P::CILIA: {
		int slot = existing_same / 2;
		float off = (slot + 1) * (PI / 14.0f);
		float ang = -PI * 0.5f + off;
		float r   = head_r * 0.95f;
		base = glm::vec2(std::cos(ang) * r, std::sin(ang) * r);
		ori  = ang;
		break;
	}
	case P::ARMOR: {
		int slot = existing_same / 2;
		float off = slot * (PI / 8.0f);
		float ang = 0.0f - off; // start on +x, work down
		float r   = side_r * 0.95f;
		base = glm::vec2(std::cos(ang) * r, std::sin(ang) * r);
		ori  = ang;
		break;
	}
	case P::POISON:
	case P::REGEN: {
		// Center.
		base = glm::vec2(8.0f, 0.0f);
		ori  = 0.0f;
		break;
	}
	default:
		base = glm::vec2(10.0f, 0.0f);
		break;
	}
	push_undo_();
	sim::Part a; a.type = t; a.anchor_local = base; a.orientation = ori; a.scale = scale;
	sim::Part b = a;
	b.anchor_local = glm::vec2(-base.x, base.y);
	b.orientation  = PI - ori;
	parts_.push_back(a);
	parts_.push_back(b);
	refresh_stats_();
	return true;
}

void CreatureLab::clamp_part_to_canvas_() {
	// Nothing strict — parts can sit just outside the cell (spikes!).
}

int CreatureLab::placed_part_hit_(glm::vec2 px, const Layout& l) const {
	float best_d = 1e9f; int best = -1;
	for (size_t i = 0; i < parts_.size(); ++i) {
		glm::vec2 sp = local_to_canvas_px_(parts_[i].anchor_local, l);
		float d = glm::length(sp - px);
		float thresh = 24.0f * std::max(0.5f, parts_[i].scale);
		if (d < thresh && d < best_d) { best_d = d; best = (int)i; }
	}
	return best;
}

bool CreatureLab::pixel_button_(float x, float y, float w, float h, const char* label,
                           bool enabled, const LabInput& in, glm::vec3 fill) {
	int fw, fh; glfwGetFramebufferSize(window_->handle(), &fw, &fh);
	auto px2ndc = [&](glm::vec2 p) { return glm::vec2(p.x / fw * 2.0f - 1.0f, 1.0f - p.y / fh * 2.0f); };
	glm::vec2 a = px2ndc(glm::vec2(x, y + h));   // bottom-left in NDC
	glm::vec2 b = px2ndc(glm::vec2(x + w, y));   // top-right in NDC
	float nx = a.x, ny = a.y, nw = b.x - a.x, nh = b.y - a.y;
	bool hover = enabled && in.mouse_px.x >= x && in.mouse_px.x <= x + w
	          && in.mouse_px.y >= y && in.mouse_px.y <= y + h;
	glm::vec3 fill_top = enabled ? (fill * (hover ? 1.14f : 1.0f)) : glm::vec3(0.78f, 0.75f, 0.72f);
	glm::vec3 fill_bot = glm::max(fill_top * 0.82f, glm::vec3(0.0f));
	float alpha = enabled ? 0.98f : 0.55f;

	// Drop shadow.
	text_->drawRect(nx + 0.004f, ny - 0.010f, nw, nh, ui::SHADOW);
	// 8-stripe faux gradient (top→bottom).
	for (int si = 0; si < 8; ++si) {
		float t0 = (float)si / 8.0f, t1 = (float)(si + 1) / 8.0f;
		glm::vec3 c = glm::mix(fill_top, fill_bot, 0.5f * (t0 + t1));
		float sy = ny + nh * (1.0f - t1);
		float sh = nh * (t1 - t0) + 0.0005f;
		text_->drawRect(nx, sy, nw, sh, glm::vec4(c, alpha));
	}
	// Top highlight.
	text_->drawRect(nx, ny + nh - nh * 0.14f, nw, nh * 0.09f,
		glm::vec4(1.0f, 1.0f, 1.0f, 0.22f));
	// Charcoal outline.
	glm::vec4 edge = ui::OUTLINE; if (!enabled) edge.a = 0.4f;
	float t = 0.004f;
	text_->drawRect(nx, ny,         nw, t, edge);
	text_->drawRect(nx, ny + nh - t, nw, t, edge);
	text_->drawRect(nx, ny,         t, nh, edge);
	text_->drawRect(nx + nw - t, ny, t, nh, edge);
	// Rounded corner cut-outs (use BG_CREAM so corners blend into the card).
	{
		float r = 0.006f;
		glm::vec4 bgc = ui::BG_CREAM;
		text_->drawRect(nx, ny + nh - r, r, r, bgc);
		text_->drawRect(nx + nw - r, ny + nh - r, r, r, bgc);
		text_->drawRect(nx, ny, r, r, bgc);
		text_->drawRect(nx + nw - r, ny, r, r, bgc);
	}
	if (label && *label) {
		float aspect = (float)fw / (float)fh;
		float scale = h / 64.0f * 1.6f;
		if (scale > 2.4f) scale = 2.4f;
		float label_w = (float)std::strlen(label) * 0.018f * scale;
		if (label_w > nw * 0.92f) {
			scale *= (nw * 0.92f) / std::max(0.001f, label_w);
			label_w = (float)std::strlen(label) * 0.018f * scale;
		}
		float tx = nx + (nw - label_w) * 0.5f;
		float ty = ny + (nh - 0.030f * scale) * 0.5f;
		// Outlined label — dark shadow + light fill on colored buttons,
		// or dark fill on cream buttons.
		float lum = 0.299f*fill.r + 0.587f*fill.g + 0.114f*fill.b;
		glm::vec4 lfill = (lum > 0.75f) ? ui::TEXT_DARK : ui::TEXT_LIGHT;
		glm::vec4 lshadow = ui::OUTLINE; lshadow.a = enabled ? 0.85f : 0.4f;
		text_->drawText(label, tx + 0.003f, ty - 0.004f, scale, lshadow, aspect);
		text_->drawText(label, tx,          ty,          scale, lfill,   aspect);
	}
	return hover && in.mouse_left_click;
}

LabOutcome CreatureLab::update(float dt, const LabInput& in) {
	time_acc_ += dt;
	wobble_phase_ += dt;
	jar_shake_t_  = std::max(0.0f, jar_shake_t_ - dt);
	boing_t_left_ = std::max(0.0f, boing_t_left_ - dt);
	if (boing_t_left_ <= 0.0f) boing_part_idx_ = -1;

	// Speech bubble timer.
	if (speech_enabled_) {
		if (speech_t_left_ > 0.0f) {
			speech_t_left_ -= dt;
		} else {
			speech_t_next_ -= dt;
			if (speech_t_next_ <= 0.0f) {
				int idx = (int)(time_acc_ * 7.0f) % 4;
				speech_text_ = kSpeechLines[idx];
				speech_t_left_ = 2.5f;
				speech_t_next_ = 8.0f + std::fmod(time_acc_ * 1.7f, 7.0f);
			}
		}
	}

	LabOutcome outc = LabOutcome::NONE;
	Layout l = compute_layout_();

	// ESC → back
	for (int k : in.keys_pressed) {
		if (k == GLFW_KEY_ESCAPE) outc = LabOutcome::BACK;
	}

	draw_top_bar_(l, in);
	draw_left_rail_(l, in);
	draw_right_rail_(l);
	draw_canvas_(l, in, dt);
	draw_drawer_(l, in);
	draw_bottom_bar_(l, in, outc);

	return outc;
}

void CreatureLab::draw_top_bar_(const Layout& l, const LabInput& in) {
	int fw = l.fw, fh = l.fh;
	float aspect = (float)fw / (float)fh;
	auto px2ndc = [&](float x, float y) {
		return glm::vec2(x / fw * 2.0f - 1.0f, 1.0f - y / fh * 2.0f);
	};
	glm::vec2 a = px2ndc(0.0f, l.top_bar_h);
	glm::vec2 b = px2ndc((float)fw, 0.0f);
	// Top bar — cream card with charcoal underline.
	text_->drawRect(a.x, a.y, b.x - a.x, b.y - a.y, ui::CARD_FILL);
	text_->drawRect(a.x, a.y, b.x - a.x, 0.004f, ui::CARD_STROKE);
	// Centered name + pencil
	float scale = l.top_bar_h / 32.0f;
	if (scale > 3.5f) scale = 3.5f;
	float w_chars = (float)name_.size() * 0.018f * scale;
	float nx = ((float)fw * 0.5f - (w_chars * fw * 0.5f)) ;
	(void)nx;
	float ndc_y = (1.0f - (l.top_bar_h * 0.30f / fh) * 2.0f);
	// Colored shadow + dark fill title treatment.
	glm::vec4 nshadow = ui::ACCENT_PINK; nshadow.a = 0.75f;
	text_->drawText(name_, -w_chars * 0.5f + 0.005f, ndc_y - 0.05f - 0.007f, scale, nshadow, aspect);
	text_->drawText(name_, -w_chars * 0.5f,          ndc_y - 0.05f,          scale, ui::TEXT_DARK, aspect);
	// Pencil button (a small box right of the name)
	float btn_w = l.top_bar_h * 0.6f;
	float btn_h = l.top_bar_h * 0.6f;
	float btn_x = (float)fw * 0.5f + (w_chars * fw * 0.5f) + 16.0f;
	float btn_y = l.top_bar_h * 0.20f;
	if (pixel_button_(btn_x, btn_y, btn_w, btn_h, "EDIT",
	                  true, in, glm::vec3(0.30f, 0.40f, 0.55f))) {
		std::mt19937 rng((uint32_t)(time_acc_ * 1000.0f + 7));
		name_ = generateName(rng);
	}
}

void CreatureLab::draw_left_rail_(const Layout& l, const LabInput& in) {
	int fw = l.fw, fh = l.fh;
	auto px2ndc = [&](float x, float y) {
		return glm::vec2(x / fw * 2.0f - 1.0f, 1.0f - y / fh * 2.0f);
	};
	glm::vec2 a = px2ndc(0.0f, (float)fh);
	glm::vec2 b = px2ndc(l.left_w, l.top_bar_h);
	// Left rail — lavender-tinted card with pink stroke on the right edge.
	text_->drawRect(a.x, a.y, b.x - a.x, b.y - a.y, ui::PANEL_TINT);
	text_->drawRect(b.x - 0.004f, a.y, 0.004f, b.y - a.y, ui::ACCENT_PINK);

	const char* labels[3] = { "SHAPE", "PARTS", "COLOR" };
	Drawer drawers[3] = { Drawer::SHAPE, Drawer::PARTS, Drawer::COLOR };
	float btn_h = 96.0f;
	float btn_w = l.left_w - 24.0f;
	float bx = 12.0f;
	float by0 = l.top_bar_h + 12.0f;
	for (int i = 0; i < 3; ++i) {
		float by = by0 + i * (btn_h + 8.0f);
		// Active drawer = hot pink; inactive = cream. Saturated, not drab.
		glm::vec3 fill = (drawer_ == drawers[i])
			? glm::vec3(1.00f, 0.31f, 0.55f)
			: glm::vec3(1.00f, 0.99f, 0.96f);
		if (pixel_button_(bx, by, btn_w, btn_h, labels[i], true, in, fill)) {
			drawer_ = drawers[i];
		}
	}
}

void CreatureLab::draw_right_rail_(const Layout& l) {
	int fw = l.fw, fh = l.fh;
	float aspect = (float)fw / (float)fh;
	auto px2ndc = [&](float x, float y) {
		return glm::vec2(x / fw * 2.0f - 1.0f, 1.0f - y / fh * 2.0f);
	};
	glm::vec2 a = px2ndc(l.right_x, (float)fh);
	glm::vec2 b = px2ndc((float)fw, l.top_bar_h);
	text_->drawRect(a.x, a.y, b.x - a.x, b.y - a.y, ui::PANEL_TINT);
	text_->drawRect(a.x, a.y, 0.004f, b.y - a.y, ui::ACCENT_CYAN);

	// Fullness jar — large, ~220px tall.
	float jar_w = l.right_w - 60.0f;
	float jar_h = 220.0f;
	float jar_x = l.right_x + 30.0f;
	float jar_y = l.top_bar_h + 30.0f;
	if (jar_shake_t_ > 0.0f) {
		float k = jar_shake_t_ / 0.4f;
		jar_x += std::sin(time_acc_ * 60.0f) * 6.0f * k;
	}
	glm::vec2 ja = px2ndc(jar_x, jar_y + jar_h);
	glm::vec2 jb = px2ndc(jar_x + jar_w, jar_y);
	text_->drawRect(ja.x, ja.y, jb.x - ja.x, jb.y - ja.y,
		glm::vec4(0.96f, 0.92f, 0.85f, 0.95f));
	float frac = fullness_frac_();
	glm::vec4 fillcol = (frac > 0.85f) ? glm::vec4(0.95f, 0.40f, 0.40f, 1.0f)
	                  : (frac > 0.60f) ? glm::vec4(0.95f, 0.85f, 0.45f, 1.0f)
	                  :                   glm::vec4(0.55f, 0.95f, 0.55f, 1.0f);
	float fill_h = jar_h * frac;
	glm::vec2 fa = px2ndc(jar_x + 4.0f, jar_y + jar_h - 4.0f);
	glm::vec2 fb = px2ndc(jar_x + jar_w - 4.0f, jar_y + jar_h - 4.0f - fill_h);
	if (fill_h > 0.5f) text_->drawRect(fa.x, fa.y, fb.x - fa.x, fb.y - fa.y, fillcol);
	// Border
	glm::vec4 edge = ui::CARD_STROKE;
	float t = 0.005f;
	text_->drawRect(ja.x, ja.y, jb.x - ja.x, t, edge);
	text_->drawRect(ja.x, jb.y - t, jb.x - ja.x, t, edge);
	text_->drawRect(ja.x, ja.y, t, jb.y - ja.y, edge);
	text_->drawRect(jb.x - t, ja.y, t, jb.y - ja.y, edge);
	// Label
	float ndc_label_y = 1.0f - (jar_y + jar_h + 10.0f) / fh * 2.0f;
	text_->drawText("FULLNESS", ja.x + 0.005f, ndc_label_y, 1.4f,
		ui::TEXT_DARK, aspect);

	// 3 stat bars below.
	float bar_x = jar_x;
	float bar_w = jar_w;
	float bar_h = 28.0f;
	float bar_y0 = jar_y + jar_h + 64.0f;
	const char* labels[3] = { "SPEED", "TOUGH", "BITE" };
	float fracs[3] = { stat_speed_, stat_tough_, stat_bite_ };
	glm::vec4 cols[3] = {
		glm::vec4(0.55f, 0.85f, 1.0f, 1.0f),
		glm::vec4(1.0f, 0.78f, 0.45f, 1.0f),
		glm::vec4(1.0f, 0.45f, 0.45f, 1.0f),
	};
	for (int i = 0; i < 3; ++i) {
		float by = bar_y0 + i * (bar_h + 36.0f);
		// label
		float lndc_y = 1.0f - (by - 6.0f) / fh * 2.0f;
		text_->drawText(labels[i], px2ndc(bar_x, 0.0f).x + 0.005f, lndc_y, 1.3f,
			ui::TEXT_DARK, aspect);
		// bar bg
		glm::vec2 ba = px2ndc(bar_x, by + bar_h);
		glm::vec2 bb = px2ndc(bar_x + bar_w, by);
		text_->drawRect(ba.x, ba.y, bb.x - ba.x, bb.y - ba.y,
			glm::vec4(0.96f, 0.92f, 0.85f, 0.9f));
		// fill
		glm::vec2 fa2 = px2ndc(bar_x + 3.0f, by + bar_h - 3.0f);
		glm::vec2 fb2 = px2ndc(bar_x + 3.0f + (bar_w - 6.0f) * fracs[i], by + 3.0f);
		text_->drawRect(fa2.x, fa2.y, fb2.x - fa2.x, fb2.y - fa2.y, cols[i]);
	}
}

void CreatureLab::draw_canvas_(const Layout& l, const LabInput& in, float dt) {
	(void)dt;
	int fw = l.fw, fh = l.fh;
	auto px2ndc = [&](float x, float y) {
		return glm::vec2(x / fw * 2.0f - 1.0f, 1.0f - y / fh * 2.0f);
	};
	float aspect = (float)fw / (float)fh;

	// Click handling — has priority over sculpt.
	bool over_canvas = in.mouse_px.x >= l.canvas_x
	                && in.mouse_px.x <= l.canvas_x + l.canvas_w
	                && in.mouse_px.y >= l.top_bar_h
	                && in.mouse_px.y <= (float)l.fh - l.bottom_bar_h;

	// Selected-part action buttons (drawn below) — handle hits first.
	bool consumed_click = false;
	if (selected_part_idx_ >= 0 && selected_part_idx_ < (int)parts_.size()) {
		// Computed below in draw section; we mirror coords here.
		auto& p = parts_[selected_part_idx_];
		glm::vec2 sp = local_to_canvas_px_(p.anchor_local, l);
		struct Btn { float dx, dy; const char* label; int act; };
		Btn btns[4] = {
			{ 0.0f, -56.0f, "+", 0 },
			{ 0.0f,  56.0f, "-", 1 },
			{ -56.0f, 0.0f, "ROT", 2 },
			{  56.0f, 0.0f, "DEL", 3 },
		};
		for (auto& bb : btns) {
			float bx = sp.x + bb.dx - 24.0f, by = sp.y + bb.dy - 24.0f;
			glm::vec3 fill = (bb.act == 3) ? glm::vec3(0.85f, 0.30f, 0.30f)
			                                : glm::vec3(0.30f, 0.40f, 0.55f);
			if (pixel_button_(bx, by, 48.0f, 48.0f, bb.label, true, in, fill)) {
				push_undo_();
				if (bb.act == 0) { p.scale *= 1.20f; if (p.scale > 2.5f) p.scale = 2.5f; }
				else if (bb.act == 1) { p.scale *= 0.83f; if (p.scale < 0.4f) p.scale = 0.4f; }
				else if (bb.act == 2) { p.orientation += PI / 8.0f; }
				else if (bb.act == 3) {
					// Remove this and its mirror partner (closest with mirrored anchor + same type).
					glm::vec2 mirror = glm::vec2(-p.anchor_local.x, p.anchor_local.y);
					int partner = -1; float bd = 1e9f;
					for (int j = 0; j < (int)parts_.size(); ++j) {
						if (j == selected_part_idx_) continue;
						if (parts_[j].type != p.type) continue;
						float d = glm::length(parts_[j].anchor_local - mirror);
						if (d < bd) { bd = d; partner = j; }
					}
					std::vector<int> to_remove = { selected_part_idx_ };
					if (partner >= 0 && bd < 12.0f) to_remove.push_back(partner);
					std::sort(to_remove.begin(), to_remove.end(), std::greater<int>());
					for (int idx : to_remove) parts_.erase(parts_.begin() + idx);
					selected_part_idx_ = -1;
				}
				refresh_stats_();
				consumed_click = true;
				break;
			}
		}
	}

	if (over_canvas && in.mouse_left_click && !consumed_click) {
		int hit = placed_part_hit_(in.mouse_px, l);
		if (hit >= 0) {
			selected_part_idx_ = hit;
			selected_buttons_t_ = 0.0f;
			consumed_click = true;
		} else {
			selected_part_idx_ = -1;
		}
	}

	// Sculpt: left-drag while over canvas (and no click consumption).
	if (over_canvas && in.mouse_left_down && selected_part_idx_ < 0) {
		// Compute angle from canvas center, push outward by mouse-radius delta.
		glm::vec2 lp = canvas_px_to_local_(in.mouse_px, l);
		float r_mouse = glm::length(lp);
		float theta   = std::atan2(lp.y, lp.x);
		// Pull cell radius toward mouse radius at theta.
		// Compute current radius at theta.
		float a_idx = theta / TWO_PI * sim::RadialCell::N;
		while (a_idx < 0) a_idx += sim::RadialCell::N;
		int i0 = ((int)a_idx) % sim::RadialCell::N;
		float cur_r = cell_.r[i0];
		float delta = (r_mouse - cur_r) * 0.10f;
		if (std::fabs(delta) > 0.5f) {
			if (!sculpting_) push_undo_();
			sculpting_ = true;
			sim::brushDeform(cell_, theta, delta, 0.18f);
			rebuild_polygon_();
			refresh_stats_();
		}
	} else {
		sculpting_ = false;
	}

	// ---- Render canvas border + creature.
	std::vector<ChalkStroke> strokes;

	// Canvas backdrop subtle
	{
		glm::vec2 a = px2ndc(l.canvas_x, (float)l.fh - l.bottom_bar_h);
		glm::vec2 b = px2ndc(l.canvas_x + l.canvas_w, l.top_bar_h);
		// Canvas backdrop — cream card so the creature pops against a
		// bright surface rather than a dark slate.
		text_->drawRect(a.x, a.y, b.x - a.x, b.y - a.y,
			glm::vec4(1.0f, 0.99f, 0.95f, 0.65f));
	}

	// Mirror axis dashed
	{
		ChalkStroke s; s.color = glm::vec3(0.55f, 0.55f, 0.50f); s.half_width = 1.2f;
		float top_y = l.top_bar_h + 20.0f;
		float bot_y = (float)l.fh - l.bottom_bar_h - 20.0f;
		for (float y = top_y; y < bot_y; y += 16.0f) {
			s.points.clear();
			s.points.push_back({l.canvas_cx, y});
			s.points.push_back({l.canvas_cx, y + 8.0f});
			strokes.push_back(s);
		}
	}

	// HEAD/TAIL arrows
	{
		ChalkStroke s; s.color = glm::vec3(0.95f, 0.85f, 0.45f); s.half_width = 2.5f;
		float top_y = l.top_bar_h + 16.0f;
		s.points = {
			{l.canvas_cx, top_y + 24.0f}, {l.canvas_cx, top_y},
			{l.canvas_cx - 8.0f, top_y + 8.0f}, {l.canvas_cx, top_y},
			{l.canvas_cx + 8.0f, top_y + 8.0f},
		};
		strokes.push_back(s);
		float by = (float)l.fh - l.bottom_bar_h - 16.0f;
		s.points = {
			{l.canvas_cx, by - 24.0f}, {l.canvas_cx, by},
			{l.canvas_cx - 8.0f, by - 8.0f}, {l.canvas_cx, by},
			{l.canvas_cx + 8.0f, by - 8.0f},
		};
		strokes.push_back(s);
	}

	// Wobble: slight rotation oscillation.
	float wob = std::sin(wobble_phase_ * TWO_PI * 0.5f) * (5.0f * PI / 180.0f);
	float breathing = 1.0f + 0.02f * std::sin(wobble_phase_ * TWO_PI * 0.5f);

	// Creature outline
	{
		ChalkStroke s; s.color = color_; s.half_width = 4.5f;
		float c = std::cos(wob), si = std::sin(wob);
		for (auto& v : local_poly_) {
			glm::vec2 vv(v.x * breathing, v.y * breathing);
			glm::vec2 r(c * vv.x - si * vv.y, si * vv.x + c * vv.y);
			s.points.push_back(local_to_canvas_px_(r, l));
		}
		s.points.push_back(s.points.front());
		strokes.push_back(s);
	}

	// Parts
	{
		float c = std::cos(wob), si = std::sin(wob);
		auto local_to_screen = [&](glm::vec2 v) {
			glm::vec2 vv(v.x * breathing, v.y * breathing);
			glm::vec2 r(c * vv.x - si * vv.y, si * vv.x + c * vv.y);
			return local_to_canvas_px_(r, l);
		};
		appendPartStrokes(parts_, color_, local_to_screen, PX_PER_UNIT,
		                  (float)glfwGetTime(), strokes);
	}

	// Brush cursor
	if (over_canvas) {
		ChalkStroke s; s.color = glm::vec3(0.95f, 0.95f, 0.85f); s.half_width = 1.2f;
		const int N = 24;
		float R = 18.0f;
		for (int i = 0; i <= N; ++i) {
			float a = TWO_PI * (float)i / (float)N;
			s.points.push_back({in.mouse_px.x + std::cos(a) * R,
			                    in.mouse_px.y + std::sin(a) * R});
		}
		strokes.push_back(s);
	}

	// Selected part marker + buttons (visual only — clicks handled above).
	if (selected_part_idx_ >= 0 && selected_part_idx_ < (int)parts_.size()) {
		auto& p = parts_[selected_part_idx_];
		glm::vec2 sp = local_to_canvas_px_(p.anchor_local, l);
		ChalkStroke s; s.color = glm::vec3(1.0f, 0.85f, 0.30f); s.half_width = 2.5f;
		const int N = 20; float R = 28.0f;
		for (int i = 0; i <= N; ++i) {
			float a = TWO_PI * (float)i / (float)N;
			s.points.push_back({sp.x + std::cos(a) * R, sp.y + std::sin(a) * R});
		}
		strokes.push_back(s);
	}

	if (!strokes.empty()) renderer_->drawStrokes(strokes, nullptr, l.fw, l.fh);

	// Speech bubble (drawn as a translucent box above creature center).
	if (speech_t_left_ > 0.0f && !speech_text_.empty()) {
		float k = std::min(1.0f, speech_t_left_ / 1.0f);
		k = std::min(k, std::max(0.0f, (2.5f - speech_t_left_) / 0.4f));
		k = std::clamp(k, 0.0f, 1.0f);
		float bx = l.canvas_cx - 90.0f, by = l.canvas_cy - 140.0f;
		float bw = 180.0f, bh = 56.0f;
		glm::vec2 a = px2ndc(bx, by + bh), b = px2ndc(bx + bw, by);
		text_->drawRect(a.x, a.y, b.x - a.x, b.y - a.y,
			glm::vec4(0.97f, 0.95f, 0.80f, 0.85f * k));
		// Border
		glm::vec4 edge(0.10f, 0.08f, 0.06f, 0.90f * k);
		float t = 0.004f;
		text_->drawRect(a.x, a.y, b.x - a.x, t, edge);
		text_->drawRect(a.x, b.y - t, b.x - a.x, t, edge);
		text_->drawRect(a.x, a.y, t, b.y - a.y, edge);
		text_->drawRect(b.x - t, a.y, t, b.y - a.y, edge);
		float scale = 1.4f;
		float w_chars = (float)speech_text_.size() * 0.018f * scale;
		float tx = a.x + ((b.x - a.x) - w_chars) * 0.5f;
		float ty = a.y + ((b.y - a.y) - 0.04f) * 0.5f;
		text_->drawText(speech_text_, tx, ty, scale,
			glm::vec4(0.10f, 0.08f, 0.06f, k), aspect);
	}
}

void CreatureLab::draw_drawer_(const Layout& l, const LabInput& in) {
	int fw = l.fw, fh = l.fh;
	float aspect = (float)fw / (float)fh;
	auto px2ndc = [&](float x, float y) {
		return glm::vec2(x / fw * 2.0f - 1.0f, 1.0f - y / fh * 2.0f);
	};

	// Drawer panel sits at the bottom of the left rail.
	float dx = 12.0f;
	float dy = l.drawer_y;
	float dw = l.left_w - 24.0f;
	float dh = l.drawer_h;
	if (dh < 60.0f) return;
	glm::vec2 a = px2ndc(dx, dy + dh);
	glm::vec2 b = px2ndc(dx + dw, dy);
	text_->drawRect(a.x, a.y, b.x - a.x, b.y - a.y, ui::CARD_FILL);

	if (drawer_ == Drawer::SHAPE) {
		float ndc_y = 1.0f - (dy + 16.0f) / fh * 2.0f;
		text_->drawText("DRAG THE CREATURE", a.x + 0.01f, ndc_y, 1.0f,
			ui::TEXT_DARK, aspect);
		text_->drawText("TO BEND IT!", a.x + 0.01f, ndc_y - 0.05f, 1.0f,
			ui::TEXT_DARK, aspect);
	} else if (drawer_ == Drawer::PARTS) {
		// Grid 2 columns × 6 rows.
		float pad = 8.0f;
		float cell_w = (dw - pad * 3.0f) * 0.5f;
		float cell_h = std::min(96.0f, (dh - pad * 7.0f) / 6.0f);
		for (int i = 0; i < kPaletteCount; ++i) {
			int col = i % 2, row = i / 2;
			float bx = dx + pad + col * (cell_w + pad);
			float by = dy + pad + row * (cell_h + pad);
			sim::PartType t = kPaletteTypes[i];
			int cur = part_stack_count(parts_, t);
			int cap = part_stack_cap(t);
			bool can = cur + 2 <= cap;
			glm::vec3 fill = (boing_part_idx_ == i)
				? glm::vec3(0.85f, 0.30f, 0.30f)
				: (can ? glm::vec3(0.18f, 0.30f, 0.40f)
				       : glm::vec3(0.20f, 0.20f, 0.22f));
			if (pixel_button_(bx, by, cell_w, cell_h, sim::part_name(t), true, in, fill)) {
				if (fullness_frac_() >= 1.0f) {
					jar_shake_t_ = 0.4f;
					boing_part_idx_ = i; boing_t_left_ = 0.4f;
				} else if (!tap_place_part_(t)) {
					boing_part_idx_ = i; boing_t_left_ = 0.4f;
				}
			}
		}
	} else if (drawer_ == Drawer::COLOR) {
		// 8 circles in 4×2 grid.
		float pad = 10.0f;
		float cell_w = (dw - pad * 5.0f) / 4.0f;
		float cell_h = std::min(64.0f, cell_w);
		for (int i = 0; i < kColorPaletteCount; ++i) {
			int col = i % 4, row = i / 4;
			float bx = dx + pad + col * (cell_w + pad);
			float by = dy + pad + row * (cell_h + pad);
			if (pixel_button_(bx, by, cell_w, cell_h, "", true, in, kColorPalette[i])) {
				push_undo_();
				color_ = kColorPalette[i];
			}
		}
	}
}

void CreatureLab::draw_bottom_bar_(const Layout& l, const LabInput& in, LabOutcome& outc) {
	int fw = l.fw, fh = l.fh;
	auto px2ndc = [&](float x, float y) {
		return glm::vec2(x / fw * 2.0f - 1.0f, 1.0f - y / fh * 2.0f);
	};
	glm::vec2 a = px2ndc(0.0f, (float)fh);
	glm::vec2 b = px2ndc((float)fw, (float)fh - l.bottom_bar_h);
	text_->drawRect(a.x, a.y, b.x - a.x, b.y - a.y, ui::CARD_FILL);
	text_->drawRect(a.x, b.y - 0.004f, b.x - a.x, 0.004f, ui::CARD_STROKE);

	float by = (float)fh - l.bottom_bar_h + 14.0f;
	float bh = l.bottom_bar_h - 28.0f;

	// UNDO
	if (pixel_button_(20.0f, by, 96.0f, bh, "UNDO", !undo_.empty(), in,
	                  glm::vec3(0.25f, 0.30f, 0.35f))) {
		if (!undo_.empty()) {
			auto& e = undo_.back();
			cell_ = e.cell; parts_ = e.parts; color_ = e.color;
			rebuild_polygon_();
			refresh_stats_();
			undo_.pop_back();
		}
	}
	// RESET
	if (pixel_button_(124.0f, by, 96.0f, bh, "RESET", true, in,
	                  glm::vec3(0.45f, 0.30f, 0.30f))) {
		push_undo_();
		cell_.init_circle(40.0f);
		parts_.clear();
		rebuild_polygon_();
		refresh_stats_();
	}
	// LET'S GO!
	float go_w = 256.0f, go_h = bh;
	float go_x = (float)fw * 0.5f - go_w * 0.5f;
	if (pixel_button_(go_x, by, go_w, go_h, "LET'S GO!", true, in,
	                  glm::vec3(0.40f, 0.70f, 0.30f))) {
		outc = LabOutcome::USE;
	}
	// BACK
	if (pixel_button_((float)fw - 130.0f, by, 110.0f, bh, "MENU", true, in,
	                  glm::vec3(0.30f, 0.30f, 0.30f))) {
		outc = LabOutcome::BACK;
	}
}

} // namespace civcraft::cellcraft
