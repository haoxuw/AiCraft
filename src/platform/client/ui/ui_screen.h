#pragma once

// One modal shape for every full-screen UI: main menu, loading, pause, death,
// world picker, server browser, settings. Centers an ImGui panel over a
// themed scrim; optionally paints a big pixel-font title via the RHI 2D path.
//
// Usage:
//   if (ui::BeginScreen("main_menu", m_rhi, { .panelW = 360, .panelH = 380 })) {
//       ui::ScreenTitle(m_rhi, "CIVCRAFT", 0.55f, 2.8f, m_menuTitleT);
//       if (ui::PrimaryButton("SINGLEPLAYER")) enterSingleplayer();
//       // …
//   }
//   ui::EndScreen();

#include <imgui.h>
#include <cstring>

#include "client/rhi/rhi.h"
#include "client/ui/ui_theme.h"

namespace civcraft::ui {

// Alias so widget signatures can say `Rhi*` without pulling the full
// civcraft::rhi::IRhi spelling through every call site.
using Rhi = ::civcraft::rhi::IRhi;

struct ScreenOpts {
	float panelW     = 360.0f;
	float panelH     = 0.0f;     // 0 → auto-fit to contents
	float anchorY    = 0.52f;    // fraction of viewport height for top-of-panel
	bool  showScrim  = true;
	ImVec4 scrimColor{0.02f, 0.02f, 0.04f, 0.55f};
};

inline bool BeginScreen(const char* id, Rhi* rhi, const ScreenOpts& o = {}) {
	if (o.showScrim && rhi) {
		const float bg[4] = {o.scrimColor.x, o.scrimColor.y, o.scrimColor.z, o.scrimColor.w};
		rhi->drawRect2D(-1.2f, -1.2f, 2.4f, 2.4f, bg);
	}

	ImGuiIO& io = ImGui::GetIO();
	float cx = io.DisplaySize.x * 0.5f;
	float cy = io.DisplaySize.y * o.anchorY;
	ImGui::SetNextWindowPos(ImVec2(cx, cy), ImGuiCond_Always, ImVec2(0.5f, 0.0f));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
	                       | ImGuiWindowFlags_NoResize
	                       | ImGuiWindowFlags_NoMove
	                       | ImGuiWindowFlags_NoCollapse
	                       | ImGuiWindowFlags_NoSavedSettings;
	if (o.panelH > 0.0f) {
		ImGui::SetNextWindowSize(ImVec2(o.panelW, o.panelH), ImGuiCond_Always);
	} else {
		// Width fixed, height auto-fits.
		ImGui::SetNextWindowSize(ImVec2(o.panelW, 0.0f), ImGuiCond_Always);
		flags |= ImGuiWindowFlags_AlwaysAutoResize;
	}
	return ImGui::Begin(id, nullptr, flags);
}

inline void EndScreen() {
	ImGui::End();
}

// Centered pixel-font title via RHI drawTitle2D. kCharW ≈ 0.018 NDC/char at
// scale 1.0 — half-width lookup that matches hand-tuned placements of the
// existing CIVCRAFT / PAUSED / YOU DIED titles. Writes the pulsing brass
// gold derived from `tPhase`.
inline void ScreenTitle(Rhi* rhi, const char* text, float y, float scale, float tPhase) {
	if (!rhi || !text) return;
	float gold[4]; accentGold(tPhase, gold);
	float halfW = (float)std::strlen(text) * 0.019f * scale * 0.5f;
	rhi->drawTitle2D(text, -halfW, y, scale, gold);
}

// Red variant for "YOU DIED". No pulse.
inline void ScreenTitleRed(Rhi* rhi, const char* text, float y, float scale) {
	if (!rhi || !text) return;
	const float red[4] = {0.95f, 0.25f, 0.20f, 1.0f};
	float halfW = (float)std::strlen(text) * 0.019f * scale * 0.5f;
	rhi->drawTitle2D(text, -halfW, y, scale, red);
}

}  // namespace civcraft::ui
