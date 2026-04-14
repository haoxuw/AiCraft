// CellCraft — Creature Lab. One unified canvas with always-on sculpt,
// drag-drop parts from a palette, and a 3D-print-style transform gizmo
// on the selected part (translate / uniform scale / rotate / delete).
// Mirror axis is the vertical (y) axis; every placement auto-mirrors.

#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/client/chalk_renderer.h"
#include "CellCraft/client/chalk_stroke.h"
#include "CellCraft/sim/part.h"
#include "CellCraft/sim/radial_cell.h"

struct GLFWwindow;

namespace civcraft { class Window; class TextRenderer; }

namespace civcraft::cellcraft {

using civcraft::Window;
using civcraft::TextRenderer;

enum class LabOutcome {
	NONE,
	USE,   // USE THIS MONSTER clicked — read cell/parts
	BACK,  // BACK clicked — go back
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
	const std::vector<sim::Part>&  parts()  const { return parts_; }
	glm::vec3                      color()  const { return color_; }

	// Pre-configure the lab with representative state for screenshots.
	void seed_lab_for_screenshot();

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

	// Active operation for the left-mouse-button drag.
	enum class Op {
		NONE,
		SCULPT,           // left-drag over empty canvas pushes/pulls boundary
		DRAG_FROM_PALETTE,// started from palette, ghost at cursor
		GIZMO_TRANSLATE,  // selected part: drag inside bbox
		GIZMO_SCALE,      // selected part: drag corner handle
		GIZMO_ROTATE,     // selected part: drag rotation handle
	};

	Layout compute_layout_() const;

	// Costs.
	float cell_cost_() const;
	float parts_cost_() const;
	float total_cost_() const { return cell_cost_() + parts_cost_(); }
	float budget_() const;

	// Rendering.
	void draw_grid_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_mirror_line_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_cell_(std::vector<ChalkStroke>& out, const Layout& l, float time_s);
	void draw_parts_(std::vector<ChalkStroke>& out, const Layout& l, float time_s);
	void draw_head_tail_markers_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_flashes_(std::vector<ChalkStroke>& out);
	void draw_brush_cursor_(std::vector<ChalkStroke>& out, const Layout& l, const LabInput& in);
	void draw_gizmo_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_ghost_(std::vector<ChalkStroke>& out, const Layout& l,
	                 const LabInput& in, float time_s);

	void draw_palette_(const Layout& l, const LabInput& in, float time_s);
	void draw_loadout_and_stats_(const Layout& l, const LabInput& in);
	void draw_bottom_buttons_(const Layout& l, const LabInput& in, LabOutcome& outc);
	void draw_title_bar_();
	void draw_brush_label_(const Layout& l, const LabInput& in);

	bool pixel_button_(float x, float y, float w, float h,
	                   const char* label, bool enabled,
	                   const LabInput& in, bool selected = false);

	static bool pt_in_rect_(glm::vec2 p, float x, float y, float w, float h) {
		return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
	}

	// Coord conversions.
	glm::vec2 local_to_canvas_px_(glm::vec2 local, const Layout& l) const;
	glm::vec2 canvas_px_to_local_(glm::vec2 px, const Layout& l) const;
	float     mouse_angle_(glm::vec2 mouse_px, const Layout& l) const;

	// Unified interaction dispatcher. Applies click-priority hit testing.
	void handle_input_(const LabInput& in, const Layout& l, float dt);

	// True while an active canvas drag is consuming input (affects scroll meaning).
	bool in_canvas_drag_() const;

	// Hit tests.
	int  palette_hit_(const LabInput& in, const Layout& l) const;
	int  placed_part_hit_(glm::vec2 px, const Layout& l) const;
	// Returns 0..3 for corner index, 4 for rotate, 5 for delete, -1 none.
	int  gizmo_handle_hit_(glm::vec2 px, const Layout& l) const;
	bool gizmo_bbox_hit_(glm::vec2 px, const Layout& l) const;
	void compute_bbox_px_(int part_idx, const Layout& l,
	                      float& x, float& y, float& w, float& h) const;

	// Budget-aware max uniform scale given part index; used to clamp.
	float max_scale_within_budget_(int part_idx) const;

	void rebuild_polygon_();
	bool pt_inside_cell_(glm::vec2 local) const;
	static int part_cap_(sim::PartType t);

	// Mirror: given a part's local anchor, return the mirror anchor.
	static glm::vec2 mirror_anchor_(glm::vec2 a) { return glm::vec2(-a.x, a.y); }
	// Find the index of the mirror partner of parts_[i], or -1 if none.
	int mirror_partner_(int i) const;
	// Place a part pair (or reject). Returns true if placed.
	bool place_part_pair_(sim::PartType pt, glm::vec2 anchor_local,
	                      float orientation, float scale);

	struct Flash { glm::vec2 pos_px; float t_left; glm::vec3 color; };
	void push_flash_(glm::vec2 pos_px, glm::vec3 col = glm::vec3(1.0f, 0.9f, 0.4f));

	// --------------------------------------------------------------------
	Window*        window_    = nullptr;
	ChalkRenderer* renderer_  = nullptr;
	TextRenderer*  text_      = nullptr;

	sim::RadialCell         cell_;
	std::vector<glm::vec2>  local_poly_;

	std::vector<sim::Part>  parts_;
	glm::vec3               color_ = glm::vec3(0.95f, 0.95f, 0.92f);

	// Sculpt brush — sigma in radians (≈angular stddev).
	float brush_sigma_ = 0.22f;

	// Active drag/op state.
	Op    op_ = Op::NONE;
	int   drag_palette_idx_ = -1;    // when DRAG_FROM_PALETTE
	int   selected_idx_ = -1;        // index into parts_ of selected part (right half convention)
	int   gizmo_handle_ = -1;        // when GIZMO_SCALE: 0..3 corner
	// Snapshot of part at op start (for cancellation / scaling reference).
	sim::Part op_start_snapshot_;
	glm::vec2 op_start_mouse_px_ = glm::vec2(0.0f);
	float     op_start_scale_ = 1.0f;

	// Status line (below title).
	std::string status_;
	glm::vec3   status_color_ = glm::vec3(0.85f);

	std::vector<Flash> flashes_;

	float time_acc_ = 0.0f;
};

} // namespace civcraft::cellcraft
