#pragma once

// Equipment + inventory panel. Offhand has paired L/R boxes: active side holds the item,
// inactive is greyed; clicking inactive moves the item there.

#include "logic/inventory.h"
#include "logic/block_registry.h"
#include "logic/material_values.h"
#include "client/box_model.h"
#include "client/inventory_visuals.h"
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
	void setModels(const std::unordered_map<std::string, BoxModel>* models) {
		m_models = models;
	}

	void render(Inventory& inventory, const BlockRegistry& blocks, float W, float H) {
		if (!m_open) return;

		m_time += 1.0f / 60.0f;

		if (!ImGui::IsMouseDown(0)) m_dragItem.clear();

		float panelW = std::min(W * 0.85f, 920.0f);
		float panelH = std::min(H * 0.82f, 620.0f);
		ImGui::SetNextWindowPos(ImVec2((W - panelW) / 2, (H - panelH) / 2));
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH));

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

		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 winPos = ImGui::GetWindowPos();
		ImVec2 winSize = ImGui::GetWindowSize();
		dl->AddRectFilled(winPos, {winPos.x + winSize.x, winPos.y + 36},
			IM_COL32(14, 12, 8, 240));
		dl->AddLine({winPos.x, winPos.y + 36}, {winPos.x + winSize.x, winPos.y + 36},
			IM_COL32(90, 72, 35, 200));
		dl->AddText(ImGui::GetFont(), 20.0f, {winPos.x + 14, winPos.y + 8},
			IM_COL32(210, 175, 80, 255), "INVENTORY");
		ImGui::SetCursorPosY(42);

		float leftW = 240;
		ImGui::BeginChild("##Equip", ImVec2(leftW, 0), false);
		{
			ImGui::Spacing();
			drawSectionHeader(dl, "EQUIPMENT");
			ImGui::Spacing();

			renderEquipSlot(dl, inventory, blocks, WearSlot::Armor, "Armor", nullptr);
			ImGui::Spacing();

			renderOffhandPair(dl, inventory, blocks);
			ImGui::Spacing();

			renderEquipSlot(dl, inventory, blocks, WearSlot::Back, "Back", nullptr);
		}
		ImGui::EndChild();

		ImGui::SameLine(0, 4);

		ImVec2 divTop = ImGui::GetCursorScreenPos();
		divTop.x -= 2;
		dl->AddLine(divTop, {divTop.x, divTop.y + ImGui::GetContentRegionAvail().y},
			IM_COL32(70, 56, 30, 140));

		ImGui::SameLine(0, 4);

		ImGui::BeginChild("##Items", ImVec2(0, 0), false);
		{
			ImGui::Spacing();
			drawSectionHeader(dl, "ITEMS");
			ImGui::Spacing();

			renderViewToolbar();
			ImGui::Spacing();

			auto items = inventory.items();

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

		// Drag preview: item visual will be added by the upcoming world-item
		// render-reuse pass. For now, no cursor-follower icon.
		(void)blocks;

		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(6);
	}

