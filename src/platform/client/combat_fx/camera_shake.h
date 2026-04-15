#pragma once

// CameraShake (Tier 2c) — small jitter added to the camera position for
// ~180 ms after a hit. Sells the impact at the viewer end of the screen,
// complementing HitStop (which sells it at the swing end).
//
// The offset is applied by the call site (game_playing.cpp) after the
// camera has been positioned for the frame and before rendering reads
// camera.position. The next frame's camera update wipes it cleanly —
// no permanent state to unwind.
//
// Removable by toggling kEnable to false.

#include <glm/glm.hpp>
#include <cmath>

namespace civcraft {

class CameraShake {
public:
	static constexpr bool  kEnable    = true;
	static constexpr float kDuration  = 0.18f;  // total shake length
	static constexpr float kAmplitude = 0.06f;  // metres at peak

	void notifyHit() {
		if (kEnable) m_remaining = kDuration;
	}

	void update(float dt) {
		if (m_remaining > 0.0f) m_remaining -= dt;
		// Phase advances every frame so the offset oscillates even when
		// remaining stays the same (in case dt happens to be tiny).
		m_phase += dt;
	}

	glm::vec3 offset() const {
		if (!kEnable || m_remaining <= 0.0f) return {0, 0, 0};
		// Quadratic decay so the shake tapers smoothly instead of cutting.
		float k = m_remaining / kDuration;       // 1 → 0
		float amp = kAmplitude * k * k;
		// Three incommensurate sine frequencies → reads as random jitter
		// without needing an RNG (deterministic, seedless).
		float t = m_phase * 60.0f;
		return {
			std::sin(t * 1.7f         ) * amp,
			std::sin(t * 2.3f + 1.3f  ) * amp,
			std::sin(t * 1.1f + 2.1f  ) * amp,
		};
	}

private:
	float m_remaining = 0.0f;
	float m_phase     = 0.0f;
};

} // namespace civcraft
