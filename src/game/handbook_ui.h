#pragma once

/**
 * HandbookUI — ImGui-based artifact browser.
 *
 * Displays all game content from the ArtifactRegistry in a tabbed interface:
 *   - Creatures (animals, monsters, NPCs)
 *   - Characters (playable skins)
 *   - Items (weapons, tools, potions)
 *   - Effects (heal, damage, teleport)
 *   - Blocks (terrain, structures)
 *   - Behaviors (AI scripts)
 *
 * Two top-level tabs: Built-in | Custom
 * Each shows the content categories as sub-tabs.
 *
 * Players can view source code of any definition and fork it for editing.
 */

#include "shared/artifact_registry.h"
#include "shared/box_model.h"
#include "client/model_preview.h"
#include "client/model.h"
#include <imgui.h>
#include <unordered_map>

namespace agentworld {

class HandbookUI {
public:
	// Register a model for 3D preview (call during init)
	void registerModel(const std::string& name, const BoxModel& model) {
		m_models[name] = model;
	}

	// Programmatically select an entry (for demo/testing)
	void selectEntry(const std::string& id) { m_selectedId = id; }

	// Set the preview renderer and model renderer (call during init)
	void setPreview(ModelPreview* preview, ModelRenderer* renderer) {
		m_preview = preview;
		m_renderer = renderer;
	}

	// Set registry for fork operations
	void setRegistry(ArtifactRegistry* reg) { m_registry = reg; }

	void render(const ArtifactRegistry& registry, bool* open) {
		if (!*open) return;

		ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Handbook", open, ImGuiWindowFlags_NoCollapse)) {
			ImGui::End();
			return;
		}

