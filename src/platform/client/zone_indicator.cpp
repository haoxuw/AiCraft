#include "client/zone_indicator.h"
#include "logic/chunk_source.h"

#include <algorithm>
#include <cmath>

namespace solarium {

void ZoneIndicator::update(float dt, glm::vec3 cameraWorldPos, ChunkSource& world) {
	const Zone here = world.zoneAt(static_cast<int>(std::floor(cameraWorldPos.x)),
	                               static_cast<int>(std::floor(cameraWorldPos.z)));

	// Edge-detect: a new zone restarts the FadeIn cycle even mid-FadeOut, so
	// the player always sees the latest zone they entered.
	if (here != m_displayed && here != m_pending) {
		m_pending = here;
		m_phase   = Phase::FadeIn;
	}

	switch (m_phase) {
	case Phase::Idle:
		// Nothing to do; held in zero alpha.
		break;
	case Phase::FadeIn:
		// Once any visible alpha exists we lock in the pending zone as the
		// displayed one (so the label text matches what's fading in).
		m_displayed = m_pending;
		m_alpha    += dt / kFadeInSec;
		if (m_alpha >= 1.0f) {
			m_alpha     = 1.0f;
			m_holdTimer = kHoldSec;
			m_phase     = Phase::Hold;
		}
		break;
	case Phase::Hold:
		m_holdTimer -= dt;
		if (m_holdTimer <= 0.0f) m_phase = Phase::FadeOut;
		break;
	case Phase::FadeOut:
		m_alpha -= dt / kFadeOutSec;
		if (m_alpha <= 0.0f) {
			m_alpha = 0.0f;
			m_phase = Phase::Idle;
		}
		break;
	}
}

}  // namespace solarium
