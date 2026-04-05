#pragma once

/**
 * Equipment & Inventory UI — Diablo-style dark panel with 3D item previews.
 *
 * Opened with [I] key. Features:
 *   - Left: character equipment slots (5 paper-doll slots)
 *   - Right: scrollable item grid with rotating isometric 3D models
 *   - Drag-and-drop between grid and equipment slots
 *   - Hover tooltip with item name + count
 *   - Dark fantasy theme (charcoal, gold accents)
 */

#include "shared/inventory.h"
#include "shared/block_registry.h"
#include "shared/box_model.h"
#include "client/model_icon_cache.h"
#include <imgui.h>
#include <string>
#include <cmath>
#include <unordered_map>

namespace agentica {

class EquipmentUI {
public:
	bool isOpen() const { return m_open; }
	void toggle() { m_open = !m_open; m_dragItem.clear(); }
	void close() { m_open = false; m_dragItem.clear(); }
	void setModels(const std::unordered_map<std::string, BoxModel>* models, ModelIconCache* icons) {
		m_models = models;
		m_iconCache = icons;
	}

	void render(Inventory& inventory, const BlockRegistry& blocks, float W, float H) {
		if (!m_open) return;

		m_time += 1.0f / 60.0f; // approximate dt

		// Clear drag when mouse released
		if (!ImGui::IsMouseDown(0)) m_dragItem.clear();

		float panelW = std::min(W * 0.85f, 920.0f);
		float panelH = std::min(H * 0.82f, 620.0f);
		ImGui::SetNextWindowPos(ImVec2((W - panelW) / 2, (H - panelH) / 2));
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH));

