#pragma once

/**
 * Equipment & Inventory UI — Diablo-style dark panel with 3D item previews.
 *
 * Opened with [I] key. Features:
 *   - Left: character equipment slots (3 wear slots: armor, offhand, back).
 *     The "main hand" is the hotbar-selected item, not a wear slot. The
 *     offhand renders as two side-by-side boxes (left hand / right hand);
 *     placing the offhand in one greys out the other and drives which
 *     hand is shown holding it on screen.
 *   - Right: scrollable item grid with rotating isometric 3D models
 *   - Drag-and-drop between grid and equipment slots
 *   - Hover tooltip with item name + count
 *   - Dark fantasy theme (charcoal, gold accents)
 */

#include "logic/inventory.h"
#include "shared/block_registry.h"
#include "logic/material_values.h"
#include "client/box_model.h"
#include "client/inventory_visuals.h"   // forward-declares ModelIconCache for VK
#include <imgui.h>
#include <string>
#include <cmath>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <set>

namespace civcraft {

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

			renderEquipSlot(dl, inventory, blocks, WearSlot::Armor, "Armor", nullptr);
			ImGui::Spacing();

			// Offhand: two side-by-side boxes for left hand / right hand.
			// One is active and holds the offhand item; the other is greyed
			// out. Toggling moves the offhand to the other hand.
			renderOffhandPair(dl, inventory, blocks);
			ImGui::Spacing();

			renderEquipSlot(dl, inventory, blocks, WearSlot::Back, "Back", nullptr);
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

