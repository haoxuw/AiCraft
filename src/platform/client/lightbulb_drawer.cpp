#include "client/lightbulb_drawer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace civcraft {

namespace {
// ── Palette ───────────────────────────────────────────────────────────────
// The title SDF shader adds a dark outline + soft glow regardless of tint,
// so these base colors stay high-contrast on bright-sky and dusk backdrops
// without per-scene tweaking.
constexpr glm::vec4 kHealthyTint = {1.00f, 0.88f, 0.30f, 1.0f}; // warm gold
constexpr glm::vec4 kErrorTint   = {1.00f, 0.30f, 0.25f, 1.0f}; // muted red

// ── Geometry ──────────────────────────────────────────────────────────────
// Glyph cell in NDC at scale 1.0 — matches the tessellation constants in
// rhi_ui.cpp so layout math stays in sync with the shader.
constexpr float kCharWNdc = 0.018f;
constexpr float kCharHNdc = 0.032f;

// World-space lift above the NPC's collision top. The entire glyph+label
// stack rides this anchor, so the indicator follows the creature's head.
constexpr float kAnchorLiftY = 0.30f;

// Vertical gap between the "!" and the label baseline, in NDC.
constexpr float kGlyphLabelGap = 0.018f;

// ── Animation ─────────────────────────────────────────────────────────────
// World-space bob: low-frequency, small amplitude, per-entity phase so a
// crowd doesn't bob in lockstep.
constexpr float kBobFreq = 2.0f;
constexpr float kBobAmp  = 0.05f;

// Scale-only pulse on the "!". Alpha/color stay constant — strobing
// saturation in peripheral vision is fatiguing; a breathing size change
// reads as "alive" without flicker.
constexpr float kGlyphBaseScale = 2.10f;
constexpr float kGlyphPulseAmp  = 0.10f;
constexpr float kGlyphPulseFreq = 3.2f;

// ── Label sizing ──────────────────────────────────────────────────────────
// Default scale is intentionally below 1.0 so the label visually subordinates
// to the "!" — icon primary, text supporting. Long goals scale down further
// but never below kLabelMinScale (legibility floor).
constexpr float kLabelIdleScale = 0.95f;
constexpr float kLabelMaxWidth  = 0.50f;
constexpr float kLabelMinScale  = 0.55f;
} // namespace

LightbulbDrawer::LightbulbDrawer(rhi::IRhi& rhi) : m_rhi(rhi) {}

void LightbulbDrawer::draw(const Entity& e, const glm::mat4& viewProj,
                           float globalTime, float cameraLookYaw, float aspect) {
	(void)cameraLookYaw;  // NDC-space indicator; no billboard rotation.
	(void)aspect;         // drawText2D / drawTitle2D are aspect-agnostic.

	if (e.goalText.empty()) {
		throw std::runtime_error(
			"LightbulbDrawer::draw: entity " + std::to_string(e.id()) +
			" (" + e.typeId() + ") has empty goalText — "
			"every NPC must expose a goal before its lightbulb can be drawn");
	}

	// ── Anchor: world point above the NPC's head, bobbing gently. ──────────
	float entityTop = e.def().collision_box_max.y;
	float bobWorld  = std::sin(globalTime * kBobFreq + e.id() * 0.7f) * kBobAmp;
	glm::vec3 anchor = e.position
	                   + glm::vec3(0, entityTop + kAnchorLiftY + bobWorld, 0);

	glm::vec4 clip = viewProj * glm::vec4(anchor, 1.0f);
	if (clip.w <= 0.01f) return;                                // behind camera
	float ndcX = clip.x / clip.w;
	float ndcY = clip.y / clip.w;
	if (ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f) return;

	glm::vec4 tint = e.hasError ? kErrorTint : kHealthyTint;

	// ── "!" glyph (title mode = SDF fill + dark outline + soft glow). ──────
	float pulse = 1.0f
	              + std::sin(globalTime * kGlyphPulseFreq + e.id() * 0.9f)
	                * kGlyphPulseAmp;
	float glyphScale = kGlyphBaseScale * pulse;
	float glyphW     = kCharWNdc * glyphScale;
	float glyphH     = kCharHNdc * glyphScale;
	float glyphX     = ndcX - glyphW * 0.5f; // center the single-char glyph
	m_rhi.drawTitle2D("!", glyphX, ndcY, glyphScale, &tint.x);

	// ── Label: plain SDF text above the "!". ───────────────────────────────
	float rawWidth = e.goalText.size() * kCharWNdc;
	float labelScale = rawWidth > kLabelMaxWidth
	                       ? std::max(kLabelMinScale, kLabelMaxWidth / rawWidth)
	                       : kLabelIdleScale;
	float labelW = rawWidth * labelScale;
	float labelX = ndcX - labelW * 0.5f;
	float labelY = ndcY + glyphH + kGlyphLabelGap;
	m_rhi.drawText2D(e.goalText.c_str(), labelX, labelY, labelScale, &tint.x);
}

} // namespace civcraft
