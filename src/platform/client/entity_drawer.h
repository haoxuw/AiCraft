#pragma once

#include "client/model.h"
#include "shared/entity.h"
#include <glm/glm.hpp>

namespace civcraft {

// Draws a single non-player entity (creature / NPC) body with animation.
//
// Responsibilities:
//   - pick the active animation clip from goalText keywords
//   - split yaw into body yaw and capped head yaw (±45°)
//   - forward to ModelRenderer::draw with the assembled AnimState
//
// Contract: every entity passed to draw() MUST have a non-empty goalText —
// animation-clip selection reads it (see pickClip). Drawing an entity
// without a goal is a bug and draw() throws std::runtime_error.
class EntityDrawer {
public:
	explicit EntityDrawer(ModelRenderer& mr);

	void draw(const Entity& e, const BoxModel& model,
	          const glm::mat4& viewProj, float globalTime,
	          float damageFlashTint);

private:
	ModelRenderer& m_mr;
};

} // namespace civcraft