			renderViewToolbar();
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
			} else if (m_sortMode == SortMode::Grid) {
				renderGridView(dl, inventory, blocks, items, cellSize, gap, cols);
			} else {
				renderSortedList(dl, inventory, blocks, items, cellSize, gap, cols);
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
	// Client-side view modes. Server only tracks {id → count}; these modes
	// just change how the local player organizes that counter visually.
	//   Name  — alphabetical by stripped display name
	//   Value — descending total worth (count × material value)
	//   Grid  — infinite slot grid the player can rearrange by drag-and-drop
	enum class SortMode { Name, Value, Grid };

	bool m_open = false;
	float m_time = 0;
	// Grid by default: items sit in real 72×72 square cells with space for
	// a clear icon, rather than the compact name-sorted list that squeezes
	// the iso-cube into a ~30 px strip.
	SortMode m_sortMode = SortMode::Grid;
	std::unordered_map<std::string, int> m_gridSlot; // id → slot index (Grid mode only)
	std::string m_dragItem;     // item being dragged
	std::string m_contextItem;  // for context menu
	const std::unordered_map<std::string, BoxModel>* m_models = nullptr;
	ModelIconCache* m_iconCache = nullptr;

	// Draw a 3D model icon for an item, or fall back to isometric cube.
	// Thin wrapper over inv_vis::drawItemIcon that injects this UI's models/time.
	void drawItemIcon(ImDrawList* dl, const std::string& id, const BlockRegistry& blocks,
	                   float x, float y, float size) {
		inv_vis::drawItemIcon(dl, id, blocks, m_models, m_iconCache, m_time, x, y, size);
	}

	void drawSectionHeader(ImDrawList* dl, const char* label) {
		inv_vis::drawSectionHeader(dl, label);
	}

	// ── View-mode toolbar (Name / Value / Grid) ──
	void renderViewToolbar() {
		auto btn = [this](const char* label, SortMode mode) {
			bool selected = (m_sortMode == mode);
			if (selected) {
				ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.38f, 0.30f, 0.13f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.46f, 0.36f, 0.16f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.52f, 0.40f, 0.18f, 1));
				ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.98f, 0.82f, 0.35f, 1));
			} else {
				ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.14f, 0.12f, 0.09f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.18f, 0.12f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.28f, 0.22f, 0.14f, 1));
				ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.70f, 0.65f, 0.55f, 1));
			}
			if (ImGui::Button(label, ImVec2(64, 22))) m_sortMode = mode;
			ImGui::PopStyleColor(4);
		};
		btn("Name",  SortMode::Name);
		ImGui::SameLine(0, 4);
		btn("Value", SortMode::Value);
		ImGui::SameLine(0, 4);
		btn("Grid",  SortMode::Grid);
	}

	// No-op retained for call sites; ids are already bare.
	static std::string stripPrefix(const std::string& s) {
		return s;
	}

	// ── Sorted list view (Name or Value) ──
	void renderSortedList(ImDrawList* dl, Inventory& inventory, const BlockRegistry& blocks,
	                      std::vector<std::pair<std::string, int>>& items,
	                      float cellSize, float gap, int cols) {
		if (m_sortMode == SortMode::Name) {
			std::sort(items.begin(), items.end(),
				[](const auto& a, const auto& b) {
					return stripPrefix(a.first) < stripPrefix(b.first);
				});
		} else { // Value
			std::sort(items.begin(), items.end(),
				[](const auto& a, const auto& b) {
					float va = getMaterialValue(a.first) * a.second;
					float vb = getMaterialValue(b.first) * b.second;
					if (va != vb) return va > vb;
					return a.first < b.first;
				});
		}

		int col = 0;
		for (auto& [id, count] : items) {
			if (col > 0) ImGui::SameLine(0, gap);
			ImGui::PushID(id.c_str());
			renderItemCell(dl, inventory, blocks, id, count, cellSize, -1);
			ImGui::PopID();
			col++;
			if (col >= cols) col = 0;
		}
	}

	// ── Grid view: infinite slot grid, purely client-side organization ──
	void renderGridView(ImDrawList* dl, Inventory& inventory, const BlockRegistry& blocks,
	                    const std::vector<std::pair<std::string, int>>& items,
	                    float cellSize, float gap, int cols) {
		// 1. Drop stale slots (items removed from inventory).
		std::set<std::string> liveIds;
		for (const auto& [id, _] : items) liveIds.insert(id);
		for (auto it = m_gridSlot.begin(); it != m_gridSlot.end(); ) {
			if (!liveIds.count(it->first)) it = m_gridSlot.erase(it);
			else ++it;
		}

		// 2. Assign new items to the lowest free slot.
		std::set<int> used;
		for (const auto& [_, s] : m_gridSlot) used.insert(s);
		for (const auto& [id, _] : items) {
			if (!m_gridSlot.count(id)) {
				int s = 0;
				while (used.count(s)) s++;
				m_gridSlot[id] = s;
				used.insert(s);
			}
		}

		// 3. Build slot → item map and find highest used slot.
		std::unordered_map<int, std::string> slotToItem;
		std::unordered_map<std::string, int> countById;
		int maxSlot = -1;
		for (const auto& [id, s] : m_gridSlot) {
			slotToItem[s] = id;
			if (s > maxSlot) maxSlot = s;
		}
		for (const auto& [id, cnt] : items) countById[id] = cnt;

		// 4. Render cells up to one padding row past the highest used slot.
		int totalSlots = maxSlot + 1 + cols;               // extra padding row
		totalSlots = ((totalSlots + cols - 1) / cols) * cols; // round up to full row

		for (int slot = 0; slot < totalSlots; slot++) {
			int col = slot % cols;
			if (col > 0) ImGui::SameLine(0, gap);

			ImGui::PushID(slot);
			auto it = slotToItem.find(slot);
			if (it != slotToItem.end()) {
				renderItemCell(dl, inventory, blocks, it->second,
				               countById[it->second], cellSize, slot);
			} else {
				renderEmptyGridCell(dl, slot, cellSize);
			}
			ImGui::PopID();
		}
	}

	// ── Empty cell in Grid view — just a drop target for rearranging ──
	void renderEmptyGridCell(ImDrawList* dl, int slot, float cellSize) {
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImU32 cellBg     = IM_COL32(10,  9,  7, 140);
		ImU32 cellBorder = IM_COL32(40, 32, 20, 100);
		dl->AddRectFilled(pos, {pos.x + cellSize, pos.y + cellSize}, cellBg, 4.0f);
		dl->AddRect      (pos, {pos.x + cellSize, pos.y + cellSize}, cellBorder, 4.0f);

		ImGui::InvisibleButton("##empty", ImVec2(cellSize, cellSize));
		bool hovered = ImGui::IsItemHovered();
		if (hovered) {
			dl->AddRect(pos, {pos.x + cellSize, pos.y + cellSize},
				IM_COL32(120, 100, 40, 140), 4.0f, 0, 1.5f);
		}

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GRID_SLOT")) {
				int srcSlot = *(const int*)payload->Data;
				if (srcSlot != slot) {
					// Move the item occupying srcSlot into this empty slot.
					for (auto& [iid, s] : m_gridSlot) {
						if (s == srcSlot) { s = slot; break; }
					}
				}
				m_dragItem.clear();
			}
			ImGui::EndDragDropTarget();
		}
	}

	// ── Render a single item cell in the grid ──
	// gridSlot >= 0  → Grid mode: drag/drop carries slot index for swap/move
	// gridSlot == -1 → Name/Value mode: standard "ITEM" payload (no positional meaning)
	void renderItemCell(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks,
	                     const std::string& id, int count, float cellSize, int gridSlot = -1) {
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
			if (gridSlot >= 0) {
				// Grid mode: carry slot index so the drop target can swap positions.
				ImGui::SetDragDropPayload("GRID_SLOT", &gridSlot, sizeof(int));
			} else {
				ImGui::SetDragDropPayload("ITEM", id.c_str(), id.size() + 1);
			}
			// Custom preview drawn in render() as dragged item
			m_dragItem = id;
			ImGui::EndDragDropSource();
		}

		// ── Drop target ──
		if (ImGui::BeginDragDropTarget()) {
			if (ImGui::AcceptDragDropPayload("ITEM")) {
				// Dropped an item onto another item cell — no action needed for counter-based inventory
			}
			if (gridSlot >= 0) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GRID_SLOT")) {
					int srcSlot = *(const int*)payload->Data;
					if (srcSlot != gridSlot) {
						// Swap the two items' slot positions.
						std::string srcId, dstId;
						for (auto& [iid, s] : m_gridSlot) {
							if (s == srcSlot)      srcId = iid;
							else if (s == gridSlot) dstId = iid;
						}
						if (!srcId.empty()) m_gridSlot[srcId] = gridSlot;
						if (!dstId.empty()) m_gridSlot[dstId] = srcSlot;
					}
					m_dragItem.clear();
				}
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
			ImGui::TextColored(ImVec4(0.90f, 0.78f, 0.35f, 1), "%s", dispName.c_str());
			ImGui::Separator();

			if (ImGui::MenuItem("Equip Armor"))
				inv.equip(WearSlot::Armor, m_contextItem);
			if (ImGui::MenuItem("Equip Offhand (Left)")) {
				inv.setOffhandInRightHand(false);
				inv.equip(WearSlot::Offhand, m_contextItem);
			}
			if (ImGui::MenuItem("Equip Offhand (Right)")) {
				inv.setOffhandInRightHand(true);
				inv.equip(WearSlot::Offhand, m_contextItem);
			}
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
				if (!name.empty()) name[0] = (char)toupper(name[0]);
			ImGui::TextColored(ImVec4(0.92f, 0.80f, 0.38f, 1), "%s", name.c_str());
			ImGui::TextColored(ImVec4(0.50f, 0.48f, 0.42f, 1), "Slot: %s", label);
			ImGui::EndTooltip();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}

		ImGui::PopID();
	}

	// ── Offhand: paired left/right hand boxes ──
	// Renders two side-by-side mini-slots. Whichever side is "active"
	// holds the offhand item; the other is greyed out. Clicking the
	// inactive side moves the item there (toggles which hand displays it).
	void renderOffhandPair(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks) {
		const auto& itemId = inv.equipped(WearSlot::Offhand);
		bool hasItem = !itemId.empty();
		bool rightActive = inv.offhandInRightHand();

		float totalW = ImGui::GetContentRegionAvail().x - 4;
		float gap = 6;
		float boxW = (totalW - gap) * 0.5f;
		float slotH = 68;

		// Left box first (visual order: L | R, matching the player's POV).
		ImVec2 startCursor = ImGui::GetCursorScreenPos();

		ImGui::PushID("OffhandPair");
		// LEFT
		renderOffhandHalf(dl, inv, blocks, /*isRightHand=*/false,
		                  hasItem, rightActive, itemId, boxW, slotH);
		ImGui::SameLine(0, gap);
		// RIGHT
		renderOffhandHalf(dl, inv, blocks, /*isRightHand=*/true,
		                  hasItem, rightActive, itemId, boxW, slotH);
		ImGui::PopID();
		(void)startCursor;
	}

	void renderOffhandHalf(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks,
	                        bool isRightHand, bool hasItem, bool rightActive,
	                        const std::string& itemId, float slotW, float slotH) {
		bool isActive = hasItem && (isRightHand == rightActive);
		bool isInactive = hasItem && !isActive;
		const char* label = isRightHand ? "Right Hand" : "Left Hand";

		ImGui::PushID(isRightHand ? "R" : "L");
		ImVec2 pos = ImGui::GetCursorScreenPos();

		// Background
		ImU32 bg, border;
		if (isActive) {
			bg = IM_COL32(22, 20, 16, 230);
			border = IM_COL32(120, 100, 45, 200);
		} else if (isInactive) {
			// Greyed out — shows as disabled because the item is in the other hand
			bg = IM_COL32(10, 9, 7, 160);
			border = IM_COL32(35, 30, 22, 110);
		} else {
			// Empty — accept drop
			bg = IM_COL32(14, 12, 10, 200);
			border = IM_COL32(45, 38, 25, 140);
		}
		dl->AddRectFilled(pos, {pos.x + slotW, pos.y + slotH}, bg, 5.0f);
		dl->AddRect(pos, {pos.x + slotW, pos.y + slotH}, border, 5.0f);

		// Click target
		if (ImGui::InvisibleButton("##offhand_half", ImVec2(slotW, slotH))) {
			if (isActive) {
				// Click active hand → unequip
				inv.unequip(WearSlot::Offhand);
			} else if (isInactive) {
				// Click inactive (greyed) hand → toggle which hand holds it
				inv.setOffhandInRightHand(isRightHand);
			}
		}
		bool hovered = ImGui::IsItemHovered();

		if (hovered && (isActive || !hasItem)) {
			dl->AddRect(pos, {pos.x + slotW, pos.y + slotH},
				IM_COL32(200, 165, 55, 140), 5.0f, 0, 2.0f);
		} else if (hovered && isInactive) {
			dl->AddRect(pos, {pos.x + slotW, pos.y + slotH},
				IM_COL32(120, 100, 60, 120), 5.0f, 0, 1.5f);
		}

		// Content
		if (isActive) {
			drawItemIcon(dl, itemId, blocks, pos.x + 6, pos.y + 6, slotH - 12);
			std::string name = itemId;
				if (!name.empty()) name[0] = (char)toupper(name[0]);
			dl->AddText(ImGui::GetFont(), 12.0f, {pos.x + slotH - 6, pos.y + 10},
				IM_COL32(200, 185, 140, 255), name.c_str());
			dl->AddText(ImGui::GetFont(), 10.0f, {pos.x + slotH - 6, pos.y + 26},
				IM_COL32(100, 90, 65, 180), label);
		} else if (isInactive) {
			// Faint icon shadow showing the item lives in the other hand
			drawItemIcon(dl, itemId, blocks, pos.x + 6, pos.y + 6, slotH - 12);
			dl->AddRectFilled(pos, {pos.x + slotW, pos.y + slotH},
				IM_COL32(0, 0, 0, 130), 5.0f);
			dl->AddText(ImGui::GetFont(), 10.0f, {pos.x + 8, pos.y + slotH - 16},
				IM_COL32(110, 95, 60, 180), label);
		} else {
			dl->AddText(ImGui::GetFont(), 12.0f, {pos.x + 8, pos.y + (slotH - 12) * 0.5f},
				IM_COL32(60, 55, 42, 160), label);
		}

		// Drag source (from active half)
		if (isActive && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
			int idx = (int)WearSlot::Offhand;
			ImGui::SetDragDropPayload("EQUIP", &idx, sizeof(int));
			m_dragItem = itemId;
			ImGui::EndDragDropSource();
		}

		// Drop target — accept item and bind to this hand
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ITEM")) {
				std::string droppedId((const char*)payload->Data);
				inv.setOffhandInRightHand(isRightHand);
				inv.equip(WearSlot::Offhand, droppedId);
				m_dragItem.clear();
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("EQUIP")) {
				int fromIdx = *(const int*)payload->Data;
				WearSlot fromSlot = (WearSlot)fromIdx;
				if (fromSlot == WearSlot::Offhand) {
					// Same item, just switching hands
					inv.setOffhandInRightHand(isRightHand);
				} else {
					std::string fromItem = inv.equipped(fromSlot);
					std::string toItem = inv.equipped(WearSlot::Offhand);
					inv.unequip(fromSlot);
					if (!toItem.empty()) inv.unequip(WearSlot::Offhand);
					inv.setOffhandInRightHand(isRightHand);
					if (!fromItem.empty()) inv.equip(WearSlot::Offhand, fromItem);
					if (!toItem.empty()) inv.equip(fromSlot, toItem);
				}
				m_dragItem.clear();
			}
			ImGui::EndDragDropTarget();
		}

		// Tooltip
		if (hovered && m_dragItem.empty()) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.07f, 0.05f, 0.95f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
			ImGui::BeginTooltip();
			if (isActive) {
				std::string name = itemId;
						if (!name.empty()) name[0] = (char)toupper(name[0]);
				ImGui::TextColored(ImVec4(0.92f, 0.80f, 0.38f, 1), "%s", name.c_str());
				ImGui::TextColored(ImVec4(0.50f, 0.48f, 0.42f, 1), "Slot: Offhand (%s)", label);
				ImGui::TextColored(ImVec4(0.45f, 0.43f, 0.38f, 0.8f), "Click to unequip");
			} else if (isInactive) {
				ImGui::TextColored(ImVec4(0.55f, 0.50f, 0.40f, 1), "Move offhand here");
			} else {
				ImGui::TextColored(ImVec4(0.55f, 0.50f, 0.40f, 1), "Offhand: %s", label);
			}
			ImGui::EndTooltip();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}

		ImGui::PopID();
	}
};

} // namespace civcraft
