#include "client/model.h"
#include <cmath>

namespace civcraft {

bool ModelRenderer::init(Shader* shader) {
	m_shader = shader;

	// 24 vertices (4 per face) with per-face normals and UVs.
	// UV atlas layout: 6 horizontal tiles, each 1/6 wide.
	//   tile 0 = front (-Z), 1 = back (+Z), 2 = left (-X),
	//   3 = right (+X), 4 = bottom (-Y), 5 = top (+Y)
	// Each vertex: pos.xyz, norm.xyz, uv.xy = 8 floats

	const float S = 1.0f / 6.0f; // tile width in UV space

	float verts[] = {
		// Front face (z=0), normal (0,0,-1), tile 0
		0,0,0, 0,0,-1, 0*S,0,   1,0,0, 0,0,-1, 1*S,0,
		1,1,0, 0,0,-1, 1*S,1,   0,1,0, 0,0,-1, 0*S,1,
		// Back face (z=1), normal (0,0,1), tile 1
		1,0,1, 0,0,1, 1*S,0,    0,0,1, 0,0,1, 2*S,0,
		0,1,1, 0,0,1, 2*S,1,    1,1,1, 0,0,1, 1*S,1,
		// Left face (x=0), normal (-1,0,0), tile 2
		0,0,1, -1,0,0, 2*S,0,   0,0,0, -1,0,0, 3*S,0,
		0,1,0, -1,0,0, 3*S,1,   0,1,1, -1,0,0, 2*S,1,
		// Right face (x=1), normal (1,0,0), tile 3
		1,0,0, 1,0,0, 3*S,0,    1,0,1, 1,0,0, 4*S,0,
		1,1,1, 1,0,0, 4*S,1,    1,1,0, 1,0,0, 3*S,1,
		// Bottom face (y=0), normal (0,-1,0), tile 4
		0,0,1, 0,-1,0, 4*S,0,   1,0,1, 0,-1,0, 5*S,0,
		1,0,0, 0,-1,0, 5*S,1,   0,0,0, 0,-1,0, 4*S,1,
		// Top face (y=1), normal (0,1,0), tile 5
		0,1,0, 0,1,0, 5*S,0,    1,1,0, 0,1,0, 6*S,0,
		1,1,1, 0,1,0, 6*S,1,    0,1,1, 0,1,0, 5*S,1,
	};
	unsigned int indices[] = {
		0,1,2,   0,2,3,     // Front
		4,5,6,   4,6,7,     // Back
		8,9,10,  8,10,11,   // Left
		12,13,14, 12,14,15, // Right
		16,17,18, 16,18,19, // Bottom
		20,21,22, 20,22,23, // Top
	};

	glGenVertexArrays(1, &m_cubeVAO);
	glGenBuffers(1, &m_cubeVBO);
	glGenBuffers(1, &m_cubeEBO);

	glBindVertexArray(m_cubeVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeEBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	const int stride = 8 * sizeof(float);
	// Position
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
	glEnableVertexAttribArray(0);
	// Normal
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3*sizeof(float)));
	glEnableVertexAttribArray(1);
	// UV
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6*sizeof(float)));
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);
	return true;
}

void ModelRenderer::shutdown() {
	if (m_cubeEBO) glDeleteBuffers(1, &m_cubeEBO);
	if (m_cubeVBO) glDeleteBuffers(1, &m_cubeVBO);
	if (m_cubeVAO) glDeleteVertexArrays(1, &m_cubeVAO);
}

