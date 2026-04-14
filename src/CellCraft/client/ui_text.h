#pragma once

// Cartoon-outlined text helper. The platform text renderer supports
// `drawText` and `drawTitle`, but neither gives us the Fall-Guys-style
// heavy charcoal outline + colored drop-shadow look kids games use for
// headlines. This header wraps the platform renderer to draw text N
// times in charcoal, offset in the 8 compass directions, then the fill
// on top — a cheap but convincing outline.

#include "client/text.h"
#include "CellCraft/client/ui_theme.h"
#include <glm/glm.hpp>
#include <string>

namespace civcraft::cellcraft::ui {

// Draw text with a thick charcoal outline (8 offset passes) and a
// colored drop shadow beneath the fill. Coordinates are NDC, scale
// matches TextRenderer::drawText.
inline void drawOutlinedText(::civcraft::TextRenderer* text,
                             const std::string& s,
                             float x, float y, float scale,
                             glm::vec4 fill, glm::vec4 shadow_color,
                             float aspect) {
	// Colored drop shadow (offset down-right), fades out at higher scale
	// so it reads as depth not duplication.
	glm::vec4 shdw = shadow_color;
	float dx = 0.010f * (scale / 2.0f);
	float dy = 0.014f * (scale / 2.0f);
	text->drawText(s, x + dx, y - dy, scale, shdw, aspect);

	// 8-direction charcoal outline. Offsets are small NDC deltas, scaled
	// with the text size so the outline reads the same at every scale.
	float o = 0.0036f * (scale / 2.0f);
	glm::vec4 outline = OUTLINE;
	outline.a = 1.0f;
	const float dirs[8][2] = {
		{ o, 0}, {-o, 0}, {0, o}, {0,-o},
		{ o*0.7f, o*0.7f}, {-o*0.7f, o*0.7f},
		{ o*0.7f,-o*0.7f}, {-o*0.7f,-o*0.7f},
	};
	for (int i = 0; i < 8; ++i) {
		text->drawText(s, x + dirs[i][0], y + dirs[i][1], scale, outline, aspect);
	}
	// Fill on top.
	text->drawText(s, x, y, scale, fill, aspect);
}

// Same but using drawTitle (larger / sharper platform path).
inline void drawOutlinedTitle(::civcraft::TextRenderer* text,
                              const std::string& s,
                              float x, float y, float scale,
                              glm::vec4 fill, glm::vec4 shadow_color,
                              float aspect) {
	glm::vec4 shdw = shadow_color;
	float dx = 0.012f * (scale / 2.5f);
	float dy = 0.016f * (scale / 2.5f);
	text->drawTitle(s, x + dx, y - dy, scale, shdw, aspect);
	float o = 0.0050f * (scale / 2.5f);
	glm::vec4 outline = OUTLINE;
	const float dirs[8][2] = {
		{ o, 0}, {-o, 0}, {0, o}, {0,-o},
		{ o*0.7f, o*0.7f}, {-o*0.7f, o*0.7f},
		{ o*0.7f,-o*0.7f}, {-o*0.7f,-o*0.7f},
	};
	for (int i = 0; i < 8; ++i) {
		text->drawTitle(s, x + dirs[i][0], y + dirs[i][1], scale, outline, aspect);
	}
	text->drawTitle(s, x, y, scale, fill, aspect);
}

} // namespace civcraft::cellcraft::ui