		// Dark Diablo-style theme
		ImGui::PushStyleColor(ImGuiCol_WindowBg,    ImVec4(0.08f, 0.07f, 0.06f, 0.96f));
		ImGui::PushStyleColor(ImGuiCol_Border,       ImVec4(0.35f, 0.28f, 0.15f, 0.80f));
		ImGui::PushStyleColor(ImGuiCol_TitleBg,      ImVec4(0.10f, 0.08f, 0.06f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.14f, 0.11f, 0.08f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg,      ImVec4(0.06f, 0.05f, 0.04f, 0.90f));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,  ImVec4(0.06f, 0.05f, 0.04f, 0.50f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

		if (!ImGui::Begin("##Inventory", &m_open,
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
			ImGui::End();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(6);
			return;
		}

		// Title bar
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 winPos = ImGui::GetWindowPos();
		ImVec2 winSize = ImGui::GetWindowSize();
		dl->AddRectFilled(winPos, {winPos.x + winSize.x, winPos.y + 36},
			IM_COL32(14, 12, 8, 240));
		dl->AddLine({winPos.x, winPos.y + 36}, {winPos.x + winSize.x, winPos.y + 36},
			IM_COL32(90, 72, 35, 200));
		// Title text
		dl->AddText(ImGui::GetFont(), 20.0f, {winPos.x + 14, winPos.y + 8},
			IM_COL32(210, 175, 80, 255), "INVENTORY");
		ImGui::SetCursorPosY(42);

		// ── Left panel: Equipment (paper doll) ──
		float leftW = 240;
		ImGui::BeginChild("##Equip", ImVec2(leftW, 0), false);
		{
			ImGui::Spacing();
			drawSectionHeader(dl, "EQUIPMENT");
			ImGui::Spacing();

			renderEquipSlot(dl, inventory, blocks, WearSlot::Helmet,    "Helmet",     "\xF0\x9F\xAA\x96");
			ImGui::Spacing();

			// Offhand (shield, torch — left hand; right hand = hotbar item)
			renderEquipSlot(dl, inventory, blocks, WearSlot::Offhand, "Offhand", nullptr);
			ImGui::Spacing();

			renderEquipSlot(dl, inventory, blocks, WearSlot::Body,      "Body",       nullptr);
			ImGui::Spacing();
			renderEquipSlot(dl, inventory, blocks, WearSlot::Back,      "Back",       nullptr);
		}
		ImGui::EndChild();

		ImGui::SameLine(0, 4);

		// Vertical divider
		ImVec2 divTop = ImGui::GetCursorScreenPos();
		divTop.x -= 2;
		dl->AddLine(divTop, {divTop.x, divTop.y + ImGui::GetContentRegionAvail().y},
			IM_COL32(70, 56, 30, 140));

		ImGui::SameLine(0, 4);

		// ── Right panel: Item grid ──
		ImGui::BeginChild("##Items", ImVec2(0, 0), false);
		{
			ImGui::Spacing();
			drawSectionHeader(dl, "ITEMS");
			ImGui::Spacing();

			auto items = inventory.items();

			// Grid layout
			float cellSize = 72;
			float gap = 6;
			float contentW = ImGui::GetContentRegionAvail().x;
			int cols = std::max(1, (int)((contentW + gap) / (cellSize + gap)));

			if (items.empty()) {
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 40);
				ImGui::TextColored(ImVec4(0.40f, 0.38f, 0.35f, 1), "  No items.");
			}

			int col = 0;
			for (auto& [id, count] : items) {
				if (col > 0) ImGui::SameLine(0, gap);

				ImGui::PushID(id.c_str());
				renderItemCell(dl, inventory, blocks, id, count, cellSize);
				ImGui::PopID();

				col++;
				if (col >= cols) col = 0;
			}
		}
		ImGui::EndChild();

		// Draw dragged item under cursor
		if (!m_dragItem.empty()) {
			ImVec2 mouse = ImGui::GetMousePos();
			ImDrawList* fg = ImGui::GetForegroundDrawList();
			float sz = 50;
			drawItemIcon(fg, m_dragItem, blocks, mouse.x - sz * 0.5f, mouse.y - sz * 0.5f, sz);
			// Shadow
			fg->AddEllipseFilled({mouse.x, mouse.y + sz * 0.32f}, {sz * 0.24f, sz * 0.08f},
				IM_COL32(0, 0, 0, 60));
		}

		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(6);
	}

private:
	bool m_open = false;
	float m_time = 0;
	std::string m_dragItem;     // item being dragged
	std::string m_contextItem;  // for context menu
	const std::unordered_map<std::string, BoxModel>* m_models = nullptr;
	ModelIconCache* m_iconCache = nullptr;

	glm::vec3 getItemColor(const std::string& id, const BlockRegistry& blocks) const {
		if (m_models) {
			std::string key = id;
			auto colon = key.find(':');
			if (colon != std::string::npos) key = key.substr(colon + 1);
			auto it = m_models->find(key);
			if (it != m_models->end() && !it->second.parts.empty()) {
				auto& c = it->second.parts[0].color;
				return {c.r, c.g, c.b};
			}
		}
		const BlockDef* bdef = blocks.find(id);
		if (bdef) return bdef->color_top;
		return {0.5f, 0.6f, 0.75f};
	}

	// Draw a 3D model icon for an item, or fall back to isometric cube
	void drawItemIcon(ImDrawList* dl, const std::string& id, const BlockRegistry& blocks,
	                   float x, float y, float size) {
		// Try 3D model icon
		if (m_models && m_iconCache) {
			std::string key = id;
			auto colon = key.find(':');
			if (colon != std::string::npos) key = key.substr(colon + 1);
			auto it = m_models->find(key);
			if (it != m_models->end()) {
				GLuint tex = m_iconCache->getIcon(key, it->second);
				if (tex) {
					dl->AddImage((ImTextureID)(intptr_t)tex,
						{x, y}, {x + size, y + size}, {0, 1}, {1, 0});
					return;
				}
			}
		}
		// Fallback: isometric cube with item color
		glm::vec3 color = getItemColor(id, blocks);
		drawIsoCube(dl, x + size * 0.5f, y + size * 0.4f, size * 0.34f,
		            color, m_time, (int)(std::hash<std::string>{}(id) % 20));
	}

	// ── Draw section header with gold line ──
	void drawSectionHeader(ImDrawList* dl, const char* label) {
		ImVec2 pos = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		dl->AddText(ImGui::GetFont(), 15.0f, {pos.x + 4, pos.y},
			IM_COL32(180, 150, 70, 220), label);
		dl->AddLine({pos.x, pos.y + 18}, {pos.x + w, pos.y + 18},
			IM_COL32(70, 56, 30, 120));
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 24);
	}

	// ── Isometric rotating cube (same technique as hotbar) ──
	void drawIsoCube(ImDrawList* dl, float cx, float cy, float sz,
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

	// ── Render a single item cell in the grid ──
	void renderItemCell(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks,
	                     const std::string& id, int count, float cellSize) {
		const BlockDef* bdef = blocks.find(id);
		glm::vec3 color = bdef ? bdef->color_top : glm::vec3(0.5f, 0.6f, 0.75f);

		ImVec2 pos = ImGui::GetCursorScreenPos();

		// Cell background
		ImU32 cellBg = IM_COL32(18, 16, 12, 220);
		ImU32 cellBorder = IM_COL32(55, 45, 28, 150);
		dl->AddRectFilled(pos, {pos.x + cellSize, pos.y + cellSize}, cellBg, 4.0f);
		dl->AddRect(pos, {pos.x + cellSize, pos.y + cellSize}, cellBorder, 4.0f);

		// Invisible button for interactions
		bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cellSize, cellSize));
		bool hovered = ImGui::IsItemHovered();
		bool active  = ImGui::IsItemActive();

