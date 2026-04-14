// LifeCraft — Spore-style creature lab screen. Self-contained state machine
// that handles DRAWING and ASSEMBLING modes in one screen. The owning App
// dispatches update()+draw() each frame while state_ == DRAW_LAB and reads
// out the finished monster via take_*(). See app.cpp for hookup.

#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "LifeCraft/client/chalk_renderer.h"
#include "LifeCraft/client/chalk_stroke.h"
#include "LifeCraft/sim/part.h"

struct GLFWwindow;

namespace civcraft { class Window; class TextRenderer; }

namespace civcraft::lifecraft {

using civcraft::Window;
using civcraft::TextRenderer;

enum class LabOutcome {
	NONE,       // still running
	USE,        // user hit USE THIS MONSTER — caller should take_*() and start match
	BACK,       // user hit MENU — caller should goToMainMenu or goToMonsterSelect
};

// Frame input snapshot. App builds this per-frame.
struct LabInput {
	glm::vec2 mouse_px      = glm::vec2(-1.0f);
	bool      mouse_left_down  = false;
	bool      mouse_right_down = false;
	bool      mouse_left_click  = false;  // edge (press this frame)
	bool      mouse_right_click = false;  // edge (press this frame)
	std::vector<int> keys_pressed;        // GLFW_KEY_* pressed this frame
};

class LabScreen {
public:
	void init(Window* window, ChalkRenderer* renderer, TextRenderer* text);
	void reset();

	// Tick + render; returns what the app should do next.
	LabOutcome update(float dt, const LabInput& in);

	// After USE: read final polygon + parts.
	const std::vector<glm::vec2>& local_shape() const { return validated_local_; }
	const std::vector<sim::Part>& parts()       const { return parts_; }
	glm::vec3                     color()       const { return color_; }

private:
	// Layout in pixel space — recomputed each frame from framebuffer size.
	struct Layout {
		int   fw = 0, fh = 0;
		float left_x = 0.0f, left_w = 0.0f;        // left rail
		float right_x = 0.0f, right_w = 0.0f;      // right rail
		float canvas_x = 0.0f, canvas_w = 0.0f;    // center canvas
		float canvas_cx = 0.0f, canvas_cy = 0.0f;  // canvas center pixel
		float top_bar_h = 0.0f;
		float bottom_bar_h = 0.0f;
	};

	enum class Mode { DRAWING, ASSEMBLING };

	Layout compute_layout_() const;
	glm::vec2 px_to_ndc_(glm::vec2 px) const;

	// Rendering helpers.
	void draw_chalk_grid_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_body_and_parts_(std::vector<ChalkStroke>& out, const Layout& l, float time_s,
	                          bool include_ghost);
	void draw_mirror_line_(std::vector<ChalkStroke>& out, const Layout& l);
	void draw_highlight_pulses_(std::vector<ChalkStroke>& out, const Layout& l, float time_s);

	void draw_top_bar_();
	void draw_palette_(const Layout& l, const LabInput& in, float time_s);
	void draw_loadout_(const Layout& l, const LabInput& in);
	void draw_stats_readout_(const Layout& l);
	void draw_mode_indicator_(const Layout& l);
	void draw_canvas_buttons_(const Layout& l, const LabInput& in, LabOutcome& outc);

	// Rect button helpers (pixel-space). Returns click edge.
	bool pixel_button_(float x, float y, float w, float h,
	                   const char* label, bool enabled,
	                   const LabInput& in);

	// Mouse position test against a pixel rect.
	static bool pt_in_rect_(glm::vec2 p, float x, float y, float w, float h) {
		return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
	}

	// Finalize body: smooth strokes, compute centroid → core, validate.
	void finalize_body_();
	float parts_cost_total_() const;

	// Place/remove logic (pixel-space mouse, local-space part anchors).
	void try_place_part_(glm::vec2 mouse_px, const Layout& l);
	void remove_part_at_(glm::vec2 mouse_px, const Layout& l);

	// Convert mouse pixel position to monster-local space using the canvas.
	glm::vec2 mouse_to_local_(glm::vec2 mouse_px, const Layout& l, float wobble_rad) const;
	// And the inverse for ghost preview / drawing existing parts.
	glm::vec2 local_to_canvas_px_(glm::vec2 local, const Layout& l, float wobble_rad) const;

	// Data ----------------------------------------------------------------
	Window*        window_    = nullptr;
	ChalkRenderer* renderer_  = nullptr;
	TextRenderer*  text_      = nullptr;

	Mode mode_ = Mode::DRAWING;

	// DRAWING mode.
	std::vector<ChalkStroke> strokes_;
	ChalkStroke              live_stroke_;
	bool                     drawing_ = false;

	// Validated body (set after finalize_body_).
	std::vector<glm::vec2>   smoothed_local_;    // in monster-local space, core at origin
	std::vector<glm::vec2>   validated_local_;   // same as smoothed; kept for parity with old code
	glm::vec3                color_ = glm::vec3(0.95f, 0.95f, 0.92f);
	float                    body_scale_ = 1.0f; // pixels-per-local-unit inside canvas

	// ASSEMBLING mode.
	std::vector<sim::Part>   parts_;
	int                      selected_palette_ = -1;   // PartType int, -1 = none
	float                    ghost_rotation_   = 0.0f; // radians, 30° snap on 'R'
	bool                     mirror_mode_      = false;
	float                    biomass_budget_   = 40.0f;

	// Right-rail click-to-highlight.
	int                      highlight_type_ = -1; // PartType or -1
	float                    highlight_t_    = 0.0f;

	// Tactile placement flash — short-lived burst markers in pixel space.
	struct Flash { glm::vec2 pos_px; float t_left; };
	std::vector<Flash>       flashes_;

	// Status bar text (top bar or mode indicator).
	std::string              status_;
	glm::vec3                status_color_ = glm::vec3(0.85f);

	// Gentle visual wobble so the creature looks alive.
	float                    time_acc_ = 0.0f;

	// Edge-state trackers for right-click (we get edge from input now).
	bool prev_right_ = false;
};

} // namespace civcraft::lifecraft
