// CellCraft — creature lab.
//
// Single screen, BIG controls, no drag-drop. Sculpt is always-on (left
// drag bends the body). Tap a part icon → it auto-anchors to a sensible
// slot. Tap a placed part → 4 floating buttons (BIGGER/SMALLER/SPIN/REMOVE).
// Right rail shows fullness jar + SPEED/TOUGH/BITE bars. Bottom: LET'S GO.

#pragma once

#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/client/cell_fill_renderer.h"
#include "CellCraft/client/chalk_renderer.h"
#include "CellCraft/client/chalk_stroke.h"
#include "CellCraft/sim/part.h"
#include "CellCraft/sim/radial_cell.h"

namespace civcraft { class Window; class TextRenderer; }

namespace civcraft::cellcraft {

using civcraft::Window;
using civcraft::TextRenderer;

enum class LabOutcome {
	NONE,
	USE,   // LET'S GO clicked — read cell/parts
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

class CreatureLab {
public:
	void init(Window* window, ChalkRenderer* renderer,
	          CellFillRenderer* fill_renderer, TextRenderer* text);
	void reset();

	// Load a starter (cell + parts + color + initial name from generator).
	void load_starter(const sim::RadialCell& cell,
	                  const std::vector<sim::Part>& parts,
	                  glm::vec3 color,
	                  const std::string& name);
	void set_speech_enabled(bool e) { speech_enabled_ = e; }

	// Tier gates which parts can be placed. Defaults to 1 (only starter
	// parts unlocked). Call after load_starter() if the player has
	// already progressed.
	void set_tier(int tier) { current_tier_ = tier; }
	int  tier() const { return current_tier_; }

	LabOutcome update(float dt, const LabInput& in);

	const sim::RadialCell&         cell()  const { return cell_; }
	const std::vector<sim::Part>&  parts() const { return parts_; }
	glm::vec3                      color() const { return color_; }
	const std::string&             name()  const { return name_; }

private:
	enum class Drawer { NONE, SHAPE, PARTS, COLOR };

	struct UndoEntry {
		sim::RadialCell        cell;
		std::vector<sim::Part> parts;
		glm::vec3              color;
	};

	struct Layout {
		int   fw = 0, fh = 0;
		float left_x = 0.0f, left_w = 0.0f;
		float right_x = 0.0f, right_w = 0.0f;
		float canvas_x = 0.0f, canvas_w = 0.0f;
		float canvas_cx = 0.0f, canvas_cy = 0.0f;
		float top_bar_h = 0.0f;
		float bottom_bar_h = 0.0f;
		float drawer_y = 0.0f, drawer_h = 0.0f;
	};
	Layout compute_layout_() const;

	void rebuild_polygon_();
	void push_undo_();

	void draw_top_bar_(const Layout& l, const LabInput& in);
	void draw_left_rail_(const Layout& l, const LabInput& in);
	void draw_right_rail_(const Layout& l);
	void draw_bottom_bar_(const Layout& l, const LabInput& in, LabOutcome& outc);
	void draw_canvas_(const Layout& l, const LabInput& in, float dt);
	void draw_drawer_(const Layout& l, const LabInput& in);

	bool pixel_button_(float x, float y, float w, float h,
	                   const char* label, bool enabled,
	                   const LabInput& in, glm::vec3 fill);

	glm::vec2 local_to_canvas_px_(glm::vec2 local, const Layout& l) const;
	glm::vec2 canvas_px_to_local_(glm::vec2 px, const Layout& l) const;

	bool tap_place_part_(sim::PartType t);

	int placed_part_hit_(glm::vec2 canvas_px, const Layout& l) const;
	void clamp_part_to_canvas_();

	float total_cost_() const;
	float budget_() const;
	float fullness_frac_() const;
	void  refresh_stats_();

	Window*        window_   = nullptr;
	ChalkRenderer* renderer_ = nullptr;
	CellFillRenderer* fill_renderer_ = nullptr;
	TextRenderer*  text_     = nullptr;

	sim::RadialCell         cell_;
	std::vector<glm::vec2>  local_poly_;
	std::vector<sim::Part>  parts_;
	glm::vec3               color_ = glm::vec3(0.239f, 0.847f, 0.776f); // teal #3DD8C6
	std::string             name_  = "Bloopy Blob";

	float stat_speed_ = 0.0f, stat_tough_ = 0.0f, stat_bite_ = 0.0f;
	sim::Diet stat_diet_ = sim::Diet::OMNIVORE;

	Drawer drawer_ = Drawer::PARTS;
	int    selected_part_idx_ = -1;
	float  selected_buttons_t_ = 0.0f;

	bool   sculpting_ = false;
	double sculpt_last_t_ = 0.0;

	std::vector<UndoEntry> undo_;

	bool        speech_enabled_ = true;
	float       speech_t_left_  = 0.0f;
	float       speech_t_next_  = 8.0f;
	std::string speech_text_;

	bool name_editing_ = false;

	float jar_shake_t_ = 0.0f;

	int   boing_part_idx_ = -1;
	float boing_t_left_   = 0.0f;

	float time_acc_ = 0.0f;

	float wobble_phase_ = 0.0f;

	// Current growth tier (1..5). Gates which palette parts are placeable.
	int current_tier_ = 1;

	// Pending outcome raised by modern-panel side hits (e.g. top-right X).
	LabOutcome pending_outcome_ = LabOutcome::NONE;
};

} // namespace civcraft::cellcraft
