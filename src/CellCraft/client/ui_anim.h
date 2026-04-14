#pragma once

// Per-button animation state: hover grow, press squish+overshoot,
// and idle bobble. Callers stash one of these per persistent button
// identity (string key) in a map and feed it hover/press edges each
// frame; the `scale` and `offset_y` outputs are consumed by ui_button.

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace civcraft::cellcraft::ui {

struct AnimState {
	float hover_t  = 0.0f;  // 0..1 hover easing driver
	float press_t  = 0.0f;  // >0 → press animation active, counts down
	float idle_ph  = 0.0f;  // random phase offset for desynchronised bobble
	float time_acc = 0.0f;  // accumulates dt for idle bobble
	bool  was_down = false;
	bool  ever_ticked = false;

	// Drive from current hover/down state for this frame.
	// press_edge is true on the frame a click is registered.
	void tick(float dt, bool hovered, bool press_edge) {
		if (!ever_ticked) {
			// Stable hash-ish from pointer-ish values → random-ish phase.
			idle_ph = std::fmod(time_acc * 9.17f + 0.37f, 6.28318f);
			ever_ticked = true;
		}
		time_acc += dt;

		// Hover easing: exponential approach (cache-warm simple model).
		float target = hovered ? 1.0f : 0.0f;
		float k = 1.0f - std::exp(-dt * 10.0f);   // ~150ms time constant
		hover_t += (target - hover_t) * k;

		if (press_edge) press_t = 0.20f;           // 200ms press anim
		press_t = std::max(0.0f, press_t - dt);
	}

	// Current animated scale for the button (about center).
	float scale(bool idle_bobble = false) const {
		// Hover: 1.0 → 1.06
		float s = 1.0f + 0.06f * hover_t;
		// Press: squish at 1.0→0.95 for first 80ms, overshoot to 1.03
		// at 120ms, settle to 1.0 at 200ms. Because press_t counts
		// *down* from 0.20 to 0.0, elapsed = 0.20 - press_t.
		if (press_t > 0.0f) {
			float el = 0.20f - press_t;          // 0 → 0.20
			float ps;
			if (el < 0.080f)      ps = 1.0f + (0.95f - 1.0f) * (el / 0.080f);
			else if (el < 0.120f) ps = 0.95f + (1.03f - 0.95f) * ((el - 0.080f) / 0.040f);
			else                  ps = 1.03f + (1.0f - 1.03f) * ((el - 0.120f) / 0.080f);
			s *= ps;
		}
		// Idle bobble (tiles / LET'S GO): ±1.5% scale wobble at ~0.4 Hz.
		if (idle_bobble) {
			s *= 1.0f + 0.015f * std::sin(time_acc * 2.5f + idle_ph);
		}
		return s;
	}

	// Vertical NDC offset for idle bobble (±0.006 NDC ≈ ±4px at 720p).
	float idle_offset(bool on) const {
		if (!on) return 0.0f;
		return 0.006f * std::sin(time_acc * 2.513f + idle_ph);
	}
};

// Keyed map wrapper so callers can write `anim.get("play").tick(...)`.
class AnimMap {
public:
	AnimState& get(const std::string& key) { return states_[key]; }
private:
	std::unordered_map<std::string, AnimState> states_;
};

} // namespace civcraft::cellcraft::ui
