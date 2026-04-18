#pragma once

// Flatten BoxModel + AnimState into 19 floats per box (mat4 + rgb) for
// instanced drawBoxModel.
// Instance format: mat4 model (column-major, [0,1]^3 → world) + vec3 rgb.

#include "client/box_model.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <vector>

namespace civcraft {

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
                                     const BodyPart& part, float s) {
	glm::mat4 m = glm::translate(partMat, (part.offset - part.halfSize) * s);
	return glm::scale(m, part.halfSize * 2.0f * s);
}

// Anchor item at gripPos, apply EquipTransform (Y-X-Z rotation order).
inline void emitHeldItem(std::vector<float>& out,
                         const HeldItem& hi,
                         const glm::mat4& handFrame,
                         const glm::vec3& gripPos,
                         float parentScale) {
	if (!hi.model) return;
	const BoxModel& itemModel = *hi.model;

	glm::mat4 root = handFrame;
	root = glm::translate(root, gripPos * parentScale);

	const EquipTransform& eqt = itemModel.equip;
	root = glm::translate(root, eqt.offset * parentScale);
	root = glm::rotate(root, glm::radians(eqt.rotation.y), glm::vec3(0, 1, 0));
	root = glm::rotate(root, glm::radians(eqt.rotation.x), glm::vec3(1, 0, 0));
	root = glm::rotate(root, glm::radians(eqt.rotation.z), glm::vec3(0, 0, 1));

	float es = eqt.scale;
	bool hasEquip = (eqt.rotation != glm::vec3(0)
	                 || eqt.offset   != glm::vec3(0)
	                 || eqt.scale    != 1.0f);
	if (!hasEquip) {
		float mh = std::max(itemModel.totalHeight * itemModel.modelScale, 0.1f);
		es = std::min(0.35f / mh, 0.5f);
	}
	root = glm::scale(root, glm::vec3(es * hi.scale));

	float itemScale = itemModel.modelScale;
	for (const auto& part : itemModel.parts) {
		glm::mat4 m = partUnitCubeToWorld(root, part, itemScale);
		emitBox(out, m, glm::vec3(part.color));
	}
}

} // namespace detail

// Append 19 floats per part (matrix + color). No hit-flash tint — apply via
// shader push constant if needed.
inline void appendBoxModel(std::vector<float>& out,
                           const BoxModel& model,
                           glm::vec3 feetPos, float yaw,
                           const AnimState& anim,
                           const HeldItems* held = nullptr) {
	constexpr float TWO_PI = 6.28318530718f;
	float s = model.modelScale;

	float walkPhase = anim.walkDistance * model.walkCycleSpeed;
	float speedFactor = std::min(anim.speed / 6.0f, 1.0f);
	float smoothSpeed = speedFactor * speedFactor * (3.0f - 2.0f * speedFactor);

	float walkBob = 0.0f;
	if (smoothSpeed > 0.05f) {
		float rawBob = std::abs(std::sin(walkPhase));
		walkBob = rawBob * rawBob * model.walkBobAmount * smoothSpeed * s;
	}
	float idleBob = 0.0f;
	if (smoothSpeed < 0.1f && !anim.suppressIdleBob) {
		float idleBlend = 1.0f - smoothSpeed / 0.1f;
		idleBob = std::sin(anim.time * model.idleBobSpeed) * model.idleBobAmount
		          * idleBlend * s;
	}

	glm::mat4 root = glm::translate(glm::mat4(1.0f),
		feetPos + glm::vec3(0, idleBob + walkBob, 0));
	root = glm::rotate(root, glm::radians(-yaw - 90.0f), glm::vec3(0, 1, 0));
	if (smoothSpeed > 0.05f) {
		float lean = smoothSpeed * 3.5f;
		root = glm::rotate(root, glm::radians(lean), glm::vec3(1, 0, 0));
	}

	const AnimClip* activeClip = nullptr;
	if (!anim.currentClip.empty()) {
		auto it = model.clips.find(anim.currentClip);
		if (it != model.clips.end()) activeClip = &it->second;
	}

	glm::mat4 rightHandFrame(1.0f);
	glm::mat4 leftHandFrame(1.0f);
	bool gotRightHand = false;
	bool gotLeftHand = false;

	for (const auto& part : model.parts) {
		glm::mat4 partMat = root;

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
					partMat = glm::translate(partMat, part.pivot * s);
					partMat = glm::rotate(partMat, glm::radians(anim.armPitch), part.swingAxis);
					partMat = glm::rotate(partMat, glm::radians(anim.armYaw),   glm::vec3(0, 1, 0));
					if (std::abs(anim.armRoll) > 0.01f) {
						partMat = glm::rotate(partMat, glm::radians(anim.armRoll),
						                      glm::vec3(0, -1, 0));
					}
					partMat = glm::translate(partMat, -part.pivot * s);
					doSwing = false;
				} else if (part.name == "left_hand") {
					partMat = glm::translate(partMat, part.pivot * s);
					partMat = glm::rotate(partMat, glm::radians(anim.leftArmPitch),
					                      part.swingAxis);
					partMat = glm::rotate(partMat, glm::radians(anim.leftArmYaw),
					                      glm::vec3(0, 1, 0));
					partMat = glm::translate(partMat, -part.pivot * s);
					doSwing = false;
				} else if (part.name == "torso") {
					partMat = glm::translate(partMat, part.pivot * s);
					partMat = glm::rotate(partMat, glm::radians(anim.torsoYaw),
					                      glm::vec3(0, 1, 0));
					partMat = glm::translate(partMat, -part.pivot * s);
				} else if (part.name == "left_leg" || part.name == "right_leg") {
					doSwing = false;
				}
			}

			if (doSwing) {
				partMat = glm::translate(partMat, part.pivot * s);
				partMat = glm::rotate(partMat, angle, swingAxis);
				partMat = glm::translate(partMat, -part.pivot * s);
			}
		}

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
			partMat = glm::translate(partMat, model.headPivot * s);
			partMat = glm::rotate(partMat, anim.lookYaw,   glm::vec3(0, 1, 0));
			partMat = glm::rotate(partMat, anim.lookPitch, glm::vec3(1, 0, 0));
			partMat = glm::translate(partMat, -model.headPivot * s);
		}

		glm::mat4 finalMat = detail::partUnitCubeToWorld(partMat, part, s);
		detail::emitBox(out, finalMat, glm::vec3(part.color));
	}

	if (held) {
		if (gotRightHand) detail::emitHeldItem(out, held->rightHand, rightHandFrame, model.handR, s);
		if (gotLeftHand)  detail::emitHeldItem(out, held->leftHand,  leftHandFrame,  model.handL, s);
	}
}

// Emit single AABB as a 19-float record (for highlights, debug boxes).
inline void emitAABox(std::vector<float>& out,
                      glm::vec3 corner, glm::vec3 size, glm::vec3 color) {
	glm::mat4 m = glm::translate(glm::mat4(1.0f), corner);
	m = glm::scale(m, size);
	detail::emitBox(out, m, color);
}

} // namespace civcraft
