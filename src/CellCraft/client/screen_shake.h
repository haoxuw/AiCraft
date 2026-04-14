// CellCraft — camera screen-shake.
// The PLAYING camera is a world-space translation (camera_world_). Callers ask
// the shake for an offset in world pixels each frame and add it into the
// camera position before rendering. Shake decays exponentially, so multiple
// impulses overlap naturally. Brief (~0.15s) and small (<10px).
#pragma once

#include <glm/glm.hpp>

namespace civcraft::cellcraft {

class ScreenShake {
public:
	// Add an impulse: amp = peak pixels, duration = decay half-life (s).
	void add(float amp, float duration = 0.15f);

	void update(float dt);

	// Current offset to add to camera position. Returns (0,0) when idle.
	glm::vec2 offset() const;

private:
	float amp_      = 0.0f;
	float decay_    = 10.0f;     // 1/s
	float phase_x_  = 0.0f;
	float phase_y_  = 1.2f;
	float time_     = 0.0f;
};

} // namespace civcraft::cellcraft
