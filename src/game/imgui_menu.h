#pragma once

/**
 * ImGui main menu — fullscreen, C&C-inspired layout with Google colors.
 *
 * Layout:
 *   ┌─────────────────────────────────────────────┐
 *   │  AGENTWORLD                          v0.9.0    │  ← top bar
 *   ├────────┬────────────────────────────────────┤
 *   │        │                                    │
 *   │  PLAY  │                                    │
 *   │        │        content area                │
 *   │ HAND-  │    (world select, handbook,        │
 *   │  BOOK  │     settings, etc.)                │
 *   │        │                                    │
 *   │ SETT-  │                                    │
 *   │  INGS  │                                    │
 *   │        │                                    │
 *   │  QUIT  │                                    │
 *   │        │                                    │
 *   └────────┴────────────────────────────────────┘
 */

#include "game/types.h"
#include "game/handbook_ui.h"
#include "shared/artifact_registry.h"
#include "server/world_template.h"
#ifndef __EMSCRIPTEN__
#include "shared/net_socket.h"
#endif
#include <imgui.h>
#include <vector>
#include <memory>

namespace agentworld {

class ImGuiMenu {
public:
	void init(const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
		m_templates = templates;
	}

	MenuAction render(const ArtifactRegistry& registry, float W, float H) {
		MenuAction action;
		action.type = MenuAction::None;

		// Fullscreen background
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(W, H));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.95f, 0.96f, 0.97f, 1.0f));
		ImGui::Begin("##FullscreenMenu", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoBringToFrontOnFocus);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);

		// ── Top bar ──
		float topBarH = 56;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.98f, 0.97f, 0.95f, 1.0f));
		ImGui::BeginChild("TopBar", ImVec2(W, topBarH), false);
		{
			ImGui::SetCursorPos(ImVec2(24, 10));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.65f, 0.15f, 1.0f));
			ImGui::SetWindowFontScale(1.8f);
			ImGui::Text("AGENTWORLD");
			ImGui::SetWindowFontScale(1.0f);
			ImGui::PopStyleColor();

			ImGui::SameLine(W - 160);
			ImGui::SetCursorPosY(20);
			ImGui::TextColored(ImVec4(0.65f, 0.67f, 0.70f, 1), "v0.9.0  |  The world is code");
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

		// Thin blue accent line under top bar
		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(ImVec2(0, topBarH), ImVec2(W, topBarH + 3),
			IM_COL32(244, 166, 38, 255)); // Google Blue

		// ── Sidebar ──
		float sideW = 200;
		float contentY = topBarH + 3;
		float contentH = H - contentY;

		ImGui::SetCursorPos(ImVec2(0, contentY));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.98f, 0.97f, 0.95f, 1.0f));
		ImGui::BeginChild("Sidebar", ImVec2(sideW, contentH), false);
		{
			ImGui::Spacing(); ImGui::Spacing();

			// Navigation buttons — large, full-width, C&C style
			float btnW = sideW - 24;
			float btnH = 48;
			ImGui::SetCursorPosX(12);

			auto navButton = [&](const char* label, Page page) {
				bool active = (m_page == page);
				if (active) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.98f, 0.94f, 0.88f, 1));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.65f, 0.15f, 1));
				} else {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 1));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.32f, 0.35f, 1));
				}
				ImGui::SetCursorPosX(12);
				bool clicked = ImGui::Button(label, ImVec2(btnW, btnH));
				ImGui::PopStyleColor(2);

				// Active indicator — blue bar on left
				if (active) {
					ImVec2 p = ImGui::GetItemRectMin();
					dl->AddRectFilled(ImVec2(0, p.y), ImVec2(4, p.y + btnH),
						IM_COL32(244, 166, 38, 255));
				}

				return clicked;
			};

			if (navButton("Play", Page::Play)) m_page = Page::Play;
			ImGui::Spacing();
			if (navButton("Handbook", Page::Handbook)) m_page = Page::Handbook;
			ImGui::Spacing();
			if (navButton("Settings", Page::Settings)) m_page = Page::Settings;

			// Quit at bottom
			ImGui::SetCursorPosY(contentH - btnH - 20);
			ImGui::SetCursorPosX(12);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 1));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.25f, 0.25f, 1));
			if (ImGui::Button("Quit", ImVec2(btnW, btnH))) {
				action.type = MenuAction::Quit;
			}
			ImGui::PopStyleColor(2);
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

		// Sidebar right border
		dl->AddRectFilled(ImVec2(sideW - 1, contentY), ImVec2(sideW, H),
			IM_COL32(225, 228, 232, 255));

		// ── Content area ──
		ImGui::SetCursorPos(ImVec2(sideW, contentY));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.96f, 0.97f, 0.98f, 1.0f));
		ImGui::BeginChild("Content", ImVec2(W - sideW, contentH), false);
		{
			ImGui::SetCursorPos(ImVec2(32, 24));

			switch (m_page) {
			case Page::Play:
				action = renderPlayContent(W - sideW);
				break;
			case Page::Handbook:
				renderHandbookContent(registry, W - sideW, contentH);
				break;
			case Page::Settings:
				renderSettingsContent(W - sideW);
				break;
			default:
				renderPlayContent(W - sideW);
				break;
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::End();
		return action;
	}

	bool isHandbookOpen() const { return m_page == Page::Handbook; }
	HandbookUI& handbook() { return m_handbook; }
	void setPage(int p) {
		if (p == 0) m_page = Page::Play;
		else if (p == 1) m_page = Page::Handbook;
		else if (p == 2) m_page = Page::Settings;
	}
	void setGameRunning(bool running) { m_gameRunning = running; }

private:
	enum class Page { Play, Handbook, Settings };
	Page m_page = Page::Play;
	HandbookUI m_handbook;
	std::vector<std::shared_ptr<WorldTemplate>> m_templates;
	int m_selectedTemplate = 0;
	int m_gameMode = 0;  // 0=survival (default), 1=admin
	bool m_gameRunning = false;

	// Server detection
	struct DetectedServer { std::string host; int port; };
	std::vector<DetectedServer> m_detectedServers;
	bool m_probed = false;

	void probeServers() {
#ifndef __EMSCRIPTEN__
		m_detectedServers.clear();
		// Probe localhost ports 7777-7787 for running servers
		for (int port = 7777; port <= 7787; port++) {
			net::TcpClient probe;
			if (probe.connect("127.0.0.1", port, 0.15f)) {
				probe.disconnect();
				m_detectedServers.push_back({"127.0.0.1", port});
			}
		}
		m_probed = true;
#endif
	}

	MenuAction renderPlayContent(float contentW) {
		MenuAction action;

		// Resume button — shown when a game is already running
		if (m_gameRunning) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.72f, 0.40f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.55f, 0.28f, 1));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
			if (ImGui::Button("Resume Game", ImVec2(220, 48))) {
				action.type = MenuAction::ResumeGame;
			}
			ImGui::PopStyleColor(4);
			ImGui::Spacing(); ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing(); ImGui::Spacing();
		}

		// ── Servers section ──
		if (!m_probed) probeServers();

		if (!m_detectedServers.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
			ImGui::SetWindowFontScale(1.2f);
			ImGui::Text("Servers");
			ImGui::SetWindowFontScale(1.0f);
			ImGui::PopStyleColor();
			ImGui::Spacing();

			float cardW = std::min(contentW - 80, 500.0f);
			for (auto& srv : m_detectedServers) {
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.94f, 0.96f, 0.94f, 1));
				char srvId[64]; snprintf(srvId, sizeof(srvId), "##srv_%d", srv.port);
				ImGui::BeginChild(srvId, ImVec2(cardW, 48), true);
				{
					ImGui::SetCursorPos(ImVec2(16, 14));
					ImGui::TextColored(ImVec4(0.20f, 0.55f, 0.25f, 1),
						"%s:%d", srv.host.c_str(), srv.port);
					ImGui::SameLine(cardW - 90);
					ImGui::SetCursorPosY(8);
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
					char btnId[32]; snprintf(btnId, sizeof(btnId), "Join##%d", srv.port);
					if (ImGui::Button(btnId, ImVec2(70, 32))) {
						action.type = MenuAction::JoinServer;
						action.serverHost = srv.host;
						action.serverPort = srv.port;
					}
					ImGui::PopStyleColor(2);
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}

			ImGui::SameLine();
			if (ImGui::SmallButton("Refresh")) {
				m_probed = false;
			}
			ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
		}

		// ── New World section ──
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
		ImGui::SetWindowFontScale(1.2f);
		ImGui::Text("New World");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();

		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1),
			"Choose a world template to start playing.");
		ImGui::Spacing(); ImGui::Spacing();

		// Template cards
		ImGui::Text("World Template");
		ImGui::Spacing();

		float cardW = std::min(contentW - 80, 500.0f);
		for (int i = 0; i < (int)m_templates.size(); i++) {
			bool selected = (m_selectedTemplate == i);

			ImGui::PushStyleColor(ImGuiCol_ChildBg,
				selected ? ImVec4(0.98f, 0.94f, 0.88f, 1) : ImVec4(0.98f, 0.97f, 0.96f, 1));

			char id[32]; snprintf(id, sizeof(id), "##tmpl%d", i);
			ImGui::BeginChild(id, ImVec2(cardW, 64), true);
			{
				ImGui::SetCursorPos(ImVec2(16, 8));

				if (selected) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.65f, 0.15f, 1));
				} else {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.28f, 1));
				}
				ImGui::SetWindowFontScale(1.1f);
				ImGui::Text("%s", m_templates[i]->name().c_str());
				ImGui::SetWindowFontScale(1.0f);
				ImGui::PopStyleColor();

				ImGui::SetCursorPosX(16);
				ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "%s",
					m_templates[i]->description().c_str());
			}
			// Click to select
			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0))
				m_selectedTemplate = i;
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		ImGui::Spacing(); ImGui::Spacing();

		// Start button — always Survival mode (F12 toggles admin in-game)
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.65f, 0.15f, 1));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.72f, 0.28f, 1));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.90f, 0.55f, 0.10f, 1));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
		if (ImGui::Button("Start Game", ImVec2(220, 48))) {
			action.type = MenuAction::EnterGame;
			action.templateIndex = m_selectedTemplate;
			action.targetState = GameState::SURVIVAL;
		}
		ImGui::PopStyleColor(4);

		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.65f, 0.67f, 0.70f, 1),
			"Press F12 in-game for admin mode (fly, debug).");

		return action;
	}

	void renderHandbookContent(const ArtifactRegistry& registry, float contentW, float contentH) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
		ImGui::SetWindowFontScale(1.4f);
		ImGui::Text("Handbook");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();

		ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1),
			"Browse all creatures, items, blocks, and behaviors defined in the game.");
		ImGui::Spacing();

		// Embedded handbook (full width)
		bool open = true;
		ImGui::BeginChild("HandbookEmbed", ImVec2(contentW - 64, contentH - 100), false);
		{
			// Use handbook tabs directly
			if (ImGui::BeginTabBar("HBTabs")) {
				if (ImGui::BeginTabItem("Built-in")) {
					m_handbook.renderCategoryTabsPublic(registry, true);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Custom")) {
					m_handbook.renderCategoryTabsPublic(registry, false);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();
	}

	void renderSettingsContent(float contentW) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
		ImGui::SetWindowFontScale(1.4f);
		ImGui::Text("Settings");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();

		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1),
			"Controls, character selection, and audio settings.");
		ImGui::Spacing(); ImGui::Spacing();

		ImGui::Text("Coming soon — use the in-game settings for now.");
	}
};

} // namespace agentworld
