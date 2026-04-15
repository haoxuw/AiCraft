#pragma once

// CombatFxController — single owner for all combat feedback effects.
//
// Tiers plug in here as separate modules so each can be removed by
// commenting out its call site (or its member) without touching the
// rest of the system:
//
//   Tier 0 — wrist roll           (lives in AttackAnimPlayer, data-only)
//   Tier 2a — BladeTrail          (ribbon behind sword tip)         TODO
//   Tier 2b — HitStop             (freeze-frame on impact)          TODO
//   Tier 2c — CameraShake         (small camera kick on peak)       TODO
//   Tier 1  — BodyAnimator        (torso/hip/head/left-arm)         TODO
//
// Currently hosts the swing-peak shockwave: AttackAnimPlayer raises a
// one-shot peak event at kPeakFrac; we emit a horizontal shockwave in
// front of the attacker. Before this refactor the same block lived
// inline in game_playing.cpp.

#include "client/attack_anim.h"
#include "client/combat_fx/blade_trail.h"
#include "client/combat_fx/camera_shake.h"
#include "client/combat_fx/hit_stop.h"
#include "client/particles.h"
#include "shared/entity.h"
#include <glm/glm.hpp>

namespace civcraft {

class CombatFxController {
public:
	// Called once per frame after AttackAnimPlayer::update(dt).
	// `player` is the locally-controlled entity (position + yaw drive FX).
	void update(float dt, AttackAnimPlayer& attack,
	            ParticleSystem& particles, const Entity& player) {
		// Tier 2b/2c — drain hit-stop + camera-shake countdowns each frame.
		m_hitStop.update(dt);
		m_cameraShake.update(dt);
		// Tier 2a — ribbon trail follows the swinging blade tip.
		m_bladeTrail.update(dt, attack, particles, player);
		// Tier 0-shockwave: one-shot ring at swing peak.
		if (attack.consumePeakEvent())
			emitShockwave(particles, player);
	}

	// Call from the gameplay layer the moment a player attack lands a valid
	// hit (HP-deduction action is sent). Kicks off the hit-stop freeze.
	void notifyHit() {
		m_hitStop.notifyHit();
		m_cameraShake.notifyHit();
	}

	// Multiplier the call site applies to the dt it passes to
	// AttackAnimPlayer::update. 1.0 normally; ≪1 during hit-stop.
	float attackDtScale() const { return m_hitStop.attackDtScale(); }

	// Position offset to add to camera.position for the current frame.
	// Caller adds it after camera update and before render reads position.
	glm::vec3 cameraShakeOffset() const { return m_cameraShake.offset(); }

private:
	BladeTrail  m_bladeTrail;
	HitStop     m_hitStop;
	CameraShake m_cameraShake;

	// Chest-height ring ~0.8m in front of the attacker. Normal = +Y so
	// the visible arc reads as a ground-level sweep; facing yaw centres
	// the 180° spread forward.
	static void emitShockwave(ParticleSystem& particles, const Entity& player) {
		float yawRad = glm::radians(player.yaw);
		glm::vec3 fwd(std::cos(yawRad), 0.0f, std::sin(yawRad));
		glm::vec3 center = player.position + glm::vec3(0, 1.1f, 0) + fwd * 0.8f;
		particles.emitSwingShockwave(center, glm::vec3(0, 1, 0), yawRad,
		                             glm::vec3(0.85f, 0.92f, 1.0f), 24);
	}
};

} // namespace civcraft
