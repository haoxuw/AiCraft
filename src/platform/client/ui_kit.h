#pragma once

// Shared NDC draw primitives used by every custom-drawn UI surface
// (inventory, hotbar, menus, pause screen, death screen, overlays).
// Nothing here knows about Game state — all calls take raw IRhi* + data.

#include "client/rhi/rhi.h"

#include <cstddef>
#include <functional>
#include <string>

#include <glm/glm.hpp>

struct GLFWwindow;

namespace solarium::vk::ui {

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

// Shadow + fill + outlined border. Pass nullptr for any of shadow/border
// to skip that layer. Shadow is offset down-right by 0.010 NDC.
void drawShadowPanel(rhi::IRhi* r, float x, float y, float w, float h,
                     const float shadow[4], const float fill[4],
                     const float border[4], float borderT);

// Horizontal progress bar (HP, load, etc.). `frac` clamped to [0,1].
void drawMeter(rhi::IRhi* r, float x, float y, float w, float h,
               float frac, const float fill[4], const float bg[4],
               const float border[4]);

// Horizontally-centered SDF text / title around NDC x=cx.
void drawCenteredText(rhi::IRhi* r, const char* txt, float cx, float y,
                      float scale, const float color[4]);
void drawCenteredTitle(rhi::IRhi* r, const char* txt, float cx, float y,
                       float scale, const float color[4]);

// Unified label drawer used by the panel surfaces (inventory, handbook,
// stats readouts, tooltips). All text routes through drawTitle2D — the
// same SDF mode the in-world floaters use ("+1 wood" pickups) — so the
// panels get the same outlined, bloom-boosted glyphs at any scale.
// `align`: 0 = left at x, 1 = centered around x, 2 = right anchored to x.
enum class TextAlign { Left = 0, Center = 1, Right = 2 };
void writeText(rhi::IRhi* r, const char* txt, float x, float y,
               float scale, const float color[4],
               TextAlign align = TextAlign::Left);

// Overload for std::string callers (most inventory/handbook code
// builds text via snprintf into a buffer or a std::string).
inline void writeText(rhi::IRhi* r, const std::string& txt, float x, float y,
                       float scale, const float color[4],
                       TextAlign align = TextAlign::Left) {
	writeText(r, txt.c_str(), x, y, scale, color, align);
}

// Returns true only on the frame a key transitions up→down. Shared
// state across calls is keyed by GLFW keycode so this survives across
// translation units.
bool keyEdge(GLFWwindow* w, int key);

// ── Peekable text ─────────────────────────────────────────────────────
// Generic "text with clickable (x,y,z) coordinate hyperlinks" primitive.
// Any UI surface (entity inspect, dialog scrollback, notification ticker,
// dev overlay) can pass its goal/message text through this to get
// hover-tinted, underlined, click-dispatched coord links without knowing
// about Game internals — the caller supplies an onCoordClick callback
// that handles the peek side-effect.
//
// Coord format: parenthesised signed ints with comma separators and
// optional spaces, e.g. `(-13, 5, 14)` or `(0,0,0)`. Matched substrings
// are replaced inline with a cyan, underlined span; unmatched text
// renders with baseCol.

struct CoordMatch {
	size_t     begin;   // byte offset of '(' in source string
	size_t     end;     // byte offset just past ')'
	glm::ivec3 coord;
};

// Finds the next "(x, y, z)" substring at or after `from`. Returns true
// and fills `out` on match; false if no coord found through end of `s`.
bool findCoord(const std::string& s, size_t from, CoordMatch& out);

// Per-frame input + callback bundle. Built by the caller (Game or similar)
// once per frame and passed to every drawPeekableText call. Decouples the
// renderer from Game — the callback owns click semantics.
struct PeekableTextInput {
	float mouseNdcX     = 0.0f;
	float mouseNdcY     = 0.0f;
	bool  mouseLPressed = false;
	std::function<void(glm::ivec3)> onCoordClick;
};

// Renders `value` starting at (x, y) with `scale`, tinting coord substrings
// as click-to-peek hyperlinks. Non-coord text uses `baseCol`. Returns the
// NDC width the full string consumed (useful when laying out inline).
// Coord color palette is fixed (cyan + hover brighten) to keep link
// affordance recognisable across surfaces.
float drawPeekableText(rhi::IRhi* r, const PeekableTextInput& in,
                       float x, float y, float scale,
                       const std::string& value, const float baseCol[4]);

// Shared palette. Keep menus / tooltips / overlays consistent.
namespace color {
extern const float kBrass[4];      // title warm (lightest)
extern const float kBrassHi[4];    // panel upper-edge highlight
extern const float kBrassMid[4];   // hud / menu frame border
extern const float kBrassDeep[4];  // panel / inventory border (dark)
extern const float kPanelFill[4];  // dark menu/panel fill (alpha 0.94)
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

} // namespace solarium::vk::ui
