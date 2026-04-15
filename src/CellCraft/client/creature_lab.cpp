// CellCraft — creature lab. See creature_lab.h.

#include "CellCraft/client/creature_lab.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "CellCraft/client/name_generator.h"
#include "CellCraft/client/part_render.h"
#include "CellCraft/client/ui_modern.h"
#include "CellCraft/client/ui_text.h"
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
	{0.239f, 0.847f, 0.776f}, // saturated teal #3DD8C6 (pink reserved for meat food)
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
	color_ = glm::vec3(0.239f, 0.847f, 0.776f); // teal #3DD8C6 (palette[0]; pink reserved for meat food)
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
	current_tier_ = 1;
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
	// Modern layout. Field mapping:
	//   left_x/left_w   → left drawer panel rect (x, width)
	//   right_x/right_w → right stats panel rect
	//   canvas_x/canvas_w → cream sculpt bezel rect (x, width)
	//   canvas_cx/canvas_cy → bezel center (creature origin)
	//   top_bar_h       → bezel top y (reused slot)
	//   bottom_bar_h    → bezel height (reused slot)
	//   drawer_y/drawer_h → drawer content region y,height inside left panel
	Layout l;
	glfwGetFramebufferSize(window_->handle(), &l.fw, &l.fh);

	const float margin = 32.0f;
	const float left_w = 260.0f;
	const float right_w = 260.0f;
	const float bottom_h = 120.0f; // reserve for bottom button row

	l.left_x = margin;
	l.left_w = left_w;
	l.right_w = right_w;
	l.right_x = (float)l.fw - right_w - margin;

	// Bezel rect: centered in the middle column, offset slightly right.
	float mid_x0 = l.left_x + left_w + margin;
	float mid_x1 = l.right_x - margin;
	float mid_w  = mid_x1 - mid_x0;
	float mid_h  = (float)l.fh - margin * 2.0f - bottom_h - margin;
	// Target ~960x820 but adapt if smaller screen.
	float bezel_w = std::min(960.0f, mid_w);
	float bezel_h = std::min(820.0f, mid_h);
	l.canvas_w   = bezel_w;
	l.canvas_x   = mid_x0 + (mid_w - bezel_w) * 0.5f + 16.0f; // nudge right
	if (l.canvas_x + bezel_w > mid_x1) l.canvas_x = mid_x1 - bezel_w;
	l.top_bar_h    = margin + (mid_h - bezel_h) * 0.5f; // bezel y
	l.bottom_bar_h = bezel_h;                            // bezel h
	l.canvas_cx = l.canvas_x + bezel_w * 0.5f;
	l.canvas_cy = l.top_bar_h + bezel_h * 0.5f;

	// Drawer content (inside left panel) starts after header+tier+name+tabs.
	l.drawer_y = margin + 320.0f;
	l.drawer_h = (float)l.fh - bottom_h - margin - l.drawer_y;
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
	stat_diet_  = m.part_effect.diet;
}

