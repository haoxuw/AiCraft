#pragma once

/**
 * ChestUI — split-panel chest + player inventory viewer.
 *
 * Visually matches EquipmentUI (same dark theme, same cell/icon rendering),
 * but with two stacked item grids instead of an equipment paper doll:
 *
 *   ┌─────────────────────────────────┐
 *   │  CHEST                          │
 *   │  [cells…]                       │
 *   │  ─────────────────────────────  │
 *   │  INVENTORY                      │
 *   │  [cells…]                       │
 *   └─────────────────────────────────┘
 *
 * Drag-and-drop between the two grids emits Relocate actions — there is no
 * chest-specific wire message, everything goes through the 4-action model
 * (see CLAUDE.md Rule 0).
 *
 * Click interactions (Minecraft-style):
 *   - Left-click item   → move full stack to the other inventory
 *   - Shift + left-click → move half the stack (round up)
 *   - Ctrl + left-click  → move exactly one item
 *   - Drag from A to B   → move the full stack
 *
 * The UI is auto-closed by the caller when the player walks out of range
 * (Game::updatePlaying distance-check) or when the chest entity disappears.
 */

#include "shared/inventory.h"
#include "shared/block_registry.h"
#include "shared/action.h"
#include "shared/types.h"
#include "client/box_model.h"
#include "client/model_icon_cache.h"
#include "client/inventory_visuals.h"
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <set>
#include <functional>

namespace modcraft {

class ChestUI {
public:
	// --- Lifecycle ---

	bool isOpen() const { return m_open; }
	EntityId chestEntityId() const { return m_chestEid; }

	void open(EntityId chestEid) {
		m_open = true;
		m_chestEid = chestEid;
		m_dragItem.clear();
		m_dragFromChest = false;
		// Chest slot layout is specific to this chest — reset when a new one opens.
		m_chestSlot.clear();
	}
	void close() {
		m_open = false;
		m_chestEid = ENTITY_NONE;
		m_dragItem.clear();
		m_chestSlot.clear();
	}

	void setModels(const std::unordered_map<std::string, BoxModel>* models, ModelIconCache* icons) {
		m_models = models;
		m_iconCache = icons;
	}

	// Called when drag-and-drop decides an item should move between inventories.
	// dir: true  = chest → player, false = player → chest
	using TransferFn = std::function<void(bool chestToPlayer, const std::string& itemId, int count)>;
	void setTransferCallback(TransferFn fn) { m_onTransfer = std::move(fn); }

	// --- Rendering ---

	void render(Inventory& playerInv, Inventory& chestInv,
	            const BlockRegistry& blocks, float W, float H)
	{
		if (!m_open) return;
		m_time += 1.0f / 60.0f;

		// Release drag when mouse released without drop.
		if (!ImGui::IsMouseDown(0)) { m_dragItem.clear(); }

		float panelW = std::min(W * 0.80f, 860.0f);
		float panelH = std::min(H * 0.85f, 660.0f);
		ImGui::SetNextWindowPos(ImVec2((W - panelW) / 2, (H - panelH) / 2));
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH));

