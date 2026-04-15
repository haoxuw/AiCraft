#pragma once

// HitStop (Tier 2b) — when an attack lands, freeze the swing animation
// for ~70 ms. Sells the impact ("the blade bit something") without
// interfering with player input or world simulation.
//
// Mechanism: notifyHit() arms a countdown. While the countdown is
// running, attackDtScale() reports a very small multiplier; the call
// site multiplies the dt it passes to AttackAnimPlayer::update() by
// this scale. Other systems run at full dt — only the swing freezes.
//
// Removable by toggling kEnable to false (notifyHit becomes a no-op
// and attackDtScale always returns 1).

namespace civcraft {

class HitStop {
public:
	static constexpr bool  kEnable       = true;
	static constexpr float kStopDuration = 0.07f; // 70 ms freeze
	static constexpr float kStopScale    = 0.05f; // ≈20× slowdown on swing

	void notifyHit() {
		if (kEnable) m_remaining = kStopDuration;
	}

	void update(float dt) {
		if (m_remaining > 0.0f) m_remaining -= dt;
	}

	float attackDtScale() const {
		return (kEnable && m_remaining > 0.0f) ? kStopScale : 1.0f;
	}

private:
	float m_remaining = 0.0f;
};

} // namespace civcraft
