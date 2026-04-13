#pragma once

#include "client/model.h"
#include "client/text.h"
#include "shared/entity.h"
#include <glm/glm.hpp>

namespace modcraft {

// Draws the floating "goal lightbulb" indicator above a living entity's head,
// plus the entity's goalText label next to it.
//
// Color:
//   - yellow (normal)
//   - red    (when e.hasError is true; goal text also rendered red)
//
// Contract: every living NPC passed to draw() MUST have a non-empty goalText.
// The lightbulb is *the visual representation* of the entity's current goal;
// drawing one with no goal is a bug. draw() throws std::runtime_error to
// surface this loudly instead of silently drawing a decoration.
class LightbulbDrawer {
public:
	LightbulbDrawer(ModelRenderer& mr, TextRenderer& tr);

	void draw(const Entity& e, const glm::mat4& viewProj,
	          float globalTime, float cameraLookYaw, float aspect);

private:
	ModelRenderer& m_mr;
	TextRenderer&  m_tr;
};

} // namespace modcraft
