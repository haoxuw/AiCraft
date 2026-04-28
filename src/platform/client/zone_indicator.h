#pragma once

// Top-centre fade-pill that announces the zone the *camera* is currently
// over (deliberately not the player — in RTS the camera roams free of the
// avatar, and that's exactly when knowing the zone matters most). Polls
// LocalWorld via ChunkSource::zoneAt(camX, camZ) once per frame.
//
// Lifecycle: hidden when the zone hasn't changed for a few seconds; on a
// transition it fades in, holds for kHoldSeconds, then fades back out. No
// allocation on the steady-state path; the only mutation is two floats.
//
// Drawn by HudRenderer at the end of the HUD pass so it sits above other
// widgets but below modal panels (inventory, menus).

#include "logic/zone.h"

#include <glm/vec3.hpp>

namespace solarium {

class ChunkSource;
namespace vk { class RhiVk; }   // forward — full include lives in the .cpp consumer

class ZoneIndicator {
public:
	void update(float dt, glm::vec3 cameraWorldPos, ChunkSource& world);

	// Returns false if the indicator is fully transparent — caller can skip
	// the draw entirely. When true, caller invokes drawTo() to emit the
	// text2d/rect2d calls.
	bool visible() const { return m_alpha > 0.001f; }

	Zone displayedZone() const { return m_displayed; }
	float alpha() const { return m_alpha; }

private:
	enum class Phase { Idle, FadeIn, Hold, FadeOut };

	Zone  m_displayed = Zone::Unknown;     // what's currently being shown
	Zone  m_pending   = Zone::Unknown;     // what we're transitioning to
	float m_alpha     = 0.0f;              // 0..1
	float m_holdTimer = 0.0f;              // seconds remaining in Hold
	Phase m_phase     = Phase::Idle;

	static constexpr float kFadeInSec  = 0.30f;
	static constexpr float kFadeOutSec = 0.50f;
	static constexpr float kHoldSec    = 3.0f;
};

}  // namespace solarium
