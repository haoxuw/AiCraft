#include "client/game_vk_renderers.h"
#include "client/game_vk.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "client/raycast.h"
#include "client/ui/ui_screen.h"
#include "client/ui/ui_theme.h"
#include "client/ui/ui_widgets.h"
#include "client/network_server.h"
#include "net/server_interface.h"
#include "logic/action.h"

namespace civcraft::vk {

// ─────────────────────────────────────────────────────────────────────────
// F3 debug overlay
// ─────────────────────────────────────────────────────────────────────────
void PanelRenderer::renderDebugOverlay() {
	Game& g = game_;
	char buf[256];
	float x = -0.96f, y = 0.90f;
	float step = 0.035f;
	const float dim[4] = {0.85f, 0.85f, 0.90f, 0.95f};

	float fps = ImGui::GetIO().Framerate;
	float fpsCol[4] = {0.85f, 0.85f, 0.90f, 0.95f};
	if (fps < 30) { fpsCol[0] = 1.0f; fpsCol[1] = 0.3f; fpsCol[2] = 0.3f; }
	else if (fps < 45) { fpsCol[0] = 1.0f; fpsCol[1] = 0.8f; fpsCol[2] = 0.2f; }
	snprintf(buf, sizeof(buf), "FPS: %.0f", fps);
	g.m_rhi->drawText2D(buf, x, y, 0.65f, fpsCol); y -= step;

	civcraft::Entity* me = g.playerEntity();
	if (me) {
		snprintf(buf, sizeof(buf), "XYZ: %.1f / %.1f / %.1f",
			me->position.x, me->position.y, me->position.z);
		g.m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;

		glm::vec3 srvPos = g.m_server->getServerPosition(me->id());
		glm::vec3 diff = srvPos - me->position;
		float posErrSq = glm::dot(diff, diff);
		float errCol[4] = {0.5f, 1.0f, 0.5f, 0.85f};
		if (posErrSq > 4.0f) { errCol[0] = 1.0f; errCol[1] = 0.3f; errCol[2] = 0.3f; errCol[3] = 0.95f; }
		snprintf(buf, sizeof(buf), "PosErr2: %.2f", posErrSq);
		g.m_rhi->drawText2D(buf, x, y, 0.60f, errCol); y -= step;
	}

	glm::vec3 dbgPos = me ? me->position : glm::vec3(0);
	int chkX = (int)std::floor(dbgPos.x) >> 4;
	int chkY = (int)std::floor(dbgPos.y) >> 4;
	int chkZ = (int)std::floor(dbgPos.z) >> 4;
	snprintf(buf, sizeof(buf), "Chunk: %d / %d / %d", chkX, chkY, chkZ);
	g.m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;

	snprintf(buf, sizeof(buf), "Entities: %zu  Chunks: %zu",
		g.m_server->entityCount(), g.m_chunkMeshes.size());
	g.m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;

	const char* modeNames[] = {"FPS", "ThirdPerson", "RPG", "RTS"};
	snprintf(buf, sizeof(buf), "Camera: %s  Admin: %s  Fly: %s",
		modeNames[(int)g.m_cam.mode],
		g.m_adminMode ? "ON" : "off",
		g.m_flyMode   ? "ON" : "off");
	g.m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;

	glm::vec3 eye = g.m_cam.position;
	glm::vec3 dir = g.m_cam.front();
	auto hit = civcraft::raycastBlocks(g.m_server->chunks(), eye, dir, 10.0f);
	if (hit) {
		auto& bdef = g.m_server->blockRegistry()
			.get(g.m_server->chunks().getBlock(
				hit->blockPos.x, hit->blockPos.y, hit->blockPos.z));
		std::string bname = bdef.display_name.empty() ? bdef.string_id : bdef.display_name;
		snprintf(buf, sizeof(buf), "Block: %s @(%d,%d,%d)",
			bname.c_str(), hit->blockPos.x, hit->blockPos.y, hit->blockPos.z);
		g.m_rhi->drawText2D(buf, x, y, 0.60f, dim); y -= step;
	}

	if (!g.m_rtsSelect.selected.empty()) {
		snprintf(buf, sizeof(buf), "RTS selected: %zu units", g.m_rtsSelect.selected.size());
		const float orange[4] = {1.0f, 0.7f, 0.2f, 0.95f};
		g.m_rhi->drawText2D(buf, x, y, 0.60f, orange); y -= step;
	}

	if (g.m_cam.mode == civcraft::CameraMode::RTS) {
		size_t activeWaypoints = 0;
		for (const auto& [eid, mo] : g.m_moveOrders)
			if (mo.active) activeWaypoints++;
		snprintf(buf, sizeof(buf), "Waypoints: %zu active", activeWaypoints);
		const float blue[4] = {0.35f, 0.75f, 1.0f, 0.95f};
		g.m_rhi->drawText2D(buf, x, y, 0.60f, blue); y -= step;
	}
}

// ══════════════════════════════════════════════════════════════════
// Render Tuning panel (F6). Every global color/grading effect the VK
// composite pass applies is driven by one slider here — all defaulting
// to 0 (no effect) so the base render is clean. Drag to taste.
// ══════════════════════════════════════════════════════════════════

void PanelRenderer::renderTuningPanel() {
	Game& g = game_;
	ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(20, 120), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Render Tuning (F6)", &g.m_showTuning)) {
		ui::Hint("Scene starts on the Vivid preset. Drag to tweak or pick another.");
		ui::VerticalSpace();

		ui::SectionHeader("Post Process");
		ImGui::SliderFloat("SSAO",      &g.m_grading.ssao,     0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Bloom",     &g.m_grading.bloom,    0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Vignette",  &g.m_grading.vignette, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("ACES Tone", &g.m_grading.aces,     0.0f, 1.0f, "%.2f");

		ui::VerticalSpace();
		ui::SectionHeader("Color Grade");
		ImGui::SliderFloat("Exposure",  &g.m_grading.exposure,   0.3f, 1.5f, "%.2f");
		ImGui::SliderFloat("Warm Tint", &g.m_grading.warmTint,   0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("S-Curve",   &g.m_grading.sCurve,     0.0f, 0.2f, "%.3f");
		ImGui::SliderFloat("Saturation",&g.m_grading.saturation,-1.0f, 1.0f, "%+0.2f");

		ui::VerticalSpace();
		ui::SectionHeader("Presets");
		float half = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
		if (ui::SecondaryButton("Vivid", half))
			g.m_grading = rhi::IRhi::GradingParams::Vivid();
		ImGui::SameLine(0, 8.0f);
		if (ui::SecondaryButton("Dungeons", half))
			g.m_grading = rhi::IRhi::GradingParams::Dungeons();
	}
	ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────
// Handbook — ImGui artifact browser (H key)
// ─────────────────────────────────────────────────────────────────────────
void PanelRenderer::renderHandbook() {
	Game& g = game_;
	ImGui::SetNextWindowSize(ImVec2(780, 540), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(
		ImVec2(g.m_fbW * 0.5f, g.m_fbH * 0.5f), ImGuiCond_FirstUseEver,
		ImVec2(0.5f, 0.5f));

	if (ImGui::Begin("Handbook", &g.m_handbookOpen)) {
		auto categories = g.m_artifactRegistry.allCategories();
		if (ImGui::BeginTabBar("HandbookTabs")) {
			for (auto& cat : categories) {
				auto entries = g.m_artifactRegistry.byCategory(cat);
				if (entries.empty()) continue;
				std::string label = cat;
				if (!label.empty()) label[0] = (char)std::toupper((unsigned char)label[0]);
				char tabLabel[64];
				snprintf(tabLabel, sizeof(tabLabel), "%s (%zu)",
					label.c_str(), entries.size());
				if (ImGui::BeginTabItem(tabLabel)) {
					// Left panel: entry list
					ImGui::BeginChild("EntryList", ImVec2(200, 0), true);
					static std::string selectedId;
					for (auto* e : entries) {
						bool sel = (selectedId == e->id);
						ImGui::PushID(e->id.c_str());
						if (ImGui::Selectable(e->name.c_str(), sel))
							selectedId = e->id;
						ImGui::PopID();
					}
					ImGui::EndChild();

					ImGui::SameLine();

					// Right panel: detail
					ImGui::BeginChild("EntryDetail", ImVec2(0, 0), true);
					const civcraft::ArtifactEntry* selected = nullptr;
					for (auto* e : entries)
						if (e->id == selectedId) { selected = e; break; }

					if (selected) {
						if (auto* f = ui::header()) ImGui::PushFont(f);
						ui::ColoredText(ui::kAccent, "%s", selected->name.c_str());
						if (ui::header()) ImGui::PopFont();
						ui::Hint("%s", selected->id.c_str());
						ui::Divider();

						if (!selected->description.empty()) {
							ImGui::TextWrapped("%s", selected->description.c_str());
							ui::VerticalSpace();
						}

						// Properties
						ui::SectionHeader("Properties");
						float matVal = civcraft::getMaterialValue(selected->id);
						ui::KeyValue("Material value", "%g", matVal);
						for (auto& [key, val] : selected->fields) {
							if (key == "name" || key == "id" || key == "description"
							    || key == "subcategory") continue;
							std::string label2 = key;
							for (auto& c : label2) if (c == '_') c = ' ';
							if (!label2.empty()) label2[0] = toupper(label2[0]);
							ui::KeyValue(label2.c_str(), "%s", val.c_str());
						}

						// Source code (Python syntax highlighting)
						if (!selected->source.empty()) {
							ui::VerticalSpace();
							if (ImGui::CollapsingHeader("Source")) {
								ImGui::PushStyleColor(ImGuiCol_ChildBg,
									ImVec4(0.02f, 0.02f, 0.02f, 0.95f));
								ImGui::BeginChild("SourceCode",
									ImVec2(0, 280), true);
								if (auto* f = ui::mono()) ImGui::PushFont(f);
								std::istringstream ss(selected->source);
								std::string line;
								while (std::getline(ss, line)) {
									std::string_view sv(line);
									auto trimmed = sv;
									while (!trimmed.empty() && trimmed[0] == ' ')
										trimmed.remove_prefix(1);
									ImVec4 color = ui::kText;
									if (trimmed.substr(0, 1) == "#")
										color = ui::kOk;
									else if (trimmed.substr(0, 3) == "def" ||
									         trimmed.substr(0, 5) == "class")
										color = ui::kAccent;
									else if (trimmed.substr(0, 6) == "return" ||
									         trimmed.substr(0, 6) == "import")
										color = ui::kAccentGlow;
									ImGui::TextColored(color, "%s", line.c_str());
								}
								if (ui::mono()) ImGui::PopFont();
								ImGui::EndChild();
								ImGui::PopStyleColor();
							}
						}

						// File path
						if (!selected->filePath.empty()) {
							ui::VerticalSpace();
							ui::Hint("%s", selected->filePath.c_str());
						}
					} else {
						ui::Hint("Select an entry from the list.");
					}
					ImGui::EndChild();

					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();

	if (!g.m_handbookOpen) {
		glfwSetInputMode(g.m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}
}

} // namespace civcraft::vk
