// CellCraft — KID MODE creature lab.
//
// Single screen, BIG controls, no drag-drop. Sculpt is always-on (left
// drag bends the body). Tap a part icon → it auto-anchors to a sensible
// slot. Tap a placed part → 4 floating buttons (BIGGER/SMALLER/SPIN/REMOVE).
// Right rail shows fullness jar + SPEED/TOUGH/BITE bars. Bottom: LET'S GO.
//
// Parallel to the classic LabScreen — they don't share state. KidLab owns
// its own RadialCell + parts vector.

#pragma once

#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "CellCraft/client/chalk_renderer.h"
#include "CellCraft/client/chalk_stroke.h"
#include "CellCraft/client/lab_screen.h"   // reuse LabInput + LabOutcome
#include "CellCraft/sim/part.h"
#include "CellCraft/sim/radial_cell.h"

namespace civcraft { class Window; class TextRenderer; }

namespace civcraft::cellcraft {

class KidLab {
public:
	void init(Window* window, ChalkRenderer* renderer, TextRenderer* text);
	void reset();

	// Load a starter (cell + parts + color + initial name from generator).
	void load_starter(const sim::RadialCell& cell,
	                  const std::vector<sim::Part>& parts,
	                  glm::vec3 color,
	                  const std::string& name);
	void set_speech_enabled(bool e) { speech_enabled_ = e; }

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

	// Coord conversions for canvas (cx,cy at canvas_cx, canvas_cy; px_per_unit constant).
	glm::vec2 local_to_canvas_px_(glm::vec2 local, const Layout& l) const;
	glm::vec2 canvas_px_to_local_(glm::vec2 px, const Layout& l) const;

	// Place a (mirrored) part pair at a sensible anchor for the type.
	// Returns true if placed; false if over budget / over stack cap (boing).
	bool tap_place_part_(sim::PartType t);

	// Hit-test placed parts; returns index in parts_ or -1.
	int placed_part_hit_(glm::vec2 canvas_px, const Layout& l) const;
	void clamp_part_to_canvas_();

	float total_cost_() const;
	float budget_() const;
	float fullness_frac_() const;
	void  refresh_stats_();

	Window*        window_   = nullptr;
	ChalkRenderer* renderer_ = nullptr;
	TextRenderer*  text_     = nullptr;

	sim::RadialCell         cell_;
	std::vector<glm::vec2>  local_poly_;
	std::vector<sim::Part>  parts_;
	glm::vec3               color_ = glm::vec3(0.95f, 0.65f, 0.85f);
	std::string             name_  = "Bloopy Blob";

	// Cached gameplay stats (for SPEED/TOUGH/BITE bars).
	float stat_speed_ = 0.0f, stat_tough_ = 0.0f, stat_bite_ = 0.0f;

	Drawer drawer_ = Drawer::PARTS;
	int    selected_part_idx_ = -1;
	float  selected_buttons_t_ = 0.0f; // for bounce animation

	// Sculpt state.
	bool   sculpting_ = false;
	double sculpt_last_t_ = 0.0;

	// Undo stack (cell + parts + color); reset clears it.
	std::vector<UndoEntry> undo_;

	// Speech bubble.
	bool        speech_enabled_ = true;
	float       speech_t_left_  = 0.0f; // current bubble life
	float       speech_t_next_  = 8.0f; // until next bubble
	std::string speech_text_;

	// Name editing (very simple inline).
	bool name_editing_ = false;

	// Jar shake animation (when overflow rejected).
	float jar_shake_t_ = 0.0f;

	// Boing visual: brief red flash on the offending part icon.
	int   boing_part_idx_ = -1;
	float boing_t_left_   = 0.0f;

	float time_acc_ = 0.0f;

	// Wobble (visual breathing).
	float wobble_phase_ = 0.0f;
};

} // namespace civcraft::cellcraft
