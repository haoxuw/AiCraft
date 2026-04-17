#pragma once

// Shared drawing helpers for inventory-style UIs (EquipmentUI, ChestUI).
// Pure functions — no state.
//
// The item-icon helpers (iso-cube fallback) have been removed. Items are now
// rendered using the same box-model pipeline as world/ground items; that
// render path will be integrated into EquipmentUI/ChestUI next.

#include <imgui.h>

namespace civcraft::inv_vis {

// Gold label + divider line — matches the section headers in EquipmentUI.
inline void drawSectionHeader(ImDrawList* dl, const char* label) {
	ImVec2 pos = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x;
	dl->AddText(ImGui::GetFont(), 15.0f, {pos.x + 4, pos.y},
		IM_COL32(180, 150, 70, 220), label);
	dl->AddLine({pos.x, pos.y + 18}, {pos.x + w, pos.y + 18},
		IM_COL32(70, 56, 30, 120));
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 24);
}

} // namespace civcraft::inv_vis
