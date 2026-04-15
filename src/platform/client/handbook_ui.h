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
#include "shared/material_values.h"
#include "client/box_model.h"
#include "client/model_preview.h"
#include "client/model.h"
#include "client/audio.h"
#include <imgui.h>
#include <unordered_map>

namespace civcraft {

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

	// Set audio manager for sound preview
	void setAudio(AudioManager* audio) { m_audio = audio; }

	void render(const ArtifactRegistry& registry, bool* open) {
		if (!*open) return;

		ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Handbook", open, ImGuiWindowFlags_NoCollapse)) {
			ImGui::End();
			return;
		}

		renderGroupTabs(registry);

		ImGui::End();
	}

	// Render all content (both built-in and custom) with flat group tabs
	void renderAllContent(const ArtifactRegistry& registry) {
		renderGroupTabs(registry);
	}

	// Legacy: for standalone handbook window
	void renderCategoryTabsPublic(const ArtifactRegistry& registry, bool builtin) {
		renderGroupTabs(registry);
	}

private:
	void renderGroupTabs(const ArtifactRegistry& registry) {
		// Derive tabs dynamically from the registry — no hardcoded list.
		// Each unique category becomes a tab, ordered by first appearance.
		auto categories = registry.allCategories();

		if (ImGui::BeginTabBar("HandbookTabs")) {
			for (auto& cat : categories) {
				auto entries = registry.byCategory(cat);
				if (entries.empty()) continue;
				// Capitalize first letter for display
				std::string label = cat;
				if (!label.empty()) label[0] = (char)std::toupper((unsigned char)label[0]);
				char tabLabel[64];
				snprintf(tabLabel, sizeof(tabLabel), "%s (%zu)", label.c_str(), entries.size());
				if (ImGui::BeginTabItem(tabLabel)) {
					if (m_audio && m_lastTab != cat) {
						m_audio->playBlip(1.0f, 0.5f);
						m_lastTab = cat;
					}
					renderEntryList(entries, cat);
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
				if (m_selectedId != e->id && m_audio) {
					m_audio->playBlip(1.2f, 0.4f);  // slightly higher pitch on select
				}
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

				// Clip selector — lets players preview each named animation
				// (attack, chop, mine, wave, dance, sleep, sit, fly, …) right
				// in the handbook, so new clips are discoverable without
				// firing up a scenario.
				const auto& clips = modelIt->second.clips;
				if (!clips.empty()) {
					// Reset picker when the selected entry changes so we
					// don't carry a stale clip across unrelated models.
					if (m_clipOwnerId != entry->id) {
						m_clipOwnerId = entry->id;
						m_preview->setClip("");
					}
					ImGui::SameLine();
					ImGui::BeginGroup();
					ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.56f, 1), "Clip");
					std::string cur = m_preview->clip();
					const char* curLabel = cur.empty() ? "idle" : cur.c_str();
					ImGui::SetNextItemWidth(140);
					if (ImGui::BeginCombo("##clip", curLabel)) {
						if (ImGui::Selectable("idle", cur.empty())) {
							if (m_audio) m_audio->playBlip(1.0f, 0.4f);
							m_preview->setClip("");
						}
						for (auto& [name, _] : clips) {
							bool sel = (cur == name);
							if (ImGui::Selectable(name.c_str(), sel)) {
								if (m_audio) m_audio->playBlip(1.1f, 0.4f);
								m_preview->setClip(name);
							}
						}
						ImGui::EndCombo();
					}
					ImGui::EndGroup();
				} else if (m_clipOwnerId != entry->id) {
					m_clipOwnerId = entry->id;
					m_preview->setClip("");
				}
				ImGui::Spacing();
			}
		}

		if (!entry->description.empty()) {
			ImGui::TextWrapped("%s", entry->description.c_str());
			ImGui::Spacing();
		}

		// Properties table — show user-meaningful fields only
		{
			// Collect displayable properties
			std::vector<std::pair<std::string, std::string>> displayProps;

			// Material value — looked up directly from shared/material_values.h.
			// Single source of truth: everything (items, blocks, entities) has a value.
			// For living entities, this value also doubles as their inventory capacity.
			if (!entry->id.empty()) {
				float v = getMaterialValue(entry->id);
				char buf[32];
				snprintf(buf, sizeof(buf), "%g", v);
				displayProps.push_back({"Material value", buf});
			}

			for (auto& [key, val] : entry->fields) {
				// Skip internal/duplicate fields
				if (key == "name" || key == "id" || key == "description" ||
				    key == "subcategory") continue;
				// Format key: replace underscores with spaces, capitalize
				std::string label = key;
				for (auto& c : label) if (c == '_') c = ' ';
				if (!label.empty()) label[0] = toupper(label[0]);
				displayProps.push_back({label, val});
			}

			if (!displayProps.empty()) {
				if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
					if (ImGui::BeginTable("Props", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
						ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130);
						ImGui::TableSetupColumn("Value");

						for (auto& [label, val] : displayProps) {
							ImGui::TableNextRow();
							ImGui::TableNextColumn();
							ImGui::TextColored(ImVec4(0.50f, 0.52f, 0.56f, 1), "%s", label.c_str());
							ImGui::TableNextColumn();
							ImGui::Text("%s", val.c_str());
						}
						ImGui::EndTable();
					}
				}
			}
		}

		// For resource entries: show sound preview instead of source code
		if (entry->category == "resource" && m_audio) {
			renderSoundPreview(entry);
		} else {
			// Source code (for behaviors, creatures, items, etc.)
			if (ImGui::CollapsingHeader("Source Code")) {
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.97f, 0.97f, 0.98f, 1));
				ImGui::BeginChild("Source", ImVec2(0, 200), true);

				std::istringstream stream(entry->source);
				std::string line;
				int lineNum = 1;
				while (std::getline(stream, line)) {
					ImGui::TextColored(ImVec4(0.72f, 0.74f, 0.76f, 1), "%3d ", lineNum++);
					ImGui::SameLine();

					if (line.empty() || line[0] == '#') {
						ImGui::TextColored(ImVec4(0.40f, 0.55f, 0.40f, 1), "%s", line.c_str());
					} else if (line.find("def ") != std::string::npos ||
					           line.find("class ") != std::string::npos) {
						ImGui::TextColored(ImVec4(0.15f, 0.35f, 0.75f, 1), "%s", line.c_str());
					} else if (line.find("\"\"\"") != std::string::npos) {
						ImGui::TextColored(ImVec4(0.20f, 0.66f, 0.33f, 1), "%s", line.c_str());
					} else if (line.find("return ") != std::string::npos ||
					           line.find("import ") != std::string::npos ||
					           line.find("from ") != std::string::npos) {
						ImGui::TextColored(ImVec4(0.55f, 0.25f, 0.78f, 1), "%s", line.c_str());
					} else {
						ImGui::TextColored(ImVec4(0.22f, 0.22f, 0.25f, 1), "%s", line.c_str());
					}
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
			}
		}

		// File path
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.60f, 0.62f, 0.65f, 1), "File: %s", entry->filePath.c_str());

		// Fork button (for built-in entries)
		if (entry->isBuiltin && m_registry) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Fork to Custom")) {
				if (m_audio) m_audio->playBlip(0.6f, 0.4f);
				printf("[Handbook] Fork clicked for '%s' (file: %s)\n",
					entry->id.c_str(), entry->filePath.c_str());
				printf("[Handbook] Registry basePath='%s', playerNS='%s'\n",
					m_registry->basePath().c_str(), m_registry->playerNS().c_str());
				printf("[Handbook] Source length: %zu bytes\n", entry->source.size());
				std::string newId = m_registry->forkEntry(entry->id);
				if (!newId.empty()) {
					printf("[Handbook] Fork succeeded → %s\n", newId.c_str());
					m_selectedId = newId;
					m_forkedMsg = "Forked to " + newId;
					m_forkedMsgTimer = 3.0f;
				} else {
					printf("[Handbook] Fork FAILED (forkEntry returned empty)\n");
					m_forkedMsg = "Fork failed!";
					m_forkedMsgTimer = 3.0f;
				}
			}
		} else if (entry->isBuiltin && !m_registry) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "(Fork unavailable: no registry)");
		}

		// Show fork confirmation
		if (m_forkedMsgTimer > 0) {
			ImGui::TextColored(ImVec4(0.20f, 0.70f, 0.30f, 1), "%s", m_forkedMsg.c_str());
			m_forkedMsgTimer -= ImGui::GetIO().DeltaTime;
		}
	}

	void renderSoundPreview(const ArtifactEntry* entry) {
		// Parse the "groups" field to get sound group names
		auto groupsIt = entry->fields.find("groups");
		if (groupsIt == entry->fields.end()) return;

		std::string groupsStr = groupsIt->second;
		std::vector<std::string> groups;
		size_t pos = 0;
		while (pos < groupsStr.size()) {
			size_t comma = groupsStr.find(',', pos);
			if (comma == std::string::npos) comma = groupsStr.size();
			std::string g = groupsStr.substr(pos, comma - pos);
			// trim whitespace
			size_t start = g.find_first_not_of(" \t");
			size_t end = g.find_last_not_of(" \t");
			if (start != std::string::npos)
				groups.push_back(g.substr(start, end - start + 1));
			pos = comma + 1;
		}

		if (groups.empty()) return;

		if (ImGui::CollapsingHeader("Sound Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.96f, 0.97f, 0.98f, 1));
			ImGui::BeginChild("SoundPreview", ImVec2(0, 250), true);

			for (auto& groupName : groups) {
				auto files = m_audio->filesInGroup(groupName);

				// Group header with play-random button
				ImGui::PushID(groupName.c_str());

				bool open = ImGui::TreeNodeEx("##grp", ImGuiTreeNodeFlags_DefaultOpen,
					"%s (%zu)", groupName.c_str(), files.size());

				// Play random button on same line as header
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
				if (ImGui::SmallButton("Play Random")) {
					m_audio->play(groupName, 0.8f);
				}

				if (open) {
					// List individual files with play buttons
					for (auto& file : files) {
						// Extract just the filename
						std::string filename = std::filesystem::path(file).filename().string();

						ImGui::PushID(file.c_str());
						if (ImGui::SmallButton(">")) {
							m_audio->playFile(file, 0.8f);
						}
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(0.40f, 0.42f, 0.45f, 1), "%s", filename.c_str());
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}

			ImGui::EndChild();
			ImGui::PopStyleColor();
		}
	}

	std::string m_selectedId;
	std::string m_lastTab;
	std::string m_clipOwnerId;   // which entry's clip is currently in m_preview
	std::unordered_map<std::string, BoxModel> m_models;
	ModelPreview* m_preview = nullptr;
	ModelRenderer* m_renderer = nullptr;
	ArtifactRegistry* m_registry = nullptr;
	AudioManager* m_audio = nullptr;
	std::string m_forkedMsg;
	float m_forkedMsgTimer = 0;
};

} // namespace civcraft