		// Matches EquipmentUI theme exactly for visual parity.
		ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(0.08f, 0.07f, 0.06f, 0.96f));
		ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.35f, 0.28f, 0.15f, 0.80f));
		ImGui::PushStyleColor(ImGuiCol_TitleBg,       ImVec4(0.10f, 0.08f, 0.06f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.14f, 0.11f, 0.08f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg,       ImVec4(0.06f, 0.05f, 0.04f, 0.90f));
		ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,   ImVec4(0.06f, 0.05f, 0.04f, 0.50f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

		if (!ImGui::Begin("##Chest", &m_open,
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
			IM_COL32(210, 175, 80, 255), "CHEST");

		// Right-aligned [X] close hint.
		const char* closeHint = "ESC to close";
		ImVec2 ts = ImGui::CalcTextSize(closeHint);
		dl->AddText(ImGui::GetFont(), 13.0f, {winPos.x + winSize.x - ts.x - 14, winPos.y + 12},
			IM_COL32(130, 115, 70, 200), closeHint);
		ImGui::SetCursorPosY(42);

		float halfH = (panelH - 56) * 0.5f;

		// ── Top: chest grid ──
		ImGui::BeginChild("##ChestGrid", ImVec2(0, halfH), false);
		{
			ImGui::Spacing();
			inv_vis::drawSectionHeader(dl, "CHEST");
			ImGui::Spacing();
			renderItemGrid(dl, chestInv, blocks, /*isChest=*/true);
		}
		ImGui::EndChild();

		// Thin divider between grids.
		{
			ImVec2 p = ImGui::GetCursorScreenPos();
			float w = ImGui::GetContentRegionAvail().x;
			dl->AddLine({p.x, p.y + 2}, {p.x + w, p.y + 2}, IM_COL32(70, 56, 30, 140));
		}
		ImGui::Dummy(ImVec2(0, 6));

		// ── Bottom: player inventory grid ──
		ImGui::BeginChild("##PlayerGrid", ImVec2(0, 0), false);
		{
			ImGui::Spacing();
			inv_vis::drawSectionHeader(dl, "INVENTORY");
			ImGui::Spacing();
			renderItemGrid(dl, playerInv, blocks, /*isChest=*/false);
		}
		ImGui::EndChild();

		// Draw dragged item under cursor (identical to EquipmentUI).
		if (!m_dragItem.empty()) {
			ImVec2 mouse = ImGui::GetMousePos();
			ImDrawList* fg = ImGui::GetForegroundDrawList();
			float sz = 50;
			inv_vis::drawItemIcon(fg, m_dragItem, blocks, m_models, m_iconCache, m_time,
				mouse.x - sz * 0.5f, mouse.y - sz * 0.5f, sz);
			fg->AddEllipseFilled({mouse.x, mouse.y + sz * 0.32f}, {sz * 0.24f, sz * 0.08f},
				IM_COL32(0, 0, 0, 60));
		}

		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(6);
	}

private:
	bool m_open = false;
	EntityId m_chestEid = ENTITY_NONE;
	float m_time = 0;
	std::string m_dragItem;
	bool m_dragFromChest = false;
	const std::unordered_map<std::string, BoxModel>* m_models = nullptr;
	ModelIconCache* m_iconCache = nullptr;
	TransferFn m_onTransfer;

	// ── Grid layout (client-only, purely visual) ──
	// Each side has its own persistent slot assignment: item id → grid slot index.
	// Drag-and-drop within a grid rearranges these local positions.
	// The server never sees them; it still only tracks {id → count}.
	std::unordered_map<std::string, int> m_chestSlot;
	std::unordered_map<std::string, int> m_playerSlot;

	// Drag-source payload: encodes which side + slot the drag came from, so drop
	// targets can decide whether to rearrange (same side) or transfer (other side).
	struct DragCell {
		bool fromChest;
		int  slot;
	};

	// Compute transfer amount from current modifier keys.
	// Default: whole stack. Shift: half. Ctrl: one.
	static int transferAmount(int available) {
		if (available <= 0) return 0;
		ImGuiIO& io = ImGui::GetIO();
		if (io.KeyCtrl)  return 1;
		if (io.KeyShift) return (available + 1) / 2; // half, round up
		return available;
	}

	void fireTransfer(bool chestToPlayer, const std::string& id, int count) {
		if (!m_onTransfer) return;
		m_onTransfer(chestToPlayer, id, count);
	}

	// Swap / move `srcSlot` onto `dstSlot` within the same slot map.
	// If dstSlot is empty, the source item simply moves there. If dstSlot is
	// occupied, the two items swap positions.
	static void swapSlots(std::unordered_map<std::string, int>& m, int srcSlot, int dstSlot) {
		if (srcSlot == dstSlot) return;
		std::string srcId, dstId;
		for (auto& [iid, s] : m) {
			if (s == srcSlot)      srcId = iid;
			else if (s == dstSlot) dstId = iid;
		}
		if (!srcId.empty()) m[srcId] = dstSlot;
		if (!dstId.empty()) m[dstId] = srcSlot;
	}

	// Render one side's grid — chest or player. Uses a persistent slot map so
	// items keep their drag-assigned positions across frames.
	void renderItemGrid(ImDrawList* dl, Inventory& inv, const BlockRegistry& blocks, bool isChest) {
		auto items = inv.items();
		auto& slotMap = isChest ? m_chestSlot : m_playerSlot;

		float cellSize = 72;
		float gap = 6;
		float contentW = ImGui::GetContentRegionAvail().x;
		int cols = std::max(1, (int)((contentW + gap) / (cellSize + gap)));

		// 1. Drop stale slot entries (items no longer in the inventory).
		std::set<std::string> liveIds;
		for (auto& [id, _] : items) liveIds.insert(id);
		for (auto it = slotMap.begin(); it != slotMap.end(); ) {
			if (!liveIds.count(it->first)) it = slotMap.erase(it);
			else ++it;
		}

		// 2. Assign any new items to the lowest free slot.
		std::set<int> used;
		for (auto& [_, s] : slotMap) used.insert(s);
		for (auto& [id, _] : items) {
			if (!slotMap.count(id)) {
				int s = 0;
				while (used.count(s)) s++;
				slotMap[id] = s;
				used.insert(s);
			}
		}

		// 3. Build slot → item and id → count lookups.
		std::unordered_map<int, std::string> slotToItem;
		std::unordered_map<std::string, int> countById;
		int maxSlot = -1;
		for (auto& [id, s] : slotMap) {
			slotToItem[s] = id;
			if (s > maxSlot) maxSlot = s;
		}
		for (auto& [id, c] : items) countById[id] = c;

		// 4. Fill the available vertical space with cells (at least one padding
		//    row past the highest used slot).
		float availH  = ImGui::GetContentRegionAvail().y;
		int   rowsFit = std::max(1, (int)(availH / (cellSize + gap)));
		int   needRows = (maxSlot + 1 + cols - 1) / cols + 1; // +1 padding row
		int   rows     = std::max(rowsFit, needRows);
		int   totalSlots = rows * cols;

		for (int slot = 0; slot < totalSlots; slot++) {
			int col = slot % cols;
			if (col > 0) ImGui::SameLine(0, gap);

			ImGui::PushID(slot);
			auto it = slotToItem.find(slot);
			if (it != slotToItem.end()) {
				renderCell(dl, blocks, it->second, countById[it->second],
				           cellSize, isChest, slot);
			} else {
				renderEmptyCell(dl, cellSize, isChest, slot);
			}
			ImGui::PopID();
		}
	}

	// Empty cell — acts as a drop target only. Accepts same-side drops (rearrange)
	// and cross-side drops (transfer full stack).
	void renderEmptyCell(ImDrawList* dl, float cellSize, bool isChest, int slot) {
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
		acceptCellDrop(isChest, slot);
	}

	// Render a single item cell with drag source + drop target + click-to-transfer.
	void renderCell(ImDrawList* dl, const BlockRegistry& blocks,
	                const std::string& id, int count, float cellSize, bool isChest, int slot) {
		ImVec2 pos = ImGui::GetCursorScreenPos();

		ImU32 cellBg     = IM_COL32(18, 16, 12, 220);
		ImU32 cellBorder = IM_COL32(55, 45, 28, 150);
		dl->AddRectFilled(pos, {pos.x + cellSize, pos.y + cellSize}, cellBg, 4.0f);
		dl->AddRect      (pos, {pos.x + cellSize, pos.y + cellSize}, cellBorder, 4.0f);

		bool clicked = ImGui::InvisibleButton("##cell", ImVec2(cellSize, cellSize));
		bool hovered = ImGui::IsItemHovered();
		bool active  = ImGui::IsItemActive();

		if (hovered) {
			dl->AddRect(pos, {pos.x + cellSize, pos.y + cellSize},
				IM_COL32(200, 165, 55, 160), 4.0f, 0, 2.0f);
		}

		float iconPad = cellSize * 0.08f;
		inv_vis::drawItemIcon(dl, id, blocks, m_models, m_iconCache, m_time,
			pos.x + iconPad, pos.y + iconPad, cellSize - iconPad * 2);

		// Short name at bottom
		std::string name = id;
		if (name.size() > 5 && name.substr(0, 5) == "base:") name = name.substr(5);
		if (name.size() > 8) name = name.substr(0, 7) + "~";
		ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
		float textX = pos.x + (cellSize - textSize.x) * 0.5f;
		dl->AddText({textX, pos.y + cellSize - 16},
			IM_COL32(170, 160, 140, 200), name.c_str());

		// Count badge
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

		// ── Click transfer ──
		// Plain left-click (no drag) sends the whole stack (or half/one with modifiers)
		// to the opposite inventory.
		if (clicked && !ImGui::IsMouseDragging(0, 3.0f)) {
			int n = transferAmount(count);
			fireTransfer(/*chestToPlayer=*/isChest, id, n);
		}

		// ── Drag source ──
		if (active && ImGui::IsMouseDragging(0, 3.0f)) {
			m_dragItem = id;
			m_dragFromChest = isChest;
		}
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
			DragCell dc{isChest, slot};
			ImGui::SetDragDropPayload("CHEST_UI_CELL", &dc, sizeof(DragCell));
			m_dragItem = id;
			m_dragFromChest = isChest;
			ImGui::EndDragDropSource();
		}

		// ── Drop target: rearrange within same side OR transfer across sides ──
		acceptCellDrop(isChest, slot);

		// ── Hover tooltip ──
		if (hovered && m_dragItem.empty()) {
			ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.07f, 0.05f, 0.95f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
			ImGui::BeginTooltip();

			std::string fullName = id;
			if (fullName.size() > 5 && fullName.substr(0, 5) == "base:")
				fullName = fullName.substr(5);
			if (!fullName.empty()) fullName[0] = (char)toupper(fullName[0]);
			ImGui::TextColored(ImVec4(0.92f, 0.80f, 0.38f, 1), "%s", fullName.c_str());
			if (count > 1) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.55f, 0.52f, 0.45f, 1), "x%d", count);
			}
			ImGui::TextColored(ImVec4(0.50f, 0.48f, 0.42f, 0.8f),
				"Click: move stack   Shift: half   Ctrl: one   Drag: rearrange / transfer");
			ImGui::EndTooltip();
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}
	}

	// Universal cell drop target. Same-side drops rearrange local slot positions;
	// cross-side drops trigger a stack transfer and optimistically pin the item to
	// the dropped slot on the destination grid.
	void acceptCellDrop(bool targetIsChest, int targetSlot) {
		if (!ImGui::BeginDragDropTarget()) return;
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHEST_UI_CELL")) {
			const DragCell* dc = (const DragCell*)payload->Data;
			auto& srcMap = dc->fromChest ? m_chestSlot : m_playerSlot;
			auto& dstMap = targetIsChest ? m_chestSlot : m_playerSlot;

			if (dc->fromChest == targetIsChest) {
				// Same side → just reorder slot positions locally.
				swapSlots(srcMap, dc->slot, targetSlot);
			} else {
				// Cross side → transfer the whole stack. Look up the item id by
				// the source slot so the callback knows what to move.
				std::string movedId;
				for (auto& [iid, s] : srcMap) {
					if (s == dc->slot) { movedId = iid; break; }
				}
				if (!movedId.empty()) {
					// Optimistically pin the item to the dropped slot on the
					// destination side (client-side only; doesn't touch server).
					// If another item already occupies that slot, push it out of
					// the way by giving it the lowest free slot.
					std::string displaced;
					for (auto& [iid, s] : dstMap) {
						if (s == targetSlot && iid != movedId) { displaced = iid; break; }
					}
					if (!displaced.empty()) {
						std::set<int> usedD;
						for (auto& [_, s] : dstMap) usedD.insert(s);
						int free = 0; while (usedD.count(free)) free++;
						dstMap[displaced] = free;
					}
					dstMap[movedId] = targetSlot;

					bool chestToPlayer = dc->fromChest;
					fireTransfer(chestToPlayer, movedId, 0); // 0 = move all
				}
			}
			m_dragItem.clear();
		}
		ImGui::EndDragDropTarget();
	}
};

} // namespace modcraft
