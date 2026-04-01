#pragma once

/**
 * Equipment & Inventory UI — Diablo-style fullscreen panel.
 *
 * Opened with [I] key. Shows:
 *   - Left side: character silhouette with 5 equipment slots
 *   - Right side: scrollable item grid (all items in inventory)
 *   - Click item → context menu (equip, drop, use)
 *   - Click equipped slot → unequip
 */

#include "shared/inventory.h"
#include "shared/block_registry.h"
#include <imgui.h>
#include <string>

namespace agentworld {

class EquipmentUI {
public:
	bool isOpen() const { return m_open; }
	void toggle() { m_open = !m_open; }
	void close() { m_open = false; }

	void render(Inventory& inventory, const BlockRegistry& blocks, float W, float H) {
		if (!m_open) return;

		float panelW = std::min(W * 0.8f, 800.0f);
		float panelH = std::min(H * 0.8f, 550.0f);
		ImGui::SetNextWindowPos(ImVec2((W - panelW) / 2, (H - panelH) / 2));
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH));

		if (!ImGui::Begin("Inventory", &m_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
			ImGui::End();
			return;
		}

		// ── Left panel: equipment slots ──
		float leftW = 220;
		ImGui::BeginChild("Equipment", ImVec2(leftW, 0), true);
		{
			ImGui::TextColored(ImVec4(0.96f, 0.65f, 0.15f, 1), "Equipment");
			ImGui::Separator();
			ImGui::Spacing();

			renderWearSlot(inventory, blocks, WearSlot::Helmet,    "Helmet");
			ImGui::Spacing();

			// Hands side by side
			ImGui::Columns(2, "##hands", false);
			renderWearSlot(inventory, blocks, WearSlot::LeftHand,  "L.Hand");
			ImGui::NextColumn();
			renderWearSlot(inventory, blocks, WearSlot::RightHand, "R.Hand");
			ImGui::Columns(1);
			ImGui::Spacing();

			renderWearSlot(inventory, blocks, WearSlot::Body,      "Body");
			ImGui::Spacing();
			renderWearSlot(inventory, blocks, WearSlot::Back,      "Back");
		}
		ImGui::EndChild();

		ImGui::SameLine();

		// ── Right panel: item grid ──
		ImGui::BeginChild("Items", ImVec2(0, 0), true);
		{
			ImGui::TextColored(ImVec4(0.96f, 0.65f, 0.15f, 1), "Items");
			ImGui::Separator();
			ImGui::Spacing();

			auto items = inventory.items();
			if (items.empty()) {
				ImGui::TextColored(ImVec4(0.60f, 0.62f, 0.65f, 1), "No items in inventory.");
			}

			// Grid layout
			float itemSize = 70;
			float spacing = 8;
			float contentW = ImGui::GetContentRegionAvail().x;
			int cols = std::max(1, (int)((contentW + spacing) / (itemSize + spacing)));

			int col = 0;
			for (auto& [id, count] : items) {
				if (col > 0) ImGui::SameLine();

				// Item card
				const BlockDef* bdef = blocks.find(id);
				glm::vec3 color = bdef ? bdef->color_top : glm::vec3(0.5f, 0.6f, 0.7f);

				ImGui::PushID(id.c_str());
				ImGui::BeginGroup();

				// Color swatch
				ImVec2 pos = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled(
					pos, ImVec2(pos.x + itemSize, pos.y + itemSize - 18),
					IM_COL32((int)(color.r*255), (int)(color.g*255), (int)(color.b*255), 200),
					6.0f);

				// Invisible button for click handling
				if (ImGui::InvisibleButton("##item", ImVec2(itemSize, itemSize))) {
					m_contextItem = id;
					ImGui::OpenPopup("ItemContext");
				}

				// Item name (stripped prefix)
				std::string name = id;
				if (name.substr(0, 5) == "base:") name = name.substr(5);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 18);
				ImGui::TextColored(ImVec4(0.25f, 0.25f, 0.28f, 1), "%s", name.c_str());

				// Count badge
				if (count > 1) {
					char badge[16];
					snprintf(badge, sizeof(badge), "x%d", count);
					ImVec2 badgePos = ImVec2(pos.x + itemSize - 28, pos.y + 2);
					ImGui::GetWindowDrawList()->AddRectFilled(
						badgePos, ImVec2(badgePos.x + 26, badgePos.y + 16),
						IM_COL32(50, 50, 60, 200), 4.0f);
					ImGui::GetWindowDrawList()->AddText(
						ImVec2(badgePos.x + 3, badgePos.y + 1),
						IM_COL32(255, 255, 255, 255), badge);
				}

				ImGui::EndGroup();

				// Context popup
				if (ImGui::BeginPopup("ItemContext")) {
					ImGui::Text("%s", m_contextItem.c_str());
					ImGui::Separator();

					if (ImGui::MenuItem("Equip (Left Hand)"))
						inventory.equip(WearSlot::LeftHand, m_contextItem);
					if (ImGui::MenuItem("Equip (Right Hand)"))
						inventory.equip(WearSlot::RightHand, m_contextItem);
					if (ImGui::MenuItem("Equip (Helmet)"))
						inventory.equip(WearSlot::Helmet, m_contextItem);
					if (ImGui::MenuItem("Equip (Body)"))
						inventory.equip(WearSlot::Body, m_contextItem);
					if (ImGui::MenuItem("Equip (Back)"))
						inventory.equip(WearSlot::Back, m_contextItem);
					ImGui::Separator();
					if (ImGui::MenuItem("Drop 1"))
						inventory.remove(m_contextItem, 1);

					ImGui::EndPopup();
				}

				ImGui::PopID();

				col++;
				if (col >= cols) col = 0;
			}
		}
		ImGui::EndChild();