bool CreatureLab::tap_place_part_(sim::PartType t) {
	if (!sim::isPartUnlocked(t, current_tier_)) {
		// Tier-gated: this part is not yet available. Shake the jar as a
		// generic "nope" signal; caller handles per-button boing anim.
		jar_shake_t_ = 0.4f;
		return false;
	}
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

	namespace m = ui::modern;
	m::beginFrame(text_, l.fw, l.fh);

	// 1) Outer dark charcoal gradient background.
	m::drawScrim(0, 0, l.fw, l.fh, m::SURFACE_BG_TOP, m::SURFACE_BG_BOTTOM);

	// 2) Canvas bezel — soft shadow + cream rounded rect with subtle stroke.
	{
		int bx = (int)l.canvas_x;
		int by = (int)l.top_bar_h;
		int bw = (int)l.canvas_w;
		int bh = (int)l.bottom_bar_h;
		m::drawSoftShadow(bx, by, bw, bh, m::RADIUS_LG, 24, 0.45f);
		const glm::vec4 CREAM{0.961f, 0.937f, 0.886f, 1.0f}; // #F5EFE2
		m::drawRoundedRect(bx, by, bw, bh, m::RADIUS_LG, CREAM,
		                   m::STROKE_SUBTLE, 1);
		// SCULPT tag just above the bezel (top-left corner).
		m::drawTextLabel(bx + 8, by - m::TYPE_LABEL - 6,
		                 "SCULPT", m::TEXT_SECONDARY);
	}

	// 3) Chalk content inside the bezel (creature polygon + parts + cursor).
	draw_canvas_(l, in, dt);

	// 4) Modern side panels + bottom bar.
	draw_left_rail_(l, in);
	draw_right_rail_(l);
	draw_bottom_bar_(l, in, outc);
	draw_top_bar_(l, in); // top-right close button + floating part action buttons

	if (outc == LabOutcome::NONE && pending_outcome_ != LabOutcome::NONE) {
		outc = pending_outcome_;
		pending_outcome_ = LabOutcome::NONE;
	}
	return outc;
}

void CreatureLab::draw_top_bar_(const Layout& l, const LabInput& in) {
	// In the modern layout, the top bar is gone. We use this hook for the
	// top-right "close" (back to menu) icon button, and for rendering the
	// floating per-part action buttons (+, -, ROT, DEL) when a part is
	// selected — since those float over the cream bezel in modern style.
	namespace m = ui::modern;
	const int close_size = 40;
	int cx = l.fw - close_size - 32;
	int cy = 32;
	bool ch = point_in_rect(in.mouse_px, (float)cx, (float)cy,
	                        (float)close_size, (float)close_size);
	bool cp = ch && in.mouse_left_down;
	m::buttonIcon(cx, cy, close_size, "X", "Menu", ch, cp);
	if (ch && in.mouse_left_click) {
		pending_outcome_ = LabOutcome::BACK;
	}
}