void ModelRenderer::draw(const BoxModel& model, const glm::mat4& viewProj,
                          glm::vec3 feetPos, float yaw, const AnimState& anim,
                          float tintStrength, glm::vec3 tint, glm::vec3 sunDir,
                          const HeldItems* held) {
	m_shader->use();
	glEnable(GL_DEPTH_TEST);

	float s = model.modelScale;

	// Walk cycle phase
	float walkPhase = anim.walkDistance * model.walkCycleSpeed;

	// Speed factor
	float speedFactor = std::min(anim.speed / 6.0f, 1.0f);
	float smoothSpeed = speedFactor * speedFactor * (3.0f - 2.0f * speedFactor);

	// Vertical bounce
	float walkBob = 0.0f;
	if (smoothSpeed > 0.05f) {
		float rawBob = std::abs(std::sin(walkPhase));
		walkBob = rawBob * rawBob * model.walkBobAmount * smoothSpeed * s;
	}

	// Idle bob
	float idleBob = 0.0f;
	if (smoothSpeed < 0.1f) {
		float idleBlend = 1.0f - smoothSpeed / 0.1f;
		idleBob = std::sin(anim.time * model.idleBobSpeed) * model.idleBobAmount * idleBlend * s;
	}

	// Root transform
	glm::mat4 root = glm::translate(glm::mat4(1.0f),
		feetPos + glm::vec3(0, idleBob + walkBob, 0));
	root = glm::rotate(root, glm::radians(-yaw - 90.0f), glm::vec3(0, 1, 0));

	// Forward lean
	if (smoothSpeed > 0.05f) {
		float lean = smoothSpeed * 3.5f;
		root = glm::rotate(root, glm::radians(lean), glm::vec3(1, 0, 0));
	}

	m_shader->setVec3("uSunDir", sunDir);
	m_shader->setVec3("uTint", tint);
	m_shader->setFloat("uTintStrength", tintStrength);
	m_shader->setInt("uPartTex", 0);

	GLint colorLoc = glGetUniformLocation(m_shader->id(), "uColor");
	GLint useTexLoc = glGetUniformLocation(m_shader->id(), "uUseTexture");
	glBindVertexArray(m_cubeVAO);

	constexpr float TWO_PI = 6.28318530718f;

	// Is a named animation clip currently active? (mine, chop, dance, wave, ...)
	const AnimClip* activeClip = nullptr;
	if (!anim.currentClip.empty()) {
		auto it = model.clips.find(anim.currentClip);
		if (it != model.clips.end()) activeClip = &it->second;
	}
	// Captured hand frames — set when we encounter the named "right_hand" /
	// "left_hand" parts in the loop. Used to anchor held items after the
	// loop completes, so the items inherit the same swing/clip/melee
	// transform as the hand they're attached to.
	glm::mat4 rightHandFrame(1.0f);
	glm::mat4 leftHandFrame(1.0f);
	bool gotRightHand = false;
	bool gotLeftHand = false;

	for (auto& part : model.parts) {
		glm::mat4 partMat = root;

		// Resolve swing params: a named clip override (if this part is in it)
		// REPLACES the part's default walk-swing params for the duration of
		// the clip. Parts not in the clip keep walking normally.
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

		// Limb swing: clip override → walk cycle → player-melee override
		// abs() so negative amplitudes (direction-flipped swings) still fire.
		if (std::abs(swingAmp) > 0.001f || swingBias != 0.0f) {
			float angle = 0.0f;
			bool  doSwing = false;

			if (clipOv) {
				// Clip is active and drives this part — runs regardless of walk
				// speed (so dance/wave/sleep animate while standing still).
				// Phase driver: real time × speed × 2π → speed=1 gives 1 Hz.
				angle = std::sin(anim.time * swingSpeed * TWO_PI + swingPhase)
				        * glm::radians(swingAmp)
				        + glm::radians(swingBias);
				doSwing = true;
			} else if (smoothSpeed > 0.02f) {
				// Walk cycle (speed-gated)
				angle = std::sin(walkPhase * swingSpeed + swingPhase)
				        * glm::radians(swingAmp) * smoothSpeed;
				doSwing = true;
			}

			// Player melee attack override (keyframe-driven arm angles).
			// ONLY fires when the local player's AttackAnimPlayer has populated
			// armPitch/armYaw. Mob work-swings now go through clips instead of
			// the old amplitude-sentinel heuristic. Right arm is identified by
			// name so it works regardless of phase direction.
			bool hasPlayerMeleeAngles =
				(std::abs(anim.armPitch) > 0.1f || std::abs(anim.armYaw) > 0.1f);
			if (!clipOv && anim.attackPhase > 0.001f && hasPlayerMeleeAngles) {
				if (part.name == "right_hand") {
					partMat = glm::translate(partMat, part.pivot * s);
					partMat = glm::rotate(partMat, glm::radians(anim.armPitch), part.swingAxis);
					partMat = glm::rotate(partMat, glm::radians(anim.armYaw),   glm::vec3(0, 1, 0));
					partMat = glm::translate(partMat, -part.pivot * s);
					doSwing = false; // already applied
				} else if (part.name == "left_hand"
				           || part.name == "left_leg" || part.name == "right_leg") {
					// Freeze non-swinging arm and both legs during an attack —
					// otherwise the whole body bobs and looks silly while the
					// sword swings. Torso/head keep their idle motion.
					doSwing = false;
				}
			}

			if (doSwing) {
				partMat = glm::translate(partMat, part.pivot * s);
				partMat = glm::rotate(partMat, angle, swingAxis);
				partMat = glm::translate(partMat, -part.pivot * s);
			}
		}

		// Capture hand frames for held items. Done AFTER swing/clip/melee
		// transforms but BEFORE head tracking and offset/scale, so the
		// captured matrix represents the bone-space frame of the hand.
		if (!gotRightHand && part.name == "right_hand") {
			rightHandFrame = partMat;
			gotRightHand = true;
		}
		if (!gotLeftHand && part.name == "left_hand") {
			leftHandFrame = partMat;
			gotLeftHand = true;
		}

		// Head tracking — applied AFTER walk/clip swing, around the head pivot.
		// Minecraft-style: yaw rotates horizontally, pitch tilts vertically.
		// Values are expected to be clamped (e.g. ±45° yaw) before being passed.
		if (part.isHead
		    && (std::abs(anim.lookYaw) > 0.001f || std::abs(anim.lookPitch) > 0.001f)) {
			partMat = glm::translate(partMat, model.headPivot * s);
			partMat = glm::rotate(partMat, anim.lookYaw,   glm::vec3(0, 1, 0));
			partMat = glm::rotate(partMat, anim.lookPitch, glm::vec3(1, 0, 0));
			partMat = glm::translate(partMat, -model.headPivot * s);
		}

		// Position and scale
		glm::mat4 worldMat = glm::translate(partMat, (part.offset - part.halfSize) * s);
		partMat = glm::scale(worldMat, part.halfSize * 2.0f * s);

		m_shader->setMat4("uModel", worldMat);
		m_shader->setMat4("uMVP", viewProj * partMat);

		if (part.texture != 0) {
			// Textured part: bind texture, shader samples it via UVs
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, part.texture);
			glUniform1i(useTexLoc, 1);
		} else {
			// Flat color part
			glUniform1i(useTexLoc, 0);
			glUniform4f(colorLoc, part.color.r, part.color.g, part.color.b, part.color.a);
		}

		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
	}

	// ── Held items ──
	// Drawn after the parts loop so they inherit the captured hand frame.
	// Each item is anchored at the model's hand grip position (handR/handL),
	// then transformed by its own EquipTransform (rotation/offset/scale)
	// declared in the item's BoxModel.equip block.
	auto drawHeld = [&](const HeldItem& hi, const glm::mat4& handFrame, const glm::vec3& gripPos) {
		if (!hi.model) return;
		const BoxModel& itemModel = *hi.model;

		// Anchor at the hand grip in the parent character's coordinate system.
		// `s` is the parent character's modelScale (already in scope).
		glm::mat4 root = handFrame;
		root = glm::translate(root, gripPos * s);

		// Apply the item's equip transform (Z, Y, X rotation order matches
		// the FPS held-item path in game_render.cpp so item poses are consistent).
		const EquipTransform& eqt = itemModel.equip;
		root = glm::translate(root, eqt.offset * s);
		root = glm::rotate(root, glm::radians(eqt.rotation.y), glm::vec3(0, 1, 0));
		root = glm::rotate(root, glm::radians(eqt.rotation.x), glm::vec3(1, 0, 0));
		root = glm::rotate(root, glm::radians(eqt.rotation.z), glm::vec3(0, 0, 1));
		// equip.scale is the per-item "how big when held" multiplier.
		// hi.scale is an extra runtime shrink (e.g. blocks shown small).
		// Items without an explicit equip transform (notably blocks — their
		// BoxModel is a 1m cube) get auto-shrunk to ~0.35m so they fit the
		// hand instead of engulfing the holder. Mirrors the FPS HUD heuristic
		// in game_render.cpp.
		float es = eqt.scale;
		bool hasEquip = (eqt.rotation != glm::vec3(0)
		                 || eqt.offset   != glm::vec3(0)
		                 || eqt.scale    != 1.0f);
		if (!hasEquip) {
			float mh = std::max(itemModel.totalHeight * itemModel.modelScale, 0.1f);
			es = std::min(0.35f / mh, 0.5f);
		}
		// Intentionally NOT multiplied by parent `s`: held items should
		// render at their intrinsic world size (same as on the ground),
		// not get inflated by the character's modelScale.
		root = glm::scale(root, glm::vec3(es * hi.scale));

		// Draw item parts using the item's own scale internally. We pass the
		// pre-scaled root, but the per-part offsets still want to be in the
		// item's intrinsic units, so undo s and pass the item's intrinsic
		// modelScale via a per-part loop here (same shape as drawStatic).
		float itemScale = itemModel.modelScale;

		for (auto& part : itemModel.parts) {
			glm::mat4 partMat = root;
			glm::mat4 worldMat = glm::translate(partMat, (part.offset - part.halfSize) * itemScale);
			partMat = glm::scale(worldMat, part.halfSize * 2.0f * itemScale);
			m_shader->setMat4("uModel", worldMat);
			m_shader->setMat4("uMVP", viewProj * partMat);
			if (part.texture != 0) {
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, part.texture);
				glUniform1i(useTexLoc, 1);
			} else {
				glUniform1i(useTexLoc, 0);
				glUniform4f(colorLoc, part.color.r, part.color.g, part.color.b, part.color.a);
			}
			glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
		}
	};

	if (held) {
		// Items inherit no character tint (so a hit-flash on the holder
		// doesn't bleed onto the sword). Reset tint for held draws.
		m_shader->setFloat("uTintStrength", 0.0f);
		if (gotRightHand) drawHeld(held->rightHand, rightHandFrame, model.handR);
		if (gotLeftHand)  drawHeld(held->leftHand,  leftHandFrame,  model.handL);
		// Restore tint state for any subsequent draw() calls in the same frame.
		m_shader->setFloat("uTintStrength", tintStrength);
	}

	// Unbind texture
	glUniform1i(useTexLoc, 0);
}