		// Hover glow
		if (hovered) {
			dl->AddRect(pos, {pos.x + cellSize, pos.y + cellSize},
				IM_COL32(200, 165, 55, 160), 4.0f, 0, 2.0f);
		}

		// 3D model icon (or isometric cube fallback)
		float iconPad = cellSize * 0.08f;
		drawItemIcon(dl, id, blocks, pos.x + iconPad, pos.y + iconPad, cellSize - iconPad * 2);

		// Item name (bottom, truncated)
		std::string name = id;
		if (name.size() > 5 && name.substr(0, 5) == "base:") name = name.substr(5);
		if (name.size() > 8) name = name.substr(0, 7) + "~";
		ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
		float textX = pos.x + (cellSize - textSize.x) * 0.5f;
		dl->AddText({textX, pos.y + cellSize - 16},
			IM_COL32(170, 160, 140, 200), name.c_str());

		// Count badge (top-right)
		if (count > 1) {
			char badge[16];
			snprintf(badge, sizeof(badge), "%d", count);
			ImVec2 badgeSize = ImGui::CalcTextSize(badge);
			float bx = pos.x + cellSize - badgeSize.x - 6;
			float by = pos.y + 3;
			dl->AddRectFilled({bx - 3, by - 1}, {bx + badgeSize.x + 3, by + badgeSize.y + 1},
				IM_COL32(20, 18, 14, 220), 3.0f);
			dl->AddText({bx, by}, IM_COL32(220, 200, 120, 255), badge);
		}