void CreatureLab::draw_left_rail_(const Layout& l, const LabInput& in) {
	namespace m = ui::modern;
	const int margin = 32;
	const int px = (int)l.left_x;
	const int py = margin;
	const int pw = (int)l.left_w;
	const int ph = l.fh - margin * 2;

	// Glass panel for the left drawer.
	m::drawGlassPanel(px, py, pw, ph, m::RADIUS_LG);

	int y = py + m::SPACE_LG;

	// Header.
	{
		int tw = m::measureTextPx("CREATURE LAB", m::TYPE_TITLE_SM);
		m::drawTextModern(px + (pw - tw) / 2, y,
		                  "CREATURE LAB", m::TYPE_TITLE_SM, m::TEXT_PRIMARY);
	}
	y += m::TYPE_TITLE_SM + m::SPACE_MD;

	// Tier badge.
	{
		char tier_buf[32];
		std::snprintf(tier_buf, sizeof(tier_buf), "T%d  %s",
		              current_tier_, sim::tierName(current_tier_));
		int tw = m::measureTextPx(tier_buf, m::TYPE_LABEL) + m::SPACE_MD * 2;
		glm::vec4 accent = (current_tier_ >= 3) ? m::ACCENT_AMBER : m::ACCENT_CYAN;
		m::drawPillBadge(px + (pw - tw) / 2, y, tier_buf,
		                 m::TEXT_PRIMARY, m::SURFACE_PANEL_HI, accent);
		y += m::TYPE_LABEL + m::SPACE_MD * 2 + m::SPACE_LG;
	}

	// Editable name input.
	{
		const int input_h = 44;
		const int input_x = px + m::SPACE_LG;
		const int input_y = y;
		const int input_w = pw - m::SPACE_LG * 2;
		bool hovered = point_in_rect(in.mouse_px, (float)input_x, (float)input_y,
		                             (float)input_w, (float)input_h);
		m::drawRoundedRect(input_x, input_y, input_w, input_h, m::RADIUS_MD,
		                   m::SURFACE_PANEL_HI,
		                   hovered ? m::ACCENT_CYAN : m::STROKE_SUBTLE, 1);
		int tw = m::measureTextPx(name_, m::TYPE_BODY);
		m::drawTextModern(input_x + (input_w - tw) / 2,
		                  input_y + (input_h - m::TYPE_BODY) / 2,
		                  name_, m::TYPE_BODY, m::TEXT_PRIMARY);
		// Click to re-roll name (preserves existing click-to-edit behaviour).
		if (hovered && in.mouse_left_click) {
			std::mt19937 rng((uint32_t)(time_acc_ * 1000.0f + 7));
			name_ = generateName(rng);
		}
		y += input_h + m::SPACE_LG;
	}

	// Divider.
	m::drawDivider(px + m::SPACE_LG, y, pw - m::SPACE_LG * 2,
	               m::DividerAxis::HORIZONTAL);
	y += m::SPACE_LG;

	// Segmented control (SHAPE / PARTS / COLOR).
	{
		const char* labels[3] = { "SHAPE", "PARTS", "COLOR" };
		Drawer drawers[3] = { Drawer::SHAPE, Drawer::PARTS, Drawer::COLOR };
		const int seg_h = 40;
		const int cells = 3;
		const int seg_w = pw - m::SPACE_LG * 2;
		const int cell_w = seg_w / cells;
		for (int i = 0; i < cells; ++i) {
			int bx = px + m::SPACE_LG + i * cell_w;
			int bw = cell_w;
			bool active = (drawer_ == drawers[i]);
			bool hov = point_in_rect(in.mouse_px, (float)bx, (float)y,
			                         (float)bw, (float)seg_h);
			bool pr = hov && in.mouse_left_down;
			if (active) {
				m::drawRoundedRect(bx, y, bw, seg_h, m::RADIUS_MD,
				                   m::ACCENT_CYAN, glm::vec4(0.0f), 0);
				int tw = m::measureTextPx(labels[i], m::TYPE_LABEL);
				m::drawTextModern(bx + (bw - tw) / 2,
				                  y + (seg_h - m::TYPE_LABEL) / 2,
				                  labels[i], m::TYPE_LABEL, m::TEXT_ON_ACCENT);
			} else {
				m::drawRoundedRect(bx, y, bw, seg_h, m::RADIUS_MD,
				                   hov ? glm::vec4(m::ACCENT_CYAN.r, m::ACCENT_CYAN.g,
				                                   m::ACCENT_CYAN.b, 0.08f)
				                       : glm::vec4(0.0f),
				                   m::ACCENT_CYAN, 1);
				int tw = m::measureTextPx(labels[i], m::TYPE_LABEL);
				m::drawTextModern(bx + (bw - tw) / 2,
				                  y + (seg_h - m::TYPE_LABEL) / 2,
				                  labels[i], m::TYPE_LABEL, m::ACCENT_CYAN);
			}
			if (hov && in.mouse_left_click) drawer_ = drawers[i];
			(void)pr;
		}
		y += seg_h + m::SPACE_LG;
	}

	// Drawer content region.
	l.drawer_y;
	{
		// Use the space between `y` and the bottom buttons area.
		Layout& lref = const_cast<Layout&>(l);
		lref.drawer_y = (float)y;
		lref.drawer_h = (float)(py + ph - y - (44 + m::SPACE_LG * 2));
	}
	draw_drawer_(l, in);

	// Bottom of drawer: UNDO + RESET ghost buttons side-by-side.
	{
		const int bh = 44;
		const int by = py + ph - bh - m::SPACE_LG;
		const int gap = m::SPACE_SM;
		const int bw = (pw - m::SPACE_LG * 2 - gap) / 2;
		const int ux = px + m::SPACE_LG;
		const int rx = ux + bw + gap;
		bool uh = !undo_.empty() && point_in_rect(in.mouse_px,
		                                          (float)ux, (float)by,
		                                          (float)bw, (float)bh);
		bool up = uh && in.mouse_left_down;
		if (m::buttonGhost(ux, by, bw, bh, "UNDO", uh, up) ||
		    (uh && in.mouse_left_click)) {
			if (!undo_.empty()) {
				auto& e = undo_.back();
				cell_ = e.cell; parts_ = e.parts; color_ = e.color;
				rebuild_polygon_();
				refresh_stats_();
				undo_.pop_back();
			}
		}
		bool rh = point_in_rect(in.mouse_px, (float)rx, (float)by,
		                        (float)bw, (float)bh);
		bool rp = rh && in.mouse_left_down;
		if (m::buttonGhost(rx, by, bw, bh, "RESET", rh, rp) ||
		    (rh && in.mouse_left_click)) {
			push_undo_();
			cell_.init_circle(40.0f);
			parts_.clear();
			rebuild_polygon_();
			refresh_stats_();
		}
	}
}

