#include "CellCraft/client/screen_shake.h"

#include <cmath>

namespace civcraft::cellcraft {

void ScreenShake::add(float amp, float duration) {
	// Use the stronger of current residual vs new impulse.
	if (amp > amp_) amp_ = amp;
	// decay so amplitude halves at `duration`.
	decay_ = (duration > 0.001f) ? (0.69314718f / duration) : 10.0f;
	time_  = 0.0f;
}

void ScreenShake::update(float dt) {
	time_ += dt;
	if (amp_ > 0.01f) amp_ *= std::exp(-decay_ * dt);
	else              amp_  = 0.0f;
}

glm::vec2 ScreenShake::offset() const {
	if (amp_ <= 0.01f) return glm::vec2(0.0f);
	// Two detuned sines for a jittery feel.
	float x = std::sin(time_ * 83.0f + phase_x_) + 0.6f * std::sin(time_ * 47.0f);
	float y = std::sin(time_ * 73.0f + phase_y_) + 0.6f * std::sin(time_ * 53.0f);
	return glm::vec2(x, y) * amp_;
}

} // namespace civcraft::cellcraft
