#pragma once

// Reusable UI widgets. Every screen should compose from these — never push
// styles inline. See ui_theme.h for the palette and type ramp.

#include <imgui.h>

namespace civcraft::ui {

// ── Buttons ──────────────────────────────────────────────────────────
// w=0 stretches to fill current ContentRegionAvail.

// Filled brass CTA. "PLAY", "SINGLEPLAYER", "CREATE WORLD".
bool PrimaryButton(const char* label, float w = 0.0f, float h = 48.0f);

// Default panel button. "Resume", "Refresh", "Add Server".
bool SecondaryButton(const char* label, float w = 0.0f, float h = 40.0f);

// Red destructive action. "Delete World", "Leave Server".
bool DangerButton(const char* label, float w = 0.0f, float h = 40.0f);

// Low-weight outline button. "Back", "Cancel".
bool GhostButton(const char* label, float w = 0.0f, float h = 36.0f);

// ── Structure ────────────────────────────────────────────────────────
void SectionHeader(const char* text);
void Divider(float thickness = 1.0f);
void VerticalSpace(float px = 8.0f);

// ── Text helpers ─────────────────────────────────────────────────────
// Muted caption line (Roboto 14px, kTextDim).
void Hint(const char* fmt, ...);

// Body text in explicit color (Roboto 18px).
void ColoredText(const ImVec4& col, const char* fmt, ...);

// Two-column row: key on the left in kTextDim, value right-aligned in kText.
// Used by entity inspector and handbook property tables.
void KeyValue(const char* key, const char* fmt, ...);

// ── Meters ───────────────────────────────────────────────────────────
// Phase-labelled progress bar for loading screens and long operations.
// pct ∈ [0,1]. phase = small uppercase tag above the bar ("Generating world").
// detail = hint line below the bar ("chunk 128/512").
void ProgressBar(float pct, const char* phase = nullptr, const char* detail = nullptr);

// HP / stamina / resource meter. Auto-colors by threshold unless overridden.
void Meter(float pct, const ImVec4* colorOverride = nullptr);

}  // namespace civcraft::ui