void CreatureLab::draw_right_rail_(const Layout& l) {
	namespace m = ui::modern;
	const int margin = 32;
	const int px = (int)l.right_x;
	const int py = margin;
	const int pw = (int)l.right_w;
	const int ph = l.fh - margin * 2;
	m::drawGlassPanel(px, py, pw, ph, m::RADIUS_LG);

	int y = py + m::SPACE_LG;

	// Header.
	{
		const char* hdr = "CREATURE STATS";
		int tw = m::measureTextPx(hdr, m::TYPE_LABEL);
		m::drawTextLabel(px + (pw - tw) / 2, y, hdr, m::TEXT_SECONDARY);
		y += m::TYPE_LABEL + m::SPACE_LG;
	}

	// FULLNESS ring.
	{
		int radius = 60;
		int cx = px + pw / 2;
		int cy = y + radius;
		float frac = fullness_frac_();
		glm::vec4 ring = (frac > 0.85f) ? m::ACCENT_DANGER
		               : (frac > 0.60f) ? m::ACCENT_AMBER
		               :                  m::ACCENT_CYAN;
		m::drawRingProgress(cx, cy, radius, 10, frac, ring);
		// Center percent label.
		char pct[16]; std::snprintf(pct, sizeof(pct), "%d%%", (int)std::round(frac * 100.0f));
		int tw = m::measureTextPx(pct, m::TYPE_TITLE_SM);
		m::drawTextModern(cx - tw / 2, cy - m::TYPE_TITLE_SM / 2,
		                  pct, m::TYPE_TITLE_SM, m::TEXT_PRIMARY);
		int lw = m::measureTextPx("FULLNESS", m::TYPE_CAPTION);
		m::drawTextModern(cx - lw / 2, cy + m::TYPE_TITLE_SM / 2 + 4,
		                  "FULLNESS", m::TYPE_CAPTION, m::TEXT_SECONDARY);
		y = cy + radius + m::SPACE_MD;
	}

	// Diet badge.
	{
		const char* diet_txt = "OMNIVORE";
		glm::vec4 accent = m::ACCENT_CYAN;
		switch (stat_diet_) {
		case sim::Diet::CARNIVORE: diet_txt = "CARNIVORE"; accent = m::ACCENT_DANGER; break;
		case sim::Diet::HERBIVORE: diet_txt = "HERBIVORE"; accent = m::ACCENT_SUCCESS; break;
		case sim::Diet::OMNIVORE:  diet_txt = "OMNIVORE";  accent = m::ACCENT_AMBER;  break;
		}
		int tw = m::measureTextPx(diet_txt, m::TYPE_LABEL) + m::SPACE_MD * 2;
		m::drawPillBadge(px + (pw - tw) / 2, y, diet_txt,
		                 m::TEXT_PRIMARY, m::SURFACE_PANEL_HI, accent);
		y += m::TYPE_LABEL + m::SPACE_MD * 2 + m::SPACE_LG;
	}

	// Divider.
	m::drawDivider(px + m::SPACE_LG, y, pw - m::SPACE_LG * 2,
	               m::DividerAxis::HORIZONTAL);
	y += m::SPACE_LG;

	// 4 stat bars: SPEED, TOUGH, BITE, REACH.
	{
		const int bx = px + m::SPACE_LG;
		const int bw = pw - m::SPACE_LG * 2;
		// Build a probe monster (like refresh_stats_) to read raw numbers.
		sim::Monster mm;
		mm.shape = local_poly_;
		mm.parts = parts_;
		mm.biomass = 30.0f;
		mm.refresh_stats();
		int spike_count = 0;
		for (auto& p : parts_) if (p.type == sim::PartType::SPIKE || p.type == sim::PartType::HORN) ++spike_count;
		float reach = mm.part_effect.pickup_radius_mult;
		char buf[16];
		std::snprintf(buf, sizeof(buf), "%d", (int)std::round(mm.move_speed));
		m::drawStatBar(bx, y, bw, "SPEED", stat_speed_, buf, m::ACCENT_CYAN);
		y += 32;
		std::snprintf(buf, sizeof(buf), "%d", (int)std::round(mm.hp_max));
		m::drawStatBar(bx, y, bw, "TOUGH", stat_tough_, buf, m::ACCENT_AMBER);
		y += 32;
		std::snprintf(buf, sizeof(buf), "%.1f", mm.part_effect.damage_mult * (1.0f + spike_count * 0.1f));
		m::drawStatBar(bx, y, bw, "BITE", stat_bite_, buf, m::ACCENT_DANGER);
		y += 32;
		std::snprintf(buf, sizeof(buf), "%.1f", reach);
		float reach_norm = std::clamp((reach - 0.8f) / 1.2f, 0.0f, 1.0f);
		m::drawStatBar(bx, y, bw, "REACH", reach_norm, buf, m::ACCENT_SUCCESS);
		y += 32 + m::SPACE_LG;
	}

	// Divider.
	m::drawDivider(px + m::SPACE_LG, y, pw - m::SPACE_LG * 2,
	               m::DividerAxis::HORIZONTAL);
	y += m::SPACE_MD;

	// Metadata.
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.1f / %.0f",
		              total_cost_(), budget_());
		m::drawTextLabel(px + m::SPACE_LG, y, "BIOMASS COST", m::TEXT_SECONDARY);
		int tw = m::measureTextPx(buf, m::TYPE_CAPTION);
		m::drawTextModern(px + pw - m::SPACE_LG - tw, y, buf,
		                  m::TYPE_CAPTION, m::TEXT_PRIMARY);
		y += m::TYPE_LABEL + m::SPACE_SM;
		float body_r = 0.0f;
		for (int i = 0; i < sim::RadialCell::N; ++i) body_r += cell_.r[i];
		body_r /= sim::RadialCell::N;
		std::snprintf(buf, sizeof(buf), "%.1f", body_r);
		m::drawTextLabel(px + m::SPACE_LG, y, "BODY RADIUS", m::TEXT_SECONDARY);
		int tw2 = m::measureTextPx(buf, m::TYPE_CAPTION);
		m::drawTextModern(px + pw - m::SPACE_LG - tw2, y, buf,
		                  m::TYPE_CAPTION, m::TEXT_PRIMARY);
	}
}