private:
	// Client-only view modes. Server tracks only {id → count}.
	enum class SortMode { Name, Value, Grid };

	bool m_open = false;
	float m_time = 0;
	SortMode m_sortMode = SortMode::Grid;
	std::unordered_map<std::string, int> m_gridSlot;
	std::string m_dragItem;
	std::string m_contextItem;
	const std::unordered_map<std::string, BoxModel>* m_models = nullptr;

	void drawSectionHeader(ImDrawList* dl, const char* label) {
		inv_vis::drawSectionHeader(dl, label);
	}

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

	static std::string stripPrefix(const std::string& s) {
		return s;
	}

	void renderSortedList(ImDrawList* dl, Inventory& inventory, const BlockRegistry& blocks,
	                      std::vector<std::pair<std::string, int>>& items,
	                      float cellSize, float gap, int cols) {
		if (m_sortMode == SortMode::Name) {
			std::sort(items.begin(), items.end(),
				[](const auto& a, const auto& b) {
					return stripPrefix(a.first) < stripPrefix(b.first);
				});
		} else {
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

	void renderGridView(ImDrawList* dl, Inventory& inventory, const BlockRegistry& blocks,
	                    const std::vector<std::pair<std::string, int>>& items,
	                    float cellSize, float gap, int cols) {
		std::set<std::string> liveIds;
		for (const auto& [id, _] : items) liveIds.insert(id);
		for (auto it = m_gridSlot.begin(); it != m_gridSlot.end(); ) {
			if (!liveIds.count(it->first)) it = m_gridSlot.erase(it);
			else ++it;
		}

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

		std::unordered_map<int, std::string> slotToItem;
		std::unordered_map<std::string, int> countById;
		int maxSlot = -1;
		for (const auto& [id, s] : m_gridSlot) {
			slotToItem[s] = id;
			if (s > maxSlot) maxSlot = s;
		}
		for (const auto& [id, cnt] : items) countById[id] = cnt;

		// Extra padding row past the highest used slot; rounded up to full rows.
		int totalSlots = maxSlot + 1 + cols;
		totalSlots = ((totalSlots + cols - 1) / cols) * cols;

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
					for (auto& [iid, s] : m_gridSlot) {
						if (s == srcSlot) { s = slot; break; }
					}
				}
				m_dragItem.clear();
			}
			ImGui::EndDragDropTarget();
		}
	}

	// gridSlot >= 0 → Grid mode (payload carries slot for swap); -1 → Name/Value (ITEM payload).
	void renderItemCell(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks,
	                     const std::string& id, int count, float cellSize, int gridSlot = -1) {
		(void)blocks;
		ImVec2 pos = ImGui::GetCursorScreenPos();

		ImU32 cellBg = IM_COL32(18, 16, 12, 220);
		ImU32 cellBorder = IM_COL32(55, 45, 28, 150);
		dl->AddRectFilled(pos, {pos.x + cellSize, pos.y + cellSize}, cellBg, 4.0f);
		dl->AddRect(pos, {pos.x + cellSize, pos.y + cellSize}, cellBorder, 4.0f);

		bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cellSize, cellSize));
		bool hovered = ImGui::IsItemHovered();
		bool active  = ImGui::IsItemActive();

		if (hovered) {
			dl->AddRect(pos, {pos.x + cellSize, pos.y + cellSize},
				IM_COL32(200, 165, 55, 160), 4.0f, 0, 2.0f);
		}

		// Item icon will be drawn by the world-item render-reuse pass. Cell
		// currently shows only chrome, name, and count.

		std::string name = id;
		if (name.size() > 8) name = name.substr(0, 7) + "~";
		ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
		float textX = pos.x + (cellSize - textSize.x) * 0.5f;
		dl->AddText({textX, pos.y + cellSize - 16},
			IM_COL32(170, 160, 140, 200), name.c_str());

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

		if (active && ImGui::IsMouseDragging(0, 3.0f)) {
			m_dragItem = id;
		}
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
			if (gridSlot >= 0) {
				ImGui::SetDragDropPayload("GRID_SLOT", &gridSlot, sizeof(int));
			} else {
				ImGui::SetDragDropPayload("ITEM", id.c_str(), id.size() + 1);
			}
			m_dragItem = id;
			ImGui::EndDragDropSource();
		}

		if (ImGui::BeginDragDropTarget()) {
			if (ImGui::AcceptDragDropPayload("ITEM")) {
				// counter-based inventory: dropping item-on-item is a no-op
			}
			if (gridSlot >= 0) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GRID_SLOT")) {
					int srcSlot = *(const int*)payload->Data;
					if (srcSlot != gridSlot) {
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
				int slotIdx = *(const int*)payload->Data;
				inv.unequip((WearSlot)slotIdx);
			}
			ImGui::EndDragDropTarget();
		}

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

		if (hovered && m_dragItem.empty()) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.07f, 0.05f, 0.95f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
			ImGui::BeginTooltip();

			std::string fullName = id;
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

	void renderEquipSlot(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks,
	                      WearSlot slot, const char* label, const char* icon) {
		float w = ImGui::GetContentRegionAvail().x - 4;
		renderEquipSlotSized(dl, inv, blocks, slot, label, w);
	}

	void renderEquipSlotSized(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks,
	                           WearSlot slot, const char* label, float slotW) {
		auto& itemId = inv.equipped(slot);
		bool hasItem = !itemId.empty();
		float slotH = 68;

		ImGui::PushID((int)slot);

		ImVec2 pos = ImGui::GetCursorScreenPos();

		ImU32 bg = hasItem ? IM_COL32(22, 20, 16, 230) : IM_COL32(14, 12, 10, 200);
		ImU32 border = hasItem ? IM_COL32(120, 100, 45, 180) : IM_COL32(45, 38, 25, 140);
		dl->AddRectFilled(pos, {pos.x + slotW, pos.y + slotH}, bg, 5.0f);
		dl->AddRect(pos, {pos.x + slotW, pos.y + slotH}, border, 5.0f);

		if (ImGui::InvisibleButton("##equip", ImVec2(slotW, slotH))) {
			if (hasItem) inv.unequip(slot);
		}
		bool hovered = ImGui::IsItemHovered();

		if (hovered) {
			dl->AddRect(pos, {pos.x + slotW, pos.y + slotH},
				IM_COL32(200, 165, 55, 140), 5.0f, 0, 2.0f);
		}

		if (hasItem) {
			// Item icon will be drawn by the world-item render-reuse pass.
			std::string name = itemId;
				if (!name.empty()) name[0] = (char)toupper(name[0]);
			dl->AddText(ImGui::GetFont(), 14.0f, {pos.x + 52, pos.y + 12},
				IM_COL32(200, 185, 140, 255), name.c_str());

			dl->AddText(ImGui::GetFont(), 12.0f, {pos.x + 52, pos.y + 30},
				IM_COL32(100, 90, 65, 180), label);

			if (hovered) {
				dl->AddText(ImGui::GetFont(), 11.0f, {pos.x + 52, pos.y + slotH - 18},
					IM_COL32(160, 140, 80, 160), "Click to unequip");
			}
		} else {
			dl->AddText(ImGui::GetFont(), 13.0f, {pos.x + 12, pos.y + (slotH - 13) * 0.5f},
				IM_COL32(60, 55, 42, 160), label);
		}

		if (hasItem && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
			int idx = (int)slot;
			ImGui::SetDragDropPayload("EQUIP", &idx, sizeof(int));
			m_dragItem = itemId;
			ImGui::EndDragDropSource();
		}

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ITEM")) {
				std::string droppedId((const char*)payload->Data);
				inv.equip(slot, droppedId);
				m_dragItem.clear();
			}
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("EQUIP")) {
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

	void renderOffhandPair(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks) {
		const auto& itemId = inv.equipped(WearSlot::Offhand);
		bool hasItem = !itemId.empty();
		bool rightActive = inv.offhandInRightHand();

		float totalW = ImGui::GetContentRegionAvail().x - 4;
		float gap = 6;
		float boxW = (totalW - gap) * 0.5f;
		float slotH = 68;

		ImVec2 startCursor = ImGui::GetCursorScreenPos();

		ImGui::PushID("OffhandPair");
		renderOffhandHalf(dl, inv, blocks, /*isRightHand=*/false,
		                  hasItem, rightActive, itemId, boxW, slotH);
		ImGui::SameLine(0, gap);
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

		ImU32 bg, border;
		if (isActive) {
			bg = IM_COL32(22, 20, 16, 230);
			border = IM_COL32(120, 100, 45, 200);
		} else if (isInactive) {
			bg = IM_COL32(10, 9, 7, 160);
			border = IM_COL32(35, 30, 22, 110);
		} else {
			bg = IM_COL32(14, 12, 10, 200);
			border = IM_COL32(45, 38, 25, 140);
		}
		dl->AddRectFilled(pos, {pos.x + slotW, pos.y + slotH}, bg, 5.0f);
		dl->AddRect(pos, {pos.x + slotW, pos.y + slotH}, border, 5.0f);

		if (ImGui::InvisibleButton("##offhand_half", ImVec2(slotW, slotH))) {
			if (isActive) {
				inv.unequip(WearSlot::Offhand);
			} else if (isInactive) {
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

		if (isActive) {
			// Item icon will be drawn by the world-item render-reuse pass.
			std::string name = itemId;
				if (!name.empty()) name[0] = (char)toupper(name[0]);
			dl->AddText(ImGui::GetFont(), 12.0f, {pos.x + slotH - 6, pos.y + 10},
				IM_COL32(200, 185, 140, 255), name.c_str());
			dl->AddText(ImGui::GetFont(), 10.0f, {pos.x + slotH - 6, pos.y + 26},
				IM_COL32(100, 90, 65, 180), label);
		} else if (isInactive) {
			// Inactive side: darkening overlay + label only. The item icon
			// will be drawn (and darkened) by the world-item render-reuse pass.
			dl->AddRectFilled(pos, {pos.x + slotW, pos.y + slotH},
				IM_COL32(0, 0, 0, 130), 5.0f);
			dl->AddText(ImGui::GetFont(), 10.0f, {pos.x + 8, pos.y + slotH - 16},
				IM_COL32(110, 95, 60, 180), label);
		} else {
			dl->AddText(ImGui::GetFont(), 12.0f, {pos.x + 8, pos.y + (slotH - 12) * 0.5f},
				IM_COL32(60, 55, 42, 160), label);
		}

		if (isActive && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
			int idx = (int)WearSlot::Offhand;
			ImGui::SetDragDropPayload("EQUIP", &idx, sizeof(int));
			m_dragItem = itemId;
			ImGui::EndDragDropSource();
		}

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