		// Top-level tabs: Built-in | Custom
		if (ImGui::BeginTabBar("HandbookTabs")) {
			if (ImGui::BeginTabItem("Built-in")) {
				renderCategoryTabs(registry, true);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Custom")) {
				renderCategoryTabs(registry, false);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	// Public for embedding in other UI panels
	void renderCategoryTabsPublic(const ArtifactRegistry& registry, bool builtin) {
		renderCategoryTabs(registry, builtin);
	}

private:
	void renderCategoryTabs(const ArtifactRegistry& registry, bool builtin) {
		// 3-group structure: Living / Objects / Logic
		struct GroupDef {
			const char* groupLabel;
			const char* groupColor; // not used yet, for future icons
			struct TabDef { const char* label; const char* category; };
			TabDef tabs[3];
			int tabCount;
		};

		GroupDef groups[] = {
			{"Living", "",
				{{"Creatures", "creature"}, {"Characters", "character"}}, 2},
			{"Objects", "",
				{{"Items", "item"}, {"Blocks", "block"}}, 2},
			{"Logic", "",
				{{"Effects", "effect"}, {"Behaviors", "behavior"}}, 2},
		};

		if (ImGui::BeginTabBar("GroupTabs")) {
			for (auto& group : groups) {
				// Count total entries in this group
				size_t groupTotal = 0;
				for (int i = 0; i < group.tabCount; i++)
					groupTotal += registry.byCategory(group.tabs[i].category, builtin).size();

				char groupLabel[64];
				snprintf(groupLabel, sizeof(groupLabel), "%s (%zu)", group.groupLabel, groupTotal);

				if (ImGui::BeginTabItem(groupLabel)) {
					// Sub-tabs for categories within this group
					if (ImGui::BeginTabBar("SubTabs")) {
						for (int i = 0; i < group.tabCount; i++) {
							auto entries = registry.byCategory(group.tabs[i].category, builtin);
							char label[64];
							snprintf(label, sizeof(label), "%s (%zu)", group.tabs[i].label, entries.size());
							if (ImGui::BeginTabItem(label)) {
								renderEntryList(entries, group.tabs[i].category);
								ImGui::EndTabItem();
							}
						}
						ImGui::EndTabBar();
					}
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
	}

	void renderEntryList(const std::vector<const ArtifactEntry*>& entries,
	                      const std::string& category) {
		if (entries.empty()) {
			ImGui::TextColored(ImVec4(0.60f, 0.62f, 0.65f, 1), "No %s defined yet.", category.c_str());
			ImGui::TextWrapped("Create one from the in-game code editor!");
			return;
		}

		// Left panel: list of entries
		ImGui::BeginChild("EntryList", ImVec2(200, 0), true);
		for (size_t i = 0; i < entries.size(); i++) {
			auto* e = entries[i];
			bool selected = (m_selectedId == e->id);
			if (ImGui::Selectable(e->name.c_str(), selected)) {
				m_selectedId = e->id;
			}
		}
		ImGui::EndChild();

		ImGui::SameLine();

		// Right panel: detail view
		ImGui::BeginChild("EntryDetail", ImVec2(0, 0), true);
		const ArtifactEntry* selected = nullptr;
		for (auto* e : entries) {
			if (e->id == m_selectedId) { selected = e; break; }
		}

		if (selected) {
			renderDetail(selected);
		} else if (!entries.empty()) {
			ImGui::TextColored(ImVec4(0.60f, 0.62f, 0.65f, 1), "Select an entry from the list.");
		}
		ImGui::EndChild();
	}

	void renderDetail(const ArtifactEntry* entry) {
		// Title
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
		ImGui::SetWindowFontScale(1.3f);
		ImGui::Text("%s", entry->name.c_str());
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();

		ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.56f, 1), "%s", entry->id.c_str());
		ImGui::Separator();

		// 3D model preview (if model registered for this entry)
		if (m_preview && m_renderer) {
			// Try to find a model by the entry's "model" field or by name
			std::string modelName;
			auto it = entry->fields.find("model");
			if (it != entry->fields.end()) modelName = it->second;
			if (modelName.empty()) modelName = entry->name;
			// Lowercase for matching
			std::string lower = modelName;
			for (auto& c : lower) c = std::tolower(c);

			auto modelIt = m_models.find(lower);
			if (modelIt == m_models.end()) modelIt = m_models.find(modelName);

			if (modelIt != m_models.end()) {
				ImGui::Spacing();
				float dt = ImGui::GetIO().DeltaTime;
				m_preview->render(*m_renderer, modelIt->second, dt, 180);
				ImGui::Spacing();
			}
		}

		if (!entry->description.empty()) {
			ImGui::TextWrapped("%s", entry->description.c_str());
			ImGui::Spacing();
		}

		// Properties table
		if (!entry->fields.empty()) {
			if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
				if (ImGui::BeginTable("Props", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
					ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 120);
					ImGui::TableSetupColumn("Value");
					ImGui::TableHeadersRow();

					for (auto& [key, val] : entry->fields) {
						if (key == "name" || key == "id" || key == "description") continue;
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::TextColored(ImVec4(0.96f, 0.65f, 0.15f, 1), "%s", key.c_str());
						ImGui::TableNextColumn();
						ImGui::Text("%s", val.c_str());
					}
					ImGui::EndTable();
				}
			}
		}

		// Source code
		if (ImGui::CollapsingHeader("Source Code")) {
			// Light code background (soft warm gray)
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.97f, 0.97f, 0.98f, 1));
			ImGui::BeginChild("Source", ImVec2(0, 200), true);

			std::istringstream stream(entry->source);
			std::string line;
			int lineNum = 1;
			while (std::getline(stream, line)) {
				// Line numbers — soft gray
				ImGui::TextColored(ImVec4(0.72f, 0.74f, 0.76f, 1), "%3d ", lineNum++);
				ImGui::SameLine();

				// Syntax colors (on white background)
				if (line.empty() || line[0] == '#') {
					// Comments — gray green
					ImGui::TextColored(ImVec4(0.40f, 0.55f, 0.40f, 1), "%s", line.c_str());
				} else if (line.find("def ") != std::string::npos ||
				           line.find("class ") != std::string::npos) {
					// Keywords — deep blue (readable on light bg)
					ImGui::TextColored(ImVec4(0.15f, 0.35f, 0.75f, 1), "%s", line.c_str());
				} else if (line.find("\"\"\"") != std::string::npos) {
					// Docstrings — Google Green
					ImGui::TextColored(ImVec4(0.20f, 0.66f, 0.33f, 1), "%s", line.c_str());
				} else if (line.find("return ") != std::string::npos ||
				           line.find("import ") != std::string::npos ||
				           line.find("from ") != std::string::npos) {
					// Keywords — purple
					ImGui::TextColored(ImVec4(0.55f, 0.25f, 0.78f, 1), "%s", line.c_str());
				} else {
					// Normal code — dark text
					ImGui::TextColored(ImVec4(0.22f, 0.22f, 0.25f, 1), "%s", line.c_str());
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}

		// File path
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.60f, 0.62f, 0.65f, 1), "File: %s", entry->filePath.c_str());

		// Fork button (for built-in entries)
		if (entry->isBuiltin && m_registry) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Fork to Custom")) {
				std::string newId = m_registry->forkEntry(entry->id);
				if (!newId.empty()) {
					m_selectedId = newId;
					m_forkedMsg = "Forked to " + newId;
					m_forkedMsgTimer = 3.0f;
				}
			}
		}

		// Show fork confirmation
		if (m_forkedMsgTimer > 0) {
			ImGui::TextColored(ImVec4(0.20f, 0.70f, 0.30f, 1), "%s", m_forkedMsg.c_str());
			m_forkedMsgTimer -= ImGui::GetIO().DeltaTime;
		}
	}

	std::string m_selectedId;
	std::unordered_map<std::string, BoxModel> m_models;
	ModelPreview* m_preview = nullptr;
	ModelRenderer* m_renderer = nullptr;
	ArtifactRegistry* m_registry = nullptr;
	std::string m_forkedMsg;
	float m_forkedMsgTimer = 0;
};

} // namespace agentworld