#if 0 // ---- Legacy right-rail renderer (dead code kept for reference) ----
	glm::vec2 a(0.0f);
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

	// Diet badge — small glyph to the right of the fullness jar.
	// CARNIVORE = pink meat blub, HERBIVORE = green leaf cluster,
	// OMNIVORE = both at half size side-by-side. ~18px glyph total.
	{
		const glm::vec4 MEAT_PINK   {1.000f, 0.361f, 0.576f, 1.0f};
		const glm::vec4 MEAT_MARBLE {0.784f, 0.227f, 0.431f, 1.0f};
		const glm::vec4 PLANT_GREEN {0.420f, 0.796f, 0.310f, 1.0f};
		const glm::vec4 PLANT_STEM  {0.659f, 0.910f, 0.478f, 1.0f};

		auto draw_meat = [&](float cx, float cy, float r) {
			// Lobed pink blub approximated as stacked rects (pixel chalk feel).
			glm::vec2 a = px2ndc(cx - r,        cy + r);
			glm::vec2 b = px2ndc(cx + r,        cy - r);
			text_->drawRect(a.x, a.y, b.x - a.x, b.y - a.y, MEAT_PINK);
			// Dark marble dot in the middle.
			glm::vec2 ma = px2ndc(cx - r * 0.35f, cy + r * 0.35f);
			glm::vec2 mb = px2ndc(cx + r * 0.35f, cy - r * 0.35f);
			text_->drawRect(ma.x, ma.y, mb.x - ma.x, mb.y - ma.y, MEAT_MARBLE);
		};
		auto draw_leaf = [&](float cx, float cy, float r) {
			// Elongated green leaf (taller than wide) + light midrib.
			glm::vec2 a = px2ndc(cx - r * 0.70f, cy + r);
			glm::vec2 b = px2ndc(cx + r * 0.70f, cy - r);
			text_->drawRect(a.x, a.y, b.x - a.x, b.y - a.y, PLANT_GREEN);
			glm::vec2 ra = px2ndc(cx - r * 0.10f, cy + r * 0.85f);
			glm::vec2 rb = px2ndc(cx + r * 0.10f, cy - r * 0.85f);
			text_->drawRect(ra.x, ra.y, rb.x - ra.x, rb.y - ra.y, PLANT_STEM);
		};

		// Anchor to right of FULLNESS label, roughly centered vertically on it.
		float badge_cy = jar_y + jar_h + 20.0f;
		float badge_cx = jar_x + jar_w - 20.0f;
		const float R = 9.0f; // ~18px glyph
		switch (stat_diet_) {
		case sim::Diet::CARNIVORE:
			draw_meat(badge_cx, badge_cy, R);
			break;
		case sim::Diet::HERBIVORE:
			draw_leaf(badge_cx, badge_cy, R);
			break;
		case sim::Diet::OMNIVORE:
			draw_meat(badge_cx - R * 0.55f, badge_cy, R * 0.55f);
			draw_leaf(badge_cx + R * 0.55f, badge_cy, R * 0.55f);
			break;
		}
	}

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
#endif

