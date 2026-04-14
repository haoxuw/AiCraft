#pragma once

// Shared kids-game button drawing primitive.
//
// Visual model — every button in the game looks like this:
//   - Drop shadow (offset down-right).
//   - Faux gradient fill, top→bottom (lighter on top, darker on bottom).
//   - Top-edge white highlight stripe (simulates the glossy reflection).
//   - 4px charcoal outline on all sides.
//   - Two tiny corner-cutout rects (BG-colored) at each corner, so the
//     button reads as rounded rather than sharp. (The platform text
//     renderer has no true rounded-rect; this trick is cheap and
//     convincing at the intended sizes.)
//   - Label rendered with a dark drop-shadow offset for extra pop.

#include "client/text.h"
#include "CellCraft/client/ui_theme.h"
#include <glm/glm.hpp>
#include <string>

namespace civcraft::cellcraft::ui {

// NDC-space button. Colors default to the primary warm-gold gradient; callers
// override `top`/`bottom` for green/neutral/danger variants.
struct PillButton {
	float x, y, w, h;
	glm::vec4 top    = BTN_PRIMARY_TOP;
	glm::vec4 bottom = BTN_PRIMARY_BOTTOM;
	glm::vec4 label_fill = TEXT_LIGHT;
	std::string label;
	float label_scale = 1.1f;
	bool  enabled = true;
	bool  hovered = false;
	// Animation scale applied around the button center (1.0 = normal).
	// Used by ui_anim in Commit 3; default 1.0 = static.
	float scale = 1.0f;
};

// Draw a pill button in NDC. Renders shadow, gradient, highlight, outline,
// label. Returns the button's actual drawn NDC bounds (post-scale) so
// callers can do hit-testing against the same area.
inline void drawPill(::civcraft::TextRenderer* text, const PillButton& b,
                     float aspect_hint = 1.0f) {
	(void)aspect_hint;
	// Apply scale about center.
	float cx = b.x + b.w * 0.5f;
	float cy = b.y + b.h * 0.5f;
	float w  = b.w * b.scale;
	float h  = b.h * b.scale;
	float x  = cx - w * 0.5f;
	float y  = cy - h * 0.5f;

	glm::vec4 top = b.top, bot = b.bottom;
	if (!b.enabled) { top.a = 0.5f; bot.a = 0.5f; }
	else if (b.hovered) {
		top = glm::vec4(glm::min(glm::vec3(top) + 0.06f, glm::vec3(1.0f)), top.a);
		bot = glm::vec4(glm::min(glm::vec3(bot) + 0.06f, glm::vec3(1.0f)), bot.a);
	}

	// Shadow.
	text->drawRect(x + 0.006f, y - 0.012f, w, h, SHADOW);

	// 8-stripe gradient.
	const int STRIPES = 8;
	for (int i = 0; i < STRIPES; ++i) {
		float t0 = (float)i / (float)STRIPES;
		float t1 = (float)(i + 1) / (float)STRIPES;
		glm::vec4 c = glm::mix(top, bot, 0.5f * (t0 + t1));
		float sy = y + h * (1.0f - t1);
		float sh = h * (t1 - t0) + 0.0005f;
		text->drawRect(x, sy, w, sh, c);
	}
	// Top highlight.
	text->drawRect(x, y + h - h * 0.14f, w, h * 0.09f,
		glm::vec4(1.0f, 1.0f, 1.0f, 0.22f));

	// Outline.
	glm::vec4 edge = OUTLINE; if (!b.enabled) edge.a = 0.4f;
	float t = 0.004f;
	text->drawRect(x, y,         w, t, edge);
	text->drawRect(x, y + h - t, w, t, edge);
	text->drawRect(x, y,         t, h, edge);
	text->drawRect(x + w - t, y, t, h, edge);

	// Corner rounding — punch 2x2 tiny dark squares at each corner, then
	// re-fill with BG_CREAM so corners read as rounded against the card.
	float r = 0.006f;
	glm::vec4 bgcut = BG_CREAM;
	// Top-left
	text->drawRect(x, y + h - r, r, r, bgcut);
	// Top-right
	text->drawRect(x + w - r, y + h - r, r, r, bgcut);
	// Bottom-left
	text->drawRect(x, y, r, r, bgcut);
	// Bottom-right
	text->drawRect(x + w - r, y, r, r, bgcut);

	// Label with shadow.
	if (!b.label.empty()) {
		float label_w = (float)b.label.size() * 0.018f * b.label_scale;
		float tx = x + (w - label_w) * 0.5f;
		float ty = y + (h - 0.032f * b.label_scale) * 0.5f;
		glm::vec4 lshadow = OUTLINE; lshadow.a = b.enabled ? 0.85f : 0.4f;
		glm::vec4 fill    = b.enabled ? b.label_fill
		                              : glm::vec4(0.6f, 0.6f, 0.6f, 1.0f);
		text->drawText(b.label, tx + 0.003f, ty - 0.004f, b.label_scale, lshadow, aspect_hint);
		text->drawText(b.label, tx,          ty,          b.label_scale, fill,    aspect_hint);
	}
}

// NDC hit-test; returns true if the mouse is over the (unscaled) button
// rect. Hover scaling up doesn't enlarge the hit region, which feels
// better than a wobbling hit target.
inline bool pillHit(const PillButton& b, glm::vec2 mouse_ndc) {
	return mouse_ndc.x >= b.x && mouse_ndc.x <= b.x + b.w
	    && mouse_ndc.y >= b.y && mouse_ndc.y <= b.y + b.h;
}

} // namespace civcraft::cellcraft::ui
