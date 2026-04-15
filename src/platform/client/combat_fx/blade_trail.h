#pragma once

// BladeTrail (Tier 2a) — short-lived particle ribbon that follows the
// blade tip during an active attack swing. Visual only — no gameplay
// impact. Removable: comment out the call site in CombatFxController
// (or flip kEnable to false).
//
// Implementation: each frame while attack is active, approximate the
// world-space sword tip from player yaw + armYaw/armPitch, then spawn
// a few interpolated particles between the previous tip and the new
// tip. Short life (≈0.18s) plus a slight upward velocity counters
// gravity so the trail reads as a clean arc instead of a falling
// dust cloud.
//
// We don't read the actual model bone — the approximation is good
// enough for a sweep ribbon, and decouples this class from
// ModelRenderer internals.

#include "client/attack_anim.h"
#include "client/particles.h"
#include "shared/entity.h"
#include <glm/glm.hpp>
#include <cmath>

namespace civcraft {

class BladeTrail {
public:
	// Master toggle — flip to false to fully disable Tier 2a.
	static constexpr bool kEnable = true;

	void update(float /*dt*/, AttackAnimPlayer& attack,
	            ParticleSystem& particles, const Entity& player) {
		if (!kEnable) return;
		if (!attack.active()) { m_haveLast = false; return; }

		float pitch = 0.f, yaw = 0.f, roll = 0.f;
		attack.currentArmAngles(pitch, yaw, roll);

		const glm::vec3 tip = computeTip(player.position, player.yaw, pitch, yaw);

		if (m_haveLast) {
			constexpr int kSegments = 4;
			for (int i = 1; i <= kSegments; ++i) {
				float t = float(i) / float(kSegments);
				Particle p;
				p.pos     = glm::mix(m_lastTip, tip, t);
				p.vel     = glm::vec3(0, 1.2f, 0); // counter the 12 m/s² gravity
				p.color   = glm::vec4(1.0f, 0.96f, 0.78f, 0.85f);
				p.maxLife = 0.18f;
				p.life    = p.maxLife;
				p.size    = 0.07f;
				particles.addParticle(p);
			}
		}
		m_lastTip   = tip;
		m_haveLast  = true;
	}

private:
	// Body yaw + arm yaw determine where the blade tip is in the swing
	// arc; arm pitch nudges it up/down. Reach (1.4 m) ≈ shoulder→sword-tip
	// at full extension. Chest base height ≈ 1.3 m matches the player model.
	static glm::vec3 computeTip(glm::vec3 playerPos, float bodyYawDeg,
	                            float armPitchDeg, float armYawDeg) {
		const float yawW   = glm::radians(bodyYawDeg);
		const float sweep  = glm::radians(-armYawDeg); // armYaw + = left sweep
		const float pitchR = glm::radians(armPitchDeg);
		const float cy = std::cos(yawW), sy = std::sin(yawW);
		const float sf = std::cos(sweep), sl = std::sin(sweep);
		// Body-yaw frame: +X is body-right, +Z is body-forward (matches
		// the renderer's pe->yaw convention used elsewhere).
		glm::vec3 swingDir(cy * sf - sy * sl, 0.0f, sy * sf + cy * sl);
		glm::vec3 chest = playerPos + glm::vec3(0.0f, 1.3f, 0.0f);
		// pitchR < 0 = arm forward+slightly-down; raise tip when level.
		float vertical = -std::sin(pitchR) * 0.5f;
		return chest + swingDir * 1.4f + glm::vec3(0.0f, vertical, 0.0f);
	}

	glm::vec3 m_lastTip = {0, 0, 0};
	bool      m_haveLast = false;
};

} // namespace civcraft