void CreatureLab::draw_canvas_(const Layout& l, const LabInput& in, float dt) {
	(void)dt;
	int fw = l.fw, fh = l.fh;
	auto px2ndc = [&](float x, float y) {
		return glm::vec2(x / fw * 2.0f - 1.0f, 1.0f - y / fh * 2.0f);
	};
	float aspect = (float)fw / (float)fh;

	// Click handling — has priority over sculpt. Canvas now = cream bezel
	// rect (top_bar_h/bottom_bar_h fields are reused as bezel y/h).
	bool over_canvas = in.mouse_px.x >= l.canvas_x
	                && in.mouse_px.x <= l.canvas_x + l.canvas_w
	                && in.mouse_px.y >= l.top_bar_h
	                && in.mouse_px.y <= l.top_bar_h + l.bottom_bar_h;

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

	// ---- Render creature inside the cream bezel (drawn by update()).
	std::vector<ChalkStroke> strokes;

	// Mirror axis dashed
	{
		ChalkStroke s; s.color = glm::vec3(0.55f, 0.55f, 0.50f); s.half_width = 1.2f;
		float top_y = l.top_bar_h + 20.0f;
		float bot_y = l.top_bar_h + l.bottom_bar_h - 20.0f;
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
		float by = l.top_bar_h + l.bottom_bar_h - 16.0f;
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
	namespace m = ui::modern;
	const int dx = (int)l.left_x + m::SPACE_LG;
	const int dy = (int)l.drawer_y;
	const int dw = (int)l.left_w - m::SPACE_LG * 2;
	const int dh = (int)l.drawer_h;
	if (dh < 60) return;

	if (drawer_ == Drawer::SHAPE) {
		m::drawTextLabel(dx, dy, "BRUSH", m::TEXT_SECONDARY);
		int ly = dy + m::TYPE_LABEL + m::SPACE_SM;
		// Placeholder brush size slider (static — brush size not wired to state).
		m::drawRoundedRect(dx, ly + 10, dw, 6, 3, m::TRACK_BG);
		m::drawRoundedRect(dx, ly + 10, dw / 2, 6, 3, m::ACCENT_CYAN);
		m::drawTextModern(dx, ly + 28,
		                  "Drag on the canvas to bend the body.",
		                  m::TYPE_CAPTION, m::TEXT_SECONDARY);
	} else if (drawer_ == Drawer::PARTS) {
		// 3 columns × ceil(N/3) rows, 72×72 tiles.
		const int cols = 3;
		const int pad = m::SPACE_SM;
		int tile_w = (dw - pad * (cols - 1)) / cols;
		int tile_h = tile_w;
		if (tile_h > 78) tile_h = 78;
		for (int i = 0; i < kPaletteCount; ++i) {
			int col = i % cols, row = i / cols;
			int bx = dx + col * (tile_w + pad);
			int by = dy + row * (tile_h + pad + 14); // extra for caption
			if (by + tile_h > dy + dh) break;
			sim::PartType t = kPaletteTypes[i];
			int cur = part_stack_count(parts_, t);
			int cap = part_stack_cap(t);
			bool can = cur + 2 <= cap;
			bool unlocked = sim::isPartUnlocked(t, current_tier_);
			bool hov = unlocked && point_in_rect(in.mouse_px, (float)bx, (float)by,
			                                    (float)tile_w, (float)tile_h);
			m::drawGlassPanel(bx, by, tile_w, tile_h, m::RADIUS_MD);
			if (hov) {
				m::drawInnerGlow(bx, by, tile_w, tile_h, m::RADIUS_MD,
				                 m::ACCENT_CYAN_GLOW, 6);
			}
			if (!unlocked) {
				// Desaturated veil + lock glyph.
				m::drawRoundedRect(bx, by, tile_w, tile_h, m::RADIUS_MD,
				                   glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));
				int gs = m::TYPE_TITLE_SM;
				int tw = m::measureTextPx("?", gs);
				m::drawTextModern(bx + (tile_w - tw) / 2,
				                  by + (tile_h - gs) / 2,
				                  "?", gs, m::TEXT_MUTED);
			} else {
				// Part glyph — first char of part name, big.
				const char* nm = sim::part_name(t);
				std::string glyph(1, nm[0]);
				int gs = m::TYPE_TITLE_MD;
				int tw = m::measureTextPx(glyph, gs);
				glm::vec4 gc = can ? m::ACCENT_CYAN : m::TEXT_MUTED;
				if (boing_part_idx_ == i) gc = m::ACCENT_DANGER;
				m::drawTextModern(bx + (tile_w - tw) / 2,
				                  by + (tile_h - gs) / 2,
				                  glyph, gs, gc);
			}
			// Caption: part name.
			const char* pname = sim::part_name(t);
			int cw = m::measureTextPx(pname, m::TYPE_CAPTION);
			if (cw > tile_w) cw = tile_w;
			m::drawTextModern(bx + (tile_w - cw) / 2, by + tile_h + 2,
			                  pname, m::TYPE_CAPTION,
			                  unlocked ? m::TEXT_SECONDARY : m::TEXT_MUTED);
			if (hov && in.mouse_left_click) {
				if (fullness_frac_() >= 1.0f) {
					jar_shake_t_ = 0.4f;
					boing_part_idx_ = i; boing_t_left_ = 0.4f;
				} else if (!tap_place_part_(t)) {
					boing_part_idx_ = i; boing_t_left_ = 0.4f;
				}
			}
		}
	} else if (drawer_ == Drawer::COLOR) {
		// 4×2 grid of 40×40 rounded swatches.
		const int cols = 4;
		const int pad = m::SPACE_MD;
		int sw = std::min(40, (dw - pad * (cols - 1)) / cols);
		for (int i = 0; i < kColorPaletteCount; ++i) {
			int col = i % cols, row = i / cols;
			int bx = dx + col * (sw + pad);
			int by = dy + row * (sw + pad);
			glm::vec4 fill(kColorPalette[i], 1.0f);
			bool selected = glm::distance(color_, kColorPalette[i]) < 0.01f;
			bool hov = point_in_rect(in.mouse_px, (float)bx, (float)by,
			                         (float)sw, (float)sw);
			m::drawRoundedRect(bx, by, sw, sw, m::RADIUS_MD, fill,
			                   selected ? m::ACCENT_CYAN : m::STROKE_SUBTLE,
			                   selected ? 2 : 1);
			if (selected) {
				// Check mark (fallback).
				int tw = m::measureTextPx("v", m::TYPE_LABEL);
				m::drawTextModern(bx + (sw - tw) / 2,
				                  by + (sw - m::TYPE_LABEL) / 2,
				                  "v", m::TYPE_LABEL, m::TEXT_ON_ACCENT);
			}
			if (hov && !selected) {
				m::drawInnerGlow(bx, by, sw, sw, m::RADIUS_MD,
				                 m::ACCENT_CYAN_GLOW, 4);
			}
			if (hov && in.mouse_left_click) {
				push_undo_();
				color_ = kColorPalette[i];
			}
		}
	}
}

