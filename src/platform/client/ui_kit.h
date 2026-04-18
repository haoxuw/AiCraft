#pragma once

// Shared NDC draw primitives used by every custom-drawn UI surface
// (inventory, hotbar, menus, pause screen, death screen, overlays).
// Nothing here knows about Game state — all calls take raw IRhi* + data.

#include "client/rhi/rhi.h"

#include <cstddef>

struct GLFWwindow;

namespace civcraft::vk::ui {

// Character cell metrics — match rhi_ui.cpp (kCharWNdc, kCharHNdc).
constexpr float kCharWNdc = 0.018f;
constexpr float kCharHNdc = 0.032f;

// NDC width of a string at the given text scale. Use this for
// title-style centering where we don't need per-aspect correction.
inline float textWidthNdc(size_t len, float scale) {
	return (float)len * kCharWNdc * scale;
}

// Aspect-corrected char width (matches the inventory panel's
// "0.013 * scale / aspect" pattern — for SDF text laid out over
// absolute screen width rather than fixed NDC width).
inline float textCharWAspect(float scale, float aspect) {
	return 0.013f * scale / aspect;
}

// Point-in-rect hit-test in NDC (+Y up).
inline bool rectContainsNdc(float x, float y, float w, float h,
                            float px, float py) {
	return px >= x && px <= x + w && py >= y && py <= y + h;
}

// Draw a 4-thin-edge outline.
void drawOutline(rhi::IRhi* r, float x, float y, float w, float h,
                 float thickness, const float color[4]);

// Horizontally-centered SDF text / title around NDC x=cx.
void drawCenteredText(rhi::IRhi* r, const char* txt, float cx, float y,
                      float scale, const float color[4]);
void drawCenteredTitle(rhi::IRhi* r, const char* txt, float cx, float y,
                       float scale, const float color[4]);

// Returns true only on the frame a key transitions up→down. Shared
// state across calls is keyed by GLFW keycode so this survives across
// translation units.
bool keyEdge(GLFWwindow* w, int key);

// Shared palette. Keep menus / tooltips / overlays consistent.
namespace color {
extern const float kBrass[4];      // title warm
extern const float kRed[4];        // danger title
extern const float kText[4];       // bright body
extern const float kTextDim[4];    // secondary label
extern const float kTextHint[4];   // hint / tooltip
extern const float kDanger[4];     // error body
extern const float kScrim[4];      // menu scrim (0.55 a)
extern const float kScrimDark[4];  // death scrim (0.68 a)
extern const float kRowBg[4];      // unselected list row
extern const float kSelBg[4];      // selected list row
}

} // namespace civcraft::vk::ui
