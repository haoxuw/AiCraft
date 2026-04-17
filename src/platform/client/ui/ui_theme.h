#pragma once

// CivCraft UI kit — theme tokens + ImGui style + font loading.
// All screens (menu, loading, pause, handbook, inspector, …) read from here.
// One palette, one type ramp, one geometry scale — no per-screen magic numbers.

#include <imgui.h>

namespace civcraft::ui {

// ── Palette (brass / forge) ──────────────────────────────────────────
inline constexpr ImVec4 kBgDeep     {0.05f, 0.04f, 0.06f, 0.92f};
inline constexpr ImVec4 kBgPanel    {0.11f, 0.09f, 0.08f, 0.94f};
inline constexpr ImVec4 kBgRaised   {0.18f, 0.14f, 0.11f, 1.00f};
inline constexpr ImVec4 kBgHover    {0.32f, 0.22f, 0.14f, 1.00f};
inline constexpr ImVec4 kBgActive   {0.50f, 0.34f, 0.18f, 1.00f};

inline constexpr ImVec4 kAccent     {1.00f, 0.72f, 0.25f, 1.00f};
inline constexpr ImVec4 kAccentDim  {0.65f, 0.48f, 0.20f, 1.00f};
inline constexpr ImVec4 kAccentGlow {1.00f, 0.85f, 0.45f, 1.00f};

inline constexpr ImVec4 kText       {0.94f, 0.90f, 0.82f, 1.00f};
inline constexpr ImVec4 kTextDim    {0.68f, 0.62f, 0.54f, 1.00f};
inline constexpr ImVec4 kTextOff    {0.42f, 0.38f, 0.34f, 1.00f};

inline constexpr ImVec4 kOk         {0.55f, 0.85f, 0.35f, 1.00f};
inline constexpr ImVec4 kWarn       {0.95f, 0.75f, 0.25f, 1.00f};
inline constexpr ImVec4 kBad        {0.92f, 0.35f, 0.25f, 1.00f};

inline constexpr ImVec4 kBorder     {0.35f, 0.25f, 0.12f, 1.00f};

// ── Geometry ─────────────────────────────────────────────────────────
inline constexpr float kRadius   = 6.0f;
inline constexpr float kBorderPx = 1.5f;
inline constexpr float kPadding  = 16.0f;
inline constexpr float kGap      = 12.0f;
inline constexpr float kButtonH  = 48.0f;
inline constexpr float kButtonW  = 320.0f;

// ── Type sizes (pixels, for ImGui fonts) ─────────────────────────────
inline constexpr float kFontHeader = 24.0f;
inline constexpr float kFontBody   = 18.0f;
inline constexpr float kFontMono   = 16.0f;
inline constexpr float kFontSmall  = 14.0f;

// Apply palette + rounding + spacing to an ImGui style. Call once after
// ImGui::CreateContext, before the first frame.
void applyTheme(ImGuiStyle& style);

// Load Roboto-Medium at the four sizes above. Call after CreateContext
// but before the Vulkan backend init. Falls back to ImGui default if the
// TTF can't be located.
void loadFonts(ImGuiIO& io);

// Font handles. Null if fonts failed to load; callers should guard with
// `if (auto* f = header()) ImGui::PushFont(f);`.
ImFont* header();
ImFont* body();
ImFont* mono();
ImFont* small_();

// Pulsing brass gold used by title text (CIVCRAFT / PAUSED / YOU DIED).
// Pass your menu-visible timer as phase.
void accentGold(float phase, float out[4]);

}  // namespace civcraft::ui
