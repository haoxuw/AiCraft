#pragma once

// CellCraft — modern UI design system (Commit 1).
//
// Sleek charcoal "app chrome" framing the cream chalk canvas. The chalk
// playfield (cells, food, board) keeps its existing aesthetic; this module
// adds NEW primitives that future commits will use to reskin menus, lab,
// HUD, and end screens.
//
// All primitives take pixel-space coordinates (origin top-left, y-down) and
// translate internally to the platform TextRenderer's NDC pipeline. Call
// `beginFrame(text, screen_w_px, screen_h_px)` once per frame before drawing.
//
// The chalk primitives in ui_theme.h / ui_button.h / ui_text.h remain
// untouched — both systems coexist while we migrate call sites in later
// commits.

#include "client/text.h"
#include <glm/glm.hpp>
#include <string>

namespace civcraft::cellcraft { class SdfFont; }

namespace civcraft::cellcraft::ui::modern {

// ---------------------------------------------------------------- Tokens
// Surfaces -------------------------------------------------------------
inline constexpr glm::vec4 SURFACE_BG_TOP    {0.078f, 0.086f, 0.110f, 1.00f}; // #14161C
inline constexpr glm::vec4 SURFACE_BG_BOTTOM {0.106f, 0.122f, 0.157f, 1.00f}; // #1B1F28
inline constexpr glm::vec4 SURFACE_PANEL     {0.078f, 0.086f, 0.110f, 0.78f};
inline constexpr glm::vec4 SURFACE_PANEL_HI  {0.118f, 0.133f, 0.173f, 0.78f};
inline constexpr glm::vec4 STROKE_SUBTLE     {1.0f, 1.0f, 1.0f, 0.08f};
inline constexpr glm::vec4 STROKE_STRONG     {1.0f, 1.0f, 1.0f, 0.16f};
inline constexpr glm::vec4 TRACK_BG          {0.169f, 0.188f, 0.227f, 1.00f}; // #2B303A

// Accents --------------------------------------------------------------
inline constexpr glm::vec4 ACCENT_CYAN       {0.133f, 0.827f, 0.933f, 1.00f}; // #22D3EE
inline constexpr glm::vec4 ACCENT_CYAN_GLOW  {0.404f, 0.910f, 0.976f, 1.00f}; // #67E8F9
inline constexpr glm::vec4 ACCENT_CYAN_DEEP  {0.031f, 0.569f, 0.698f, 1.00f}; // #0891B2
inline constexpr glm::vec4 ACCENT_AMBER      {0.961f, 0.620f, 0.043f, 1.00f}; // #F59E0B
inline constexpr glm::vec4 ACCENT_AMBER_DEEP {0.706f, 0.325f, 0.035f, 1.00f}; // #B45309
inline constexpr glm::vec4 ACCENT_DANGER     {0.937f, 0.267f, 0.267f, 1.00f}; // #EF4444
inline constexpr glm::vec4 ACCENT_SUCCESS    {0.133f, 0.773f, 0.369f, 1.00f}; // #22C55E

// Text -----------------------------------------------------------------
inline constexpr glm::vec4 TEXT_PRIMARY      {0.945f, 0.961f, 0.976f, 1.00f}; // #F1F5F9
inline constexpr glm::vec4 TEXT_SECONDARY    {0.580f, 0.639f, 0.722f, 1.00f}; // #94A3B8
inline constexpr glm::vec4 TEXT_MUTED        {0.278f, 0.333f, 0.412f, 1.00f}; // #475569
inline constexpr glm::vec4 TEXT_ON_ACCENT    {0.043f, 0.071f, 0.125f, 1.00f}; // #0B1220

// Spacing scale (pixels) ----------------------------------------------
inline constexpr int SPACE_XS  = 4;
inline constexpr int SPACE_SM  = 8;
inline constexpr int SPACE_MD  = 12;
inline constexpr int SPACE_LG  = 16;
inline constexpr int SPACE_XL  = 24;
inline constexpr int SPACE_2XL = 32;
inline constexpr int SPACE_3XL = 48;
inline constexpr int SPACE_4XL = 64;

// Radii (pixels) ------------------------------------------------------
inline constexpr int RADIUS_SM   = 4;
inline constexpr int RADIUS_MD   = 8;
inline constexpr int RADIUS_LG   = 12;
inline constexpr int RADIUS_XL   = 20;
inline constexpr int RADIUS_PILL = 999;

// Type sizes (pixels) -------------------------------------------------
inline constexpr int TYPE_CAPTION  = 11;
inline constexpr int TYPE_BODY     = 14;
inline constexpr int TYPE_LABEL    = 12;
inline constexpr int TYPE_TITLE_SM = 20;
inline constexpr int TYPE_TITLE_MD = 28;
inline constexpr int TYPE_TITLE_LG = 40;
inline constexpr int TYPE_DISPLAY  = 72;

enum class Align { LEFT, CENTER, RIGHT };

// ----------------------------------------------------------------- Fonts
// Call once at app init (after GL context + shader dir resolved). Loads
// Inter Regular/Bold and Audiowide-Regular from `fonts/` (staged next to
// the binary). Returns true if all three loaded; false still leaves the
// UI usable (falls back to the platform bitmap renderer).
bool initFonts(const std::string& fontDir, const std::string& shaderDir);
void shutdownFonts();

// Text role — drives font family, weight, and default glow.
enum class Role {
	BODY,        // Inter-Regular, no glow
	LABEL,       // Inter-Bold uppercase tracked, no glow
	TITLE_SM,    // Inter-Bold
	TITLE_MD,    // Inter-Bold
	TITLE_LG,    // Inter-Bold, subtle glow
	DISPLAY,     // Audiowide, subtle glow
	HERO_NEON,   // Audiowide, strong arcade glow (red/amber caller-chosen)
};

// ----------------------------------------------------------------- Frame
// Call once per frame before any modern primitives. Stashes the renderer
// + viewport so primitives can compute pixel→NDC.
void beginFrame(::civcraft::TextRenderer* text, int screen_w_px, int screen_h_px);

// ------------------------------------------------------------- Surfaces
// Vertical gradient fill (top → bottom). Used for page backgrounds.
void drawScrim(int x, int y, int w, int h,
               const glm::vec4& top_rgba, const glm::vec4& bottom_rgba);

// Filled rounded rectangle with optional stroke. radius in px.
// stroke_px=0 or stroke_rgba.a=0 → no stroke.
void drawRoundedRect(int x, int y, int w, int h, int radius,
                     const glm::vec4& fill_rgba,
                     const glm::vec4& stroke_rgba = glm::vec4(0.0f),
                     int stroke_px = 0);

// Frosted-glass panel: dark fill + top highlight gradient + inner stroke
// + 1px top "lip" highlight.
void drawGlassPanel(int x, int y, int w, int h, int radius);

// Faked drop shadow — concentric rounded rects of decreasing alpha.
// Call BEFORE the panel so the shadow lays underneath.
void drawSoftShadow(int x, int y, int w, int h, int radius,
                    int spread = 24, float alpha = 0.35f);

// Inner glow for hovered surfaces — rect strokes inset by 1px each step.
void drawInnerGlow(int x, int y, int w, int h, int radius,
                   const glm::vec4& rgba, int spread = 8);

// -------------------------------------------------------------- Buttons
// Each returns true on a left-click while hovered. Caller owns the
// hovered/pressed state (read from your own mouse code).
bool buttonPrimary(int x, int y, int w, int h, const std::string& label,
                   bool hovered, bool pressed);
bool buttonGhost  (int x, int y, int w, int h, const std::string& label,
                   bool hovered, bool pressed);
// Square 40x40 glass button with a single-glyph icon in the middle.
bool buttonIcon   (int x, int y, int size, const std::string& icon,
                   const std::string& tooltip, bool hovered, bool pressed);

// ----------------------------------------------------------------- Text
// Plain sans label — no halo, no shadow. Pixel-space x/y is the baseline
// origin (LEFT align) or center/right edge per `align`.
void drawTextModern(int x, int y, const std::string& text, int size_px,
                    const glm::vec4& rgba, Align align = Align::LEFT);

// TYPE_LABEL, uppercased + tracked. For "SPEED", "FULLNESS", "TIER", etc.
void drawTextLabel (int x, int y, const std::string& text,
                    const glm::vec4& rgba);

// TYPE_DISPLAY heading.
void drawTextDisplay(int x, int y, const std::string& text,
                     const glm::vec4& rgba, Align align = Align::LEFT);

// Measure rendered width in pixels for a given size — uses the real SDF
// font metrics when loaded, falling back to the 8×8 cell metric × scale.
int  measureTextPx(const std::string& text, int size_px);

// Role-based variants. If SDF fonts failed to load, these fall back to
// drawTextModern / measureTextPx.
int  measureTextRole(const std::string& text, Role role);
void drawTextRole(int x, int y, const std::string& text, Role role,
                  const glm::vec4& rgba, Align align = Align::LEFT,
                  // Optional arcade-neon halo; ignored for BODY/LABEL.
                  const glm::vec4& glow_override = glm::vec4(0.0f));

// ------------------------------------------------------------- Stat bar
// Row layout: small uppercase LABEL (left), numeric (right), 6px rounded
// track underneath with a glow fill up to value_0_1 * w. ~32px tall.
void drawStatBar(int x, int y, int w,
                 const std::string& label, float value_0_1,
                 const std::string& numeric,
                 const glm::vec4& color = ACCENT_CYAN);

// ----------------------------------------------------------- Pill badge
// Auto-sized rounded pill. If accent_rgba.a > 0, a 2px underline is drawn
// just inside the bottom edge in that color.
void drawPillBadge(int x, int y, const std::string& text,
                   const glm::vec4& fg_rgba, const glm::vec4& bg_rgba,
                   const glm::vec4& accent_rgba = glm::vec4(0.0f));

// -------------------------------------------------------- Ring progress
// Circular progress ring. cx/cy/radius/thickness in pixels. Fill arc
// starts at -90 degrees (12 o'clock) and sweeps clockwise.
void drawRingProgress(int cx, int cy, int radius, int thickness,
                      float value_0_1,
                      const glm::vec4& fg_rgba,
                      const glm::vec4& bg_rgba = TRACK_BG);

// ------------------------------------------------------------- Divider
enum class DividerAxis { HORIZONTAL, VERTICAL };
void drawDivider(int x, int y, int length, DividerAxis axis,
                 const glm::vec4& rgba = STROKE_SUBTLE);

} // namespace civcraft::cellcraft::ui::modern