void CreatureLab::draw_bottom_bar_(const Layout& l, const LabInput& in, LabOutcome& outc) {
	namespace m = ui::modern;

	// LET'S GO button: centered horizontally under the bezel.
	const int go_w = 240, go_h = 64;
	int go_x = (int)(l.canvas_x + (l.canvas_w - go_w) / 2.0f);
	int go_y = (int)(l.top_bar_h + l.bottom_bar_h) + 32;
	if (go_y + go_h + 8 > l.fh) go_y = l.fh - go_h - 16;
	bool gh = point_in_rect(in.mouse_px, (float)go_x, (float)go_y,
	                        (float)go_w, (float)go_h);
	bool gp = gh && in.mouse_left_down;
	m::buttonPrimary(go_x, go_y, go_w, go_h, "LET'S GO! >", gh, gp);
	if (gh && in.mouse_left_click) outc = LabOutcome::USE;

	// MENU ghost button: right of LET'S GO.
	const int menu_w = 120, menu_h = 48;
	int menu_x = go_x + go_w + m::SPACE_LG;
	int menu_y = go_y + (go_h - menu_h) / 2;
	if (menu_x + menu_w > l.fw - 32) {
		menu_x = (int)(l.canvas_x + l.canvas_w) - menu_w;
		menu_y = go_y - menu_h - 8;
	}
	bool mh = point_in_rect(in.mouse_px, (float)menu_x, (float)menu_y,
	                        (float)menu_w, (float)menu_h);
	bool mp = mh && in.mouse_left_down;
	m::buttonGhost(menu_x, menu_y, menu_w, menu_h, "MENU", mh, mp);
	if (mh && in.mouse_left_click) outc = LabOutcome::BACK;
}

} // namespace civcraft::cellcraft
