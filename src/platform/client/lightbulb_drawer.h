#pragma once

#include "client/rhi/rhi.h"
#include "logic/entity.h"
#include <glm/glm.hpp>

namespace civcraft {

// Goal indicator above an NPC's head: an animated SDF "!" marker plus the
// entity's goalText label. Pure NDC rendering through the RHI's drawTitle2D
// / drawText2D, so GL and Vulkan backends produce pixel-identical output
// from the same shared font atlas.
//
// Visual language (modern quest-marker style):
//   - "!" glyph in title mode (fill + dark outline + soft glow), pulsing
//     gently; it's the eye-catcher.
//   - Goal label above the "!" in plain SDF text, slightly smaller — it's
//     the supporting detail.
//   - Both tint warm gold for healthy NPCs, muted red when `hasError` is
//     set (see the "Red lightbulb = broken entity" convention).
//
// Contract: every living NPC passed to draw() MUST have a non-empty goalText.
// The indicator is *the visual representation* of the entity's current goal;
// drawing one with no goal is a bug. draw() throws std::runtime_error to
// surface this loudly instead of silently drawing a decoration.
class LightbulbDrawer {
public:
	explicit LightbulbDrawer(rhi::IRhi& rhi);

	void draw(const Entity& e, const glm::mat4& viewProj,
	          float globalTime, float cameraLookYaw, float aspect);

private:
	rhi::IRhi& m_rhi;
};

} // namespace civcraft
