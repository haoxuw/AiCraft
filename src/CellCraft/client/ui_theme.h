#pragma once

// CellCraft UI theme — single source of truth for colors across the UI.
//
// Bright pastel kids-game palette (Roblox / Fortnite / Fall Guys energy).
// All UI surfaces, buttons, text, and outlines should reference these
// constants rather than inline hex/vec4s.

#include <glm/glm.hpp>

namespace civcraft::cellcraft::ui {

// ---- Background + cards -----------------------------------------------
inline constexpr glm::vec4 BG_CREAM    {0.97f, 0.94f, 0.88f, 1.00f};
inline constexpr glm::vec4 CARD_FILL   {1.00f, 0.99f, 0.96f, 0.95f};
inline constexpr glm::vec4 CARD_STROKE {0.16f, 0.14f, 0.20f, 1.00f};
inline constexpr glm::vec4 PANEL_TINT  {0.94f, 0.90f, 0.98f, 0.88f}; // lavender-tinted card
inline constexpr glm::vec4 SHADOW      {0.10f, 0.08f, 0.14f, 0.35f};

// ---- Accents / brand --------------------------------------------------
inline constexpr glm::vec4 ACCENT_PINK    {1.00f, 0.31f, 0.55f, 1.00f}; // #FF4F8B
inline constexpr glm::vec4 ACCENT_CYAN    {0.31f, 0.76f, 1.00f, 1.00f}; // #4FC1FF
inline constexpr glm::vec4 ACCENT_LIME    {0.50f, 0.83f, 0.31f, 1.00f}; // #7FD44F
inline constexpr glm::vec4 ACCENT_GOLD    {1.00f, 0.76f, 0.31f, 1.00f}; // #FFC34F
inline constexpr glm::vec4 ACCENT_MAGENTA {0.77f, 0.31f, 1.00f, 1.00f}; // #C44FFF
inline constexpr glm::vec4 ACCENT_ORANGE  {1.00f, 0.56f, 0.31f, 1.00f}; // #FF8F4F

// ---- Typography -------------------------------------------------------
inline constexpr glm::vec4 TEXT_DARK  {0.12f, 0.10f, 0.18f, 1.00f};
inline constexpr glm::vec4 TEXT_LIGHT {1.00f, 0.99f, 0.96f, 1.00f};
inline constexpr glm::vec4 TEXT_MUTED {0.38f, 0.34f, 0.44f, 1.00f};
inline constexpr glm::vec4 OUTLINE    {0.10f, 0.08f, 0.14f, 1.00f};

// ---- Semantic roles ---------------------------------------------------
inline constexpr glm::vec4 BTN_PRIMARY_TOP    {1.00f, 0.43f, 0.62f, 1.00f};
inline constexpr glm::vec4 BTN_PRIMARY_BOTTOM {0.90f, 0.22f, 0.45f, 1.00f};
inline constexpr glm::vec4 BTN_GO_TOP         {0.68f, 0.90f, 0.42f, 1.00f};
inline constexpr glm::vec4 BTN_GO_BOTTOM      {0.42f, 0.72f, 0.24f, 1.00f};
inline constexpr glm::vec4 BTN_NEUTRAL_TOP    {1.00f, 0.99f, 0.96f, 0.98f};
inline constexpr glm::vec4 BTN_NEUTRAL_BOTTOM {0.85f, 0.82f, 0.78f, 0.98f};
inline constexpr glm::vec4 BTN_DANGER_TOP     {1.00f, 0.56f, 0.31f, 1.00f};
inline constexpr glm::vec4 BTN_DANGER_BOTTOM  {0.88f, 0.34f, 0.18f, 1.00f};

// ---- Chalk-stroke creature colors (6 slots) ---------------------------
inline constexpr glm::vec3 STROKE_PALETTE[6] = {
	{1.00f, 0.31f, 0.55f},  // hot pink
	{0.31f, 0.76f, 1.00f},  // cyan
	{0.50f, 0.83f, 0.31f},  // lime
	{1.00f, 0.76f, 0.31f},  // gold
	{0.77f, 0.31f, 1.00f},  // magenta
	{1.00f, 0.56f, 0.31f},  // orange
};

} // namespace civcraft::cellcraft::ui
