#pragma once

/**
 * inventory_visuals.h — shared drawing helpers for inventory-style UIs.
 *
 * Used by EquipmentUI and ChestUI so both panels stay visually consistent
 * (same cell look, same 3D icons, same section headers, same cube fallback).
 *
 * Pure functions — no state. Callers pass in the model/icon registries and
 * the cube animation time.
 */

#include "shared/block_registry.h"
#include "client/box_model.h"
#include "client/model_icon_cache.h"
#include <imgui.h>
#include <glm/glm.hpp>
#include <cmath>
#include <string>
#include <unordered_map>
#include <functional>

namespace civcraft::inv_vis {

// Resolve a representative color for an item id.
// Priority: first box-model part color → block top color → fallback.
inline glm::vec3 getItemColor(const std::string& id, const BlockRegistry& blocks,
                              const std::unordered_map<std::string, BoxModel>* models) {
	if (models) {
		std::string key = id;
		auto colon = key.find(':');
		if (colon != std::string::npos) key = key.substr(colon + 1);
		auto it = models->find(key);
		if (it != models->end() && !it->second.parts.empty()) {
			auto& c = it->second.parts[0].color;
			return {c.r, c.g, c.b};
		}
	}
	const BlockDef* bdef = blocks.find(id);
	if (bdef) return bdef->color_top;
	return {0.5f, 0.6f, 0.75f};
}

// Rotating isometric cube — the icon fallback when no 3D model is cached.
inline void drawIsoCube(ImDrawList* dl, float cx, float cy, float sz,
                        glm::vec3 c, float time, int slotIdx, float alpha = 1.0f) {
	float angle = time * 0.8f + slotIdx * 0.5f;
	float ca = std::cos(angle), sa = std::sin(angle);
	ImVec2 proj[8];
	float corners[8][3] = {
		{-1,-1,-1},{1,-1,-1},{1,-1,1},{-1,-1,1},
		{-1, 1,-1},{1, 1,-1},{1, 1,1},{-1, 1,1},
	};
	for (int v = 0; v < 8; v++) {
		float rx = corners[v][0]*ca - corners[v][2]*sa;
		float rz = corners[v][0]*sa + corners[v][2]*ca;
		float ry = corners[v][1];
		proj[v] = {cx + (rx - rz) * sz * 0.5f,
		           cy - (rx + rz) * sz * 0.25f - ry * sz * 0.5f};
	}

	int a255 = (int)(alpha * 230);
	auto drawFace = [&](int a, int b, int d, int e, float shade) {
		ImVec2 pts[] = {proj[a], proj[b], proj[d], proj[e]};
		ImU32 col = IM_COL32(
			(int)(c.r*shade*255), (int)(c.g*shade*255), (int)(c.b*shade*255), a255);
		dl->AddConvexPolyFilled(pts, 4, col);
		dl->AddPolyline(pts, 4, IM_COL32(0, 0, 0, (int)(alpha * 60)), true, 1.0f);
	};

	drawFace(7, 6, 5, 4, 1.0f); // top
	float nx_r = ca + sa;
	float nx_f = -sa + ca;
	if (nx_r > 0) drawFace(1, 2, 6, 5, 0.70f);
	else          drawFace(3, 0, 4, 7, 0.70f);
	if (nx_f > 0) drawFace(2, 3, 7, 6, 0.82f);
	else          drawFace(0, 1, 5, 4, 0.82f);
}

// Draw a 3D model icon for an item, or fall back to the isometric cube.
inline void drawItemIcon(ImDrawList* dl, const std::string& id,
                         const BlockRegistry& blocks,
                         const std::unordered_map<std::string, BoxModel>* models,
                         ModelIconCache* icons,
                         float time, float x, float y, float size) {
	if (models && icons) {
		std::string key = id;
		auto colon = key.find(':');
		if (colon != std::string::npos) key = key.substr(colon + 1);
		auto it = models->find(key);
		if (it != models->end()) {
			GLuint tex = icons->getIcon(key, it->second);
			if (tex) {
				dl->AddImage((ImTextureID)(intptr_t)tex,
					{x, y}, {x + size, y + size}, {0, 1}, {1, 0});
				return;
			}
		}
	}
	glm::vec3 color = getItemColor(id, blocks, models);
	drawIsoCube(dl, x + size * 0.5f, y + size * 0.4f, size * 0.34f,
	            color, time, (int)(std::hash<std::string>{}(id) % 20));
}

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
