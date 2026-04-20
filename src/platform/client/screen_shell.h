#pragma once

// ScreenShell — shared chrome layout for immersive preview screens
// (CharacterSelect, Handbook). Both screens anchor navigation to the left
// edge and actions/hints to the bottom edge; the rest of the screen is a
// full-bleed 3D preview area. A Tab toggle lifts a text "cover" over the
// preview when the user wants to read long-form detail without losing
// the bars.
//
// State fields here are the single source of truth for the menu-mode
// preview: the camera pin in game_vk.cpp reads shell.previewId to decide
// whether to hold steady, and the world renderer injects the model at
// shell.previewId into the entity stream. The owning screen (character
// select, handbook) mutates these fields each frame based on cursor
// selection — the shell itself doesn't know what "playable" means.

#include "client/rhi/rhi.h"
#include "client/ui_kit.h"

#include <cstring>
#include <string>

namespace civcraft::vk {

struct ScreenShell {
	struct Rect { float x, y, w, h; };

	// Title shown at the top of the LeftBar. Blank = no title strip.
	const char* title = "";

	// Preview — empty previewId means no 3D preview, camera keeps its
	// ambient orbit. Non-empty means the camera pins to the preview pose
	// and the world renderer injects the model. Clip is the named
	// animation from the BoxModel; yaw/pitch are owner-driven (auto-spin
	// is handled in the renderer, these are user-drag offsets).
	std::string previewId;
	std::string previewClip  = "mine";
	float       previewYaw   = 0.0f;
	float       previewPitch = 0.0f;

	// Cover — when true the owning screen is expected to draw a text
	// panel over the preview area. Bars remain visible. Shell doesn't
	// render the cover itself; it just holds the toggle state so camera
	// pin / world injection can keep running underneath regardless.
	bool coverVisible = false;

	// ── Layout (NDC, +Y up) ───────────────────────────────────────────
	// LeftBar flush to the left edge, BottomBar flush to the bottom.
	// Preview fills what's left. Constants kept on the struct so the
	// owning screen can draw its content with the same numbers the
	// chrome uses.
	static constexpr float kLeftX  = -0.98f;
	static constexpr float kLeftW  =  0.42f;
	static constexpr float kLeftY  = -0.78f;
	static constexpr float kLeftH  =  1.76f;   // top = +0.98

	static constexpr float kBotX   = -0.98f;
	static constexpr float kBotW   =  1.96f;
	static constexpr float kBotY   = -0.95f;
	static constexpr float kBotH   =  0.15f;

	static constexpr float kPrevX  = -0.54f;
	static constexpr float kPrevW  =  1.52f;
	static constexpr float kPrevY  = -0.78f;
	static constexpr float kPrevH  =  1.76f;

	Rect leftBar()     const { return { kLeftX, kLeftY, kLeftW, kLeftH }; }
	Rect bottomBar()   const { return { kBotX,  kBotY,  kBotW,  kBotH  }; }
	Rect previewArea() const { return { kPrevX, kPrevY, kPrevW, kPrevH }; }

	// Draws the two translucent bars + LeftBar title strip. Content
	// inside each bar is the caller's responsibility.
	void drawChrome(rhi::IRhi* r) const {
		const float cardFill[4]   = {0.07f, 0.06f, 0.06f, 0.72f};
		const float cardShadow[4] = {0.00f, 0.00f, 0.00f, 0.40f};
		const float brass[4]      = {0.72f, 0.54f, 0.22f, 0.90f};
		const float brassLt[4]    = {0.95f, 0.78f, 0.35f, 0.60f};

		// Left bar
		ui::drawShadowPanel(r, kLeftX, kLeftY, kLeftW, kLeftH,
		                    cardShadow, cardFill, brass, 0.003f);
		ui::drawOutline(r, kLeftX + 0.010f, kLeftY + 0.010f,
		                kLeftW - 0.020f, kLeftH - 0.020f, 0.0012f, brassLt);

		if (title && *title) {
			const float titleH = 0.080f;
			const float titleFill[4] = {0.14f, 0.10f, 0.07f, 0.92f};
			const float titleCol[4]  = {1.00f, 0.86f, 0.45f, 1.00f};
			float ty = kLeftY + kLeftH - titleH - 0.016f;
			r->drawRect2D(kLeftX + 0.018f, ty, kLeftW - 0.036f, titleH, titleFill);
			ui::drawOutline(r, kLeftX + 0.018f, ty, kLeftW - 0.036f, titleH,
			                0.0012f, brassLt);
			// Title scale auto-shrinks so long strings fit the LeftBar width.
			const float avail = kLeftW - 0.060f;
			float scale = 1.10f;
			float w = ui::textWidthNdc(std::strlen(title), scale);
			if (w > avail) scale *= (avail / w);
			ui::drawCenteredTitle(r, title, kLeftX + kLeftW * 0.5f,
			                      ty + 0.022f + (0.080f - 0.032f * scale) * 0.5f,
			                      scale, titleCol);
		}

		// Bottom bar
		ui::drawShadowPanel(r, kBotX, kBotY, kBotW, kBotH,
		                    cardShadow, cardFill, brass, 0.003f);
		ui::drawOutline(r, kBotX + 0.008f, kBotY + 0.008f,
		                kBotW - 0.016f, kBotH - 0.016f, 0.0012f, brassLt);
	}
};

} // namespace civcraft::vk
