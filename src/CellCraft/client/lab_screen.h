// CellCraft — Creature Lab. Playdough-sculpt cell body, painted armor
// plates, and drag-and-drop mods. One screen; three modes toggled at the
// top (SCULPT / PLATE / MODS). The owning App calls update() each frame
// and, when the user confirms, reads cell/plates/parts via accessors.

#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/client/chalk_renderer.h"
#include "CellCraft/client/chalk_stroke.h"
#include "CellCraft/sim/part.h"
#include "CellCraft/sim/plate.h"
#include "CellCraft/sim/radial_cell.h"

struct GLFWwindow;

namespace civcraft { class Window; class TextRenderer; }

namespace civcraft::cellcraft {

using civcraft::Window;
using civcraft::TextRenderer;

enum class LabOutcome {
	NONE,
	USE,   // USE THIS MONSTER clicked — read cell/plates/parts
	BACK,  // MENU clicked — go back
};

// Per-frame input snapshot built by App.
struct LabInput {
	glm::vec2 mouse_px       = glm::vec2(-1.0f);
	bool      mouse_left_down  = false;
	bool      mouse_right_down = false;
	bool      mouse_left_click  = false;
	bool      mouse_right_click = false;
	float     scroll_y = 0.0f;
	std::vector<int> keys_pressed;
};

class LabScreen {
public:
	void init(Window* window, ChalkRenderer* renderer, TextRenderer* text);
	void reset();

	LabOutcome update(float dt, const LabInput& in);

	// Finalized outputs — read after LabOutcome::USE.
	const sim::RadialCell&         cell()   const { return cell_; }
	const std::vector<sim::Plate>& plates() const { return plates_; }
	const std::vector<sim::Part>&  parts()  const { return parts_; }
	glm::vec3                      color()  const { return color_; }

	// Screenshot seeders — pre-configure a mode with representative state.
	void seed_sculpt_for_screenshot();
	void seed_plate_for_screenshot();
	void seed_mods_for_screenshot();

private:
	struct Layout {
		int   fw = 0, fh = 0;
		float left_x = 0.0f, left_w = 0.0f;
		float right_x = 0.0f, right_w = 0.0f;
		float canvas_x = 0.0f, canvas_w = 0.0f;
		float canvas_cx = 0.0f, canvas_cy = 0.0f;
		float top_bar_h = 0.0f;
		float bottom_bar_h = 0.0f;
	};

	enum class Mode { SCULPT, PLATE, MODS };

	Layout compute_layout_() const;

	// Costs.
	float cell_cost_() const;
	float plates_cost_() const;
	float parts_cost_() const;
	float total_cost_() const { return cell_cost_() + plates_cost_() + parts_cost_(); }
	float budget_() const;

	// Rendering.
	void draw_grid_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_mirror_line_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_cell_(std::vector<ChalkStroke>& out, const Layout& l, float time_s);
	void draw_plates_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_parts_(std::vector<ChalkStroke>& out, const Layout& l, float time_s);
	void draw_head_tail_markers_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_flashes_(std::vector<ChalkStroke>& out);

	void draw_top_tabs_(const Layout& l, const LabInput& in);
	void draw_palette_(const Layout& l, const LabInput& in, float time_s);
	void draw_loadout_and_stats_(const Layout& l, const LabInput& in);
	void draw_bottom_buttons_(const Layout& l, const LabInput& in, LabOutcome& outc);
	void draw_title_bar_();

	bool pixel_button_(float x, float y, float w, float h,
	                   const char* label, bool enabled,
	                   const LabInput& in, bool selected = false);

	static bool pt_in_rect_(glm::vec2 p, float x, float y, float w, float h) {
		return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
	}

	// Coord conversions.
	// Local space: core at origin, y-up (head = +y, tail = -y).
	// Canvas px: screen pixel. canvas_cx → local (0,0).
	glm::vec2 local_to_canvas_px_(glm::vec2 local, const Layout& l) const;
	glm::vec2 canvas_px_to_local_(glm::vec2 px, const Layout& l) const;
	// Angle (radians, RadialCell convention: θ=0 → +x) from a mouse px.
	float     mouse_angle_(glm::vec2 mouse_px, const Layout& l) const;

	// Interaction.
	void handle_sculpt_(const LabInput& in, const Layout& l, float dt);
	void handle_plate_(const LabInput& in, const Layout& l);
	void handle_mods_(const LabInput& in, const Layout& l);
	void rebuild_polygon_();

	bool pt_inside_cell_(glm::vec2 local) const;
	// Stack cap for a given part type (design number lives in part_stats.h).
	static int part_cap_(sim::PartType t);

	// Flash marker for tactile feedback (placements, rejections).
	struct Flash { glm::vec2 pos_px; float t_left; glm::vec3 color; };
	void push_flash_(glm::vec2 pos_px, glm::vec3 col = glm::vec3(1.0f, 0.9f, 0.4f));

	// Data ----------------------------------------------------------------
	Window*        window_    = nullptr;
	ChalkRenderer* renderer_  = nullptr;
	TextRenderer*  text_      = nullptr;

	Mode mode_ = Mode::SCULPT;

	sim::RadialCell         cell_;
	std::vector<glm::vec2>  local_poly_;   // cached polygon from cell (for rendering/hit-tests)

	std::vector<sim::Plate> plates_;
	std::vector<sim::Part>  parts_;
	glm::vec3               color_ = glm::vec3(0.95f, 0.95f, 0.92f);

	// SCULPT.
	float brush_sigma_ = 0.18f;
	bool  prev_mouse_left_down_sc_ = false;
	glm::vec2 prev_mouse_px_sc_ = glm::vec2(-1.0f);

	// PLATE painting.
	bool  plate_painting_ = false;
	float plate_theta_min_ = 0.0f;
	float plate_theta_max_ = 0.0f;

	// MODS drag-and-drop.
	int   drag_palette_idx_ = -1;    // palette slot being dragged
	float ghost_rotation_   = 0.0f;  // 30° snap via scroll
	bool  prev_mouse_left_down_md_ = false;

	// Status line (below title).
	std::string status_;
	glm::vec3   status_color_ = glm::vec3(0.85f);

	// Flash markers.
	std::vector<Flash> flashes_;

	float time_acc_ = 0.0f;
	bool  prev_right_ = false;
};

} // namespace civcraft::cellcraft