void ModelRenderer::drawStatic(const BoxModel& model, const glm::mat4& viewProj,
                                const glm::mat4& rootTransform, glm::vec3 sunDir) {
	m_shader->use();
	glEnable(GL_DEPTH_TEST);

	float s = model.modelScale;

	m_shader->setVec3("uSunDir", sunDir);
	m_shader->setFloat("uTintStrength", 0.0f); // no tint for static draws (FPS item, icons)
	m_shader->setInt("uPartTex", 0);

	GLint colorLoc = glGetUniformLocation(m_shader->id(), "uColor");
	GLint useTexLoc = glGetUniformLocation(m_shader->id(), "uUseTexture");
	glBindVertexArray(m_cubeVAO);

	for (auto& part : model.parts) {
		glm::mat4 partMat = rootTransform;

		glm::mat4 worldMat = glm::translate(partMat, (part.offset - part.halfSize) * s);
		partMat = glm::scale(worldMat, part.halfSize * 2.0f * s);

		m_shader->setMat4("uModel", worldMat);
		m_shader->setMat4("uMVP", viewProj * partMat);

		if (part.texture != 0) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, part.texture);
			glUniform1i(useTexLoc, 1);
		} else {
			glUniform1i(useTexLoc, 0);
			glUniform4f(colorLoc, part.color.r, part.color.g, part.color.b, part.color.a);
		}

		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
	}
	glUniform1i(useTexLoc, 0);
}

} // namespace civcraft
