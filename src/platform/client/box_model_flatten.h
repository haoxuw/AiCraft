#pragma once

// Flatten BoxModel + AnimState into 19 floats per box (mat4 + rgb) for
// instanced drawBoxModel.
// Instance format: mat4 model (column-major, [0,1]^3 → world) + vec3 rgb.
//
// All sizes are in absolute world-block units. There is no model-level
// scale: artifacts express geometry directly via part offset/size.

#include "client/box_model.h"
#include "client/rig.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <vector>

namespace solarium {

namespace detail {

inline void emitBox(std::vector<float>& out, const glm::mat4& m,
                    const glm::vec3& color) {
	const float* p = glm::value_ptr(m);
	for (int i = 0; i < 16; i++) out.push_back(p[i]);
	out.push_back(color.r);
	out.push_back(color.g);
	out.push_back(color.b);
}

// Maps unit cube [0,1]^3 to world-space oriented box for `part`.
inline glm::mat4 partUnitCubeToWorld(const glm::mat4& partMat,
                                     const BodyPart& part) {
	glm::mat4 m = glm::translate(partMat, part.offset - part.halfSize);
	return glm::scale(m, part.halfSize * 2.0f);
}

// Anchor item at gripPos, apply EquipTransform (Y-X-Z rotation order).
inline void emitHeldItem(std::vector<float>& out,
                         const HeldItem& hi,
                         const glm::mat4& handFrame,
                         const glm::vec3& gripPos) {
	if (!hi.model) return;
	const BoxModel& itemModel = *hi.model;

	glm::mat4 root = handFrame;
	root = glm::translate(root, gripPos);

	const EquipTransform& eqt = itemModel.equip;
	root = glm::translate(root, eqt.offset);
	root = glm::rotate(root, glm::radians(eqt.rotation.y), glm::vec3(0, 1, 0));
	root = glm::rotate(root, glm::radians(eqt.rotation.x), glm::vec3(1, 0, 0));
	root = glm::rotate(root, glm::radians(eqt.rotation.z), glm::vec3(0, 0, 1));

	// hi.scale is a per-instance runtime knob (e.g. blocks shrink in hand).
	if (hi.scale != 1.0f) root = glm::scale(root, glm::vec3(hi.scale));

	for (const auto& part : itemModel.parts) {
		glm::mat4 m = partUnitCubeToWorld(root, part);
		emitBox(out, m, glm::vec3(part.color));
	}
}

} // namespace detail

// Append 19 floats per part (matrix + color). No hit-flash tint — apply via
// shader push constant if needed.
//
// rootOverride: if non-null, use this matrix as the model's root frame
// instead of building one from feetPos+yaw. Lets HUD callers orient items
// fully along the camera basis (right/up/back) so slot items stay upright
// at any camera pitch, not just yaw.
// `rig`: optional rig template resolved by the caller from `model.rigId` via
// the RigRegistry. When non-null AND a part has `part.bone` set, that part
// follows the bone's accumulated parent-chain transform via `computeRigPose`
// instead of running the per-part sin-driver / clip-override / attack
// special-case path. Parts without `bone:` are unaffected. If `rig` is null,
// every part runs the legacy path — existing creatures see no behavior
// change.
inline void appendBoxModel(std::vector<float>& out,
                           const BoxModel& model,
                           glm::vec3 feetPos, float yaw,
                           const AnimState& anim,
                           const HeldItems* held = nullptr,
                           const glm::mat4* rootOverride = nullptr,
                           const Rig* rig = nullptr) {
	constexpr float TWO_PI = 6.28318530718f;

	float walkPhase = anim.walkDistance * model.walkCycleSpeed;
	float speedFactor = std::min(anim.speed / 6.0f, 1.0f);
	float smoothSpeed = speedFactor * speedFactor * (3.0f - 2.0f * speedFactor);

	float walkBob = 0.0f;
	if (smoothSpeed > 0.05f) {
		float rawBob = std::abs(std::sin(walkPhase));
		walkBob = rawBob * rawBob * model.walkBobAmount * smoothSpeed;
	}
	float idleBob = 0.0f;
	if (smoothSpeed < 0.1f && !anim.suppressIdleBob) {
		float idleBlend = 1.0f - smoothSpeed / 0.1f;
		idleBob = std::sin(anim.time * model.idleBobSpeed) * model.idleBobAmount
		          * idleBlend;
	}

	glm::mat4 root;
	if (rootOverride) {
		root = *rootOverride;
	} else {
		root = glm::translate(glm::mat4(1.0f),
			feetPos + glm::vec3(0, idleBob + walkBob, 0));
		root = glm::rotate(root, glm::radians(-yaw - 90.0f), glm::vec3(0, 1, 0));
		if (smoothSpeed > 0.05f) {
			float lean = smoothSpeed * 3.5f;
			root = glm::rotate(root, glm::radians(lean), glm::vec3(1, 0, 0));
		}
	}

	const AnimClip* activeClip = nullptr;
	if (!anim.currentClip.empty()) {
		auto it = model.clips.find(anim.currentClip);
		if (it != model.clips.end()) activeClip = &it->second;
	}

	// Per-frame bone pose. Thread-local cache keeps the vector's allocation
	// alive across calls so a 100-entity scene doesn't churn the heap.
	thread_local std::vector<glm::mat4> rigPose;
	if (rig != nullptr) {
		computeRigPose(*rig, anim.currentClip, anim.time, rigPose);
	} else {
		rigPose.clear();
	}

	glm::mat4 rightHandFrame(1.0f);
	glm::mat4 leftHandFrame(1.0f);
	bool gotRightHand = false;
	bool gotLeftHand = false;

	for (const auto& part : model.parts) {
		glm::mat4 partMat = root;
		bool boneDriven = false;

		// Bone-driven path: the bone's accumulated transform encodes rest
		// position + parent chain + active clip's keyframe rotations. When
		// taken, skip the legacy sin-driver / clip-override / attack
		// special-case branches. `part.offset` is interpreted as bone-local
		// (relative to the bone's origin) — the editor is responsible for
		// converting model-space offsets into bone-local at bind time.
		if (rig != nullptr && !part.bone.empty()) {
			int boneIdx = findBone(*rig, part.bone);
			if (boneIdx >= 0) {
				partMat = root * rigPose[boneIdx];
				boneDriven = true;
			} else {
				// Once-per-(rigId, boneName) warning. Silent degradation
				// would mask binding typos forever; we'd rather see them.
				static thread_local std::unordered_map<std::string, char> warned;
				std::string key = rig->id + "::" + part.bone;
				if (warned.emplace(key, 1).second) {
					std::fprintf(stderr,
						"[rig] %s: part bone '%s' not found in rig — falling back to legacy path\n",
						rig->id.c_str(), part.bone.c_str());
				}
			}
		}

		if (!boneDriven) {

		glm::vec3 swingAxis = part.swingAxis;
		float swingAmp   = part.swingAmplitude;
		float swingPhase = part.swingPhase;
		float swingSpeed = part.swingSpeed;
		float swingBias  = 0.0f;
		const ClipOverride* clipOv = nullptr;
		if (activeClip && !part.name.empty()) {
			auto ovIt = activeClip->overrides.find(part.name);
			if (ovIt != activeClip->overrides.end()) {
				clipOv = &ovIt->second;
				swingAxis  = clipOv->axis;
				swingAmp   = clipOv->amplitude;
				swingPhase = clipOv->phase;
				swingSpeed = clipOv->speed;
				swingBias  = clipOv->bias;
			}
		}

		if (std::abs(swingAmp) > 0.001f || swingBias != 0.0f) {
			float angle = 0.0f;
			bool doSwing = false;
			if (clipOv) {
				angle = std::sin(anim.time * swingSpeed * TWO_PI + swingPhase)
				        * glm::radians(swingAmp)
				        + glm::radians(swingBias);
				doSwing = true;
			} else if (smoothSpeed > 0.02f) {
				angle = std::sin(walkPhase * swingSpeed + swingPhase)
				        * glm::radians(swingAmp) * smoothSpeed;
				doSwing = true;
			}

			bool hasPlayerMeleeAngles =
				(std::abs(anim.armPitch) > 0.1f || std::abs(anim.armYaw) > 0.1f);
			if (!clipOv && anim.attackPhase > 0.001f && hasPlayerMeleeAngles) {
				if (part.name == "right_hand") {
					partMat = glm::translate(partMat, part.pivot);
					partMat = glm::rotate(partMat, glm::radians(anim.armPitch), part.swingAxis);
					partMat = glm::rotate(partMat, glm::radians(anim.armYaw),   glm::vec3(0, 1, 0));
					if (std::abs(anim.armRoll) > 0.01f) {
						partMat = glm::rotate(partMat, glm::radians(anim.armRoll),
						                      glm::vec3(0, -1, 0));
					}
					partMat = glm::translate(partMat, -part.pivot);
					doSwing = false;
				} else if (part.name == "left_hand") {
					partMat = glm::translate(partMat, part.pivot);
					partMat = glm::rotate(partMat, glm::radians(anim.leftArmPitch),
					                      part.swingAxis);
					partMat = glm::rotate(partMat, glm::radians(anim.leftArmYaw),
					                      glm::vec3(0, 1, 0));
					partMat = glm::translate(partMat, -part.pivot);
					doSwing = false;
				} else if (part.name == "torso") {
					partMat = glm::translate(partMat, part.pivot);
					partMat = glm::rotate(partMat, glm::radians(anim.torsoYaw),
					                      glm::vec3(0, 1, 0));
					partMat = glm::translate(partMat, -part.pivot);
				} else if (part.name == "left_leg" || part.name == "right_leg") {
					doSwing = false;
				}
			}

			if (doSwing) {
				partMat = glm::translate(partMat, part.pivot);
				partMat = glm::rotate(partMat, angle, swingAxis);
				partMat = glm::translate(partMat, -part.pivot);
			}
		}

		}  // end if (!boneDriven) — fall through to shared post-transform

		if (!gotRightHand && part.name == "right_hand") {
			rightHandFrame = partMat;
			gotRightHand = true;
		}
		if (!gotLeftHand && part.name == "left_hand") {
			leftHandFrame = partMat;
			gotLeftHand = true;
		}

		if (part.isHead
		    && (std::abs(anim.lookYaw) > 0.001f || std::abs(anim.lookPitch) > 0.001f)) {
			partMat = glm::translate(partMat, model.headPivot);
			partMat = glm::rotate(partMat, anim.lookYaw,   glm::vec3(0, 1, 0));
			partMat = glm::rotate(partMat, anim.lookPitch, glm::vec3(1, 0, 0));
			partMat = glm::translate(partMat, -model.headPivot);
		}

		glm::mat4 finalMat = detail::partUnitCubeToWorld(partMat, part);
		detail::emitBox(out, finalMat, glm::vec3(part.color));
	}

	if (held) {
		if (gotRightHand) detail::emitHeldItem(out, held->rightHand, rightHandFrame, model.handR);
		if (gotLeftHand)  detail::emitHeldItem(out, held->leftHand,  leftHandFrame,  model.handL);
	}
}

// Emit single AABB as a 19-float record (for highlights, debug boxes).
inline void emitAABox(std::vector<float>& out,
                      glm::vec3 corner, glm::vec3 size, glm::vec3 color) {
	glm::mat4 m = glm::translate(glm::mat4(1.0f), corner);
	m = glm::scale(m, size);
	detail::emitBox(out, m, color);
}

} // namespace solarium