		// ── Drag source ──
		if (active && ImGui::IsMouseDragging(0, 3.0f)) {
			m_dragItem = id;
		}
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
			ImGui::SetDragDropPayload("ITEM", id.c_str(), id.size() + 1);
			// Custom preview drawn in render() as dragged item
			m_dragItem = id;
			ImGui::EndDragDropSource();
		}

		// ── Drop target (swap items in inventory — no-op for counter model) ──
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ITEM")) {
				// Dropped an item onto another item cell — no action needed for counter-based inventory
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("EQUIP")) {
				// Dragged from equip slot → unequip
				int slotIdx = *(const int*)payload->Data;
				inv.unequip((WearSlot)slotIdx);
			}
			ImGui::EndDragDropTarget();
		}

		// ── Right-click context menu ──
		if (clicked && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			m_dragItem.clear();
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			m_contextItem = id;
			ImGui::OpenPopup("ItemCtx");
		}

		if (ImGui::BeginPopup("ItemCtx")) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.10f, 0.08f, 0.06f, 0.96f));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.82f, 0.68f, 1.0f));

			std::string dispName = m_contextItem;
			if (dispName.size() > 5 && dispName.substr(0, 5) == "base:")
				dispName = dispName.substr(5);
			ImGui::TextColored(ImVec4(0.90f, 0.78f, 0.35f, 1), "%s", dispName.c_str());
			ImGui::Separator();

			if (ImGui::MenuItem("Equip Left Hand"))
				inv.equip(WearSlot::Offhand, m_contextItem);
			if (ImGui::MenuItem("Equip Right Hand"))
				inv.equip(WearSlot::RightHand, m_contextItem);
			if (ImGui::MenuItem("Equip Helmet"))
				inv.equip(WearSlot::Helmet, m_contextItem);
			if (ImGui::MenuItem("Equip Body"))
				inv.equip(WearSlot::Body, m_contextItem);
			if (ImGui::MenuItem("Equip Back"))
				inv.equip(WearSlot::Back, m_contextItem);
			ImGui::Separator();
			if (ImGui::MenuItem("Drop 1"))
				inv.remove(m_contextItem, 1);
			if (count >= 5 && ImGui::MenuItem("Drop 5"))
				inv.remove(m_contextItem, 5);

			ImGui::PopStyleColor(2);
			ImGui::EndPopup();
		}

		// ── Hover tooltip ──
		if (hovered && m_dragItem.empty()) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.07f, 0.05f, 0.95f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
			ImGui::BeginTooltip();

			std::string fullName = id;
			if (fullName.size() > 5 && fullName.substr(0, 5) == "base:")
				fullName = fullName.substr(5);
			// Capitalize first letter
			if (!fullName.empty()) fullName[0] = (char)toupper(fullName[0]);
			ImGui::TextColored(ImVec4(0.92f, 0.80f, 0.38f, 1), "%s", fullName.c_str());

			if (count > 1) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.55f, 0.52f, 0.45f, 1), "x%d", count);
			}
			ImGui::TextColored(ImVec4(0.45f, 0.43f, 0.38f, 1), "%s", id.c_str());
			ImGui::TextColored(ImVec4(0.50f, 0.48f, 0.42f, 0.8f), "Right-click for options");

			ImGui::EndTooltip();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}
	}

	// ── Render equipment slot (full width) ──
	void renderEquipSlot(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks,
	                      WearSlot slot, const char* label, const char* icon) {
		float w = ImGui::GetContentRegionAvail().x - 4;
		renderEquipSlotSized(dl, inv, blocks, slot, label, w);
	}

	// ── Render equipment slot (specified width) ──
	void renderEquipSlotSized(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks,
	                           WearSlot slot, const char* label, float slotW) {
		auto& itemId = inv.equipped(slot);
		bool hasItem = !itemId.empty();
		float slotH = 68;

		ImGui::PushID((int)slot);

		ImVec2 pos = ImGui::GetCursorScreenPos();

		// Slot background
		ImU32 bg = hasItem ? IM_COL32(22, 20, 16, 230) : IM_COL32(14, 12, 10, 200);
		ImU32 border = hasItem ? IM_COL32(120, 100, 45, 180) : IM_COL32(45, 38, 25, 140);
		dl->AddRectFilled(pos, {pos.x + slotW, pos.y + slotH}, bg, 5.0f);
		dl->AddRect(pos, {pos.x + slotW, pos.y + slotH}, border, 5.0f);

		// Invisible button
		if (ImGui::InvisibleButton("##equip", ImVec2(slotW, slotH))) {
			if (hasItem) inv.unequip(slot);
		}
		bool hovered = ImGui::IsItemHovered();

		// Hover highlight
		if (hovered) {
			dl->AddRect(pos, {pos.x + slotW, pos.y + slotH},
				IM_COL32(200, 165, 55, 140), 5.0f, 0, 2.0f);
		}

		if (hasItem) {
			// 3D item preview
			drawItemIcon(dl, itemId, blocks, pos.x + 6, pos.y + 6, slotH - 12);

			// Item name
			std::string name = itemId;
			if (name.size() > 5 && name.substr(0, 5) == "base:") name = name.substr(5);
			if (!name.empty()) name[0] = (char)toupper(name[0]);
			dl->AddText(ImGui::GetFont(), 14.0f, {pos.x + 52, pos.y + 12},
				IM_COL32(200, 185, 140, 255), name.c_str());

			// Slot label
			dl->AddText(ImGui::GetFont(), 12.0f, {pos.x + 52, pos.y + 30},
				IM_COL32(100, 90, 65, 180), label);

			// Unequip hint on hover
			if (hovered) {
				dl->AddText(ImGui::GetFont(), 11.0f, {pos.x + 52, pos.y + slotH - 18},
					IM_COL32(160, 140, 80, 160), "Click to unequip");
			}
		} else {
			// Empty slot
			dl->AddText(ImGui::GetFont(), 13.0f, {pos.x + 12, pos.y + (slotH - 13) * 0.5f},
				IM_COL32(60, 55, 42, 160), label);
		}

		// ── Drag source (from equip slot) ──
		if (hasItem && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
			int idx = (int)slot;
			ImGui::SetDragDropPayload("EQUIP", &idx, sizeof(int));
			m_dragItem = itemId;
			ImGui::EndDragDropSource();
		}

		// ── Drop target (equip item) ──
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ITEM")) {
				std::string droppedId((const char*)payload->Data);
				inv.equip(slot, droppedId);
				m_dragItem.clear();
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("EQUIP")) {
				// Swap equip slots
				int fromIdx = *(const int*)payload->Data;
				WearSlot fromSlot = (WearSlot)fromIdx;
				std::string fromItem = inv.equipped(fromSlot);
				std::string toItem = inv.equipped(slot);
				inv.unequip(fromSlot);
				if (!toItem.empty()) inv.unequip(slot);
				if (!fromItem.empty()) inv.equip(slot, fromItem);
				if (!toItem.empty()) inv.equip(fromSlot, toItem);
				m_dragItem.clear();
			}
			ImGui::EndDragDropTarget();
		}

		// Tooltip
		if (hovered && hasItem && m_dragItem.empty()) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.07f, 0.05f, 0.95f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
			ImGui::BeginTooltip();
			std::string name = itemId;
			if (name.size() > 5 && name.substr(0, 5) == "base:") name = name.substr(5);
			if (!name.empty()) name[0] = (char)toupper(name[0]);
			ImGui::TextColored(ImVec4(0.92f, 0.80f, 0.38f, 1), "%s", name.c_str());
			ImGui::TextColored(ImVec4(0.50f, 0.48f, 0.42f, 1), "Slot: %s", label);
			ImGui::EndTooltip();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}

		ImGui::PopID();
	}
};

} // namespace agentica
