#pragma once

// BodyAnimator (Tier 1) — derives torso twist + left-arm counter-swing
// from the attack's right-arm angles, so a swing reads as a whole-body
// motion rather than just a flailing limb.
//
// The procedural derivation keeps everything in lockstep with the
// existing combo keyframes (no separate per-clip data to maintain):
//   torso_yaw       =  arm_yaw   * 0.30   (shoulders rotate with the swing)
//   left_arm_pitch  = -arm_pitch * 0.50   (counter-balance)
//   left_arm_yaw    = -arm_yaw   * 0.40   (counter-balance)
//
// Removable by toggling kEnable; pre-Tier-1 behavior was: torso/head
// kept idle motion, left arm + legs froze (handled in model.cpp).

namespace civcraft {

class BodyAnimator {
public:
	static constexpr bool kEnable = true;

	// outTorsoYaw, outLeftArmPitch, outLeftArmYaw are in degrees.
	// Caller writes them into AnimState before invoking the model draw.
	static void derive(float armPitch, float armYaw,
	                   float& outTorsoYaw,
	                   float& outLeftArmPitch,
	                   float& outLeftArmYaw) {
		if (!kEnable) {
			outTorsoYaw     = 0.f;
			outLeftArmPitch = 0.f;
			outLeftArmYaw   = 0.f;
			return;
		}
		outTorsoYaw     =  armYaw   * 0.30f;
		outLeftArmPitch = -armPitch * 0.50f;
		outLeftArmYaw   = -armYaw   * 0.40f;
	}
};

} // namespace civcraft