		ImGui::End();
	}

private:
	bool m_open = false;
	std::string m_contextItem;

	void renderWearSlot(Inventory& inv, const BlockRegistry& blocks,
	                      WearSlot slot, const char* label) {
		auto& itemId = inv.equipped(slot);
		bool hasItem = !itemId.empty();

		float slotW = ImGui::GetContentRegionAvail().x - 8;
		float slotH = 50;

		ImGui::PushID((int)slot);

		// Slot background
		ImVec4 bgColor = hasItem
			? ImVec4(0.94f, 0.90f, 0.82f, 1)
			: ImVec4(0.90f, 0.88f, 0.85f, 1);
		ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
			ImVec4(bgColor.x + 0.03f, bgColor.y + 0.03f, bgColor.z + 0.03f, 1));

		if (ImGui::Button("##slot", ImVec2(slotW, slotH))) {
			if (hasItem) inv.unequip(slot);
		}
		ImGui::PopStyleColor(2);

		// Draw content on top of button
		ImVec2 slotPos = ImGui::GetItemRectMin();

		if (hasItem) {
			// Item color swatch
			const BlockDef* bdef = blocks.find(itemId);
			glm::vec3 color = bdef ? bdef->color_top : glm::vec3(0.5f, 0.6f, 0.7f);
			ImGui::GetWindowDrawList()->AddRectFilled(
				ImVec2(slotPos.x + 4, slotPos.y + 4),
				ImVec2(slotPos.x + slotH - 4, slotPos.y + slotH - 4),
				IM_COL32((int)(color.r*255), (int)(color.g*255), (int)(color.b*255), 200),
				4.0f);

			// Item name
			std::string name = itemId;
			if (name.substr(0, 5) == "base:") name = name.substr(5);
			ImGui::GetWindowDrawList()->AddText(
				ImVec2(slotPos.x + slotH, slotPos.y + 8),
				IM_COL32(40, 40, 45, 255), name.c_str());

			// Slot label (small)
			ImGui::GetWindowDrawList()->AddText(
				ImVec2(slotPos.x + slotH, slotPos.y + 28),
				IM_COL32(140, 140, 150, 200), label);
		} else {
			// Empty slot label
			ImGui::GetWindowDrawList()->AddText(
				ImVec2(slotPos.x + 12, slotPos.y + 16),
				IM_COL32(160, 160, 170, 200), label);
		}

		ImGui::PopID();
	}
};

} // namespace agentworld
