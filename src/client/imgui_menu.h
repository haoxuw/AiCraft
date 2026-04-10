#pragma once

/**
 * ImGui main menu — fullscreen, C&C-inspired layout with Google colors.
 *
 * Layout:
 *   ┌─────────────────────────────────────────────┐
 *   │  modcraft                          v0.9.0    │  ← top bar
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

#include "client/types.h"
#include "client/handbook_ui.h"
#include "client/world_manager.h"
#include "shared/artifact_registry.h"
#include "server/world_template.h"
#include "client/controls.h"
#include "client/audio.h"
#include "client/behavior_editor.h"
#ifndef __EMSCRIPTEN__
#include "shared/net_socket.h"
#endif
#include "client/box_model.h"
#include "client/model_preview.h"
#include <imgui.h>
#include <vector>
#include <memory>
#include <random>
#include <filesystem>
#include <fstream>

namespace modcraft {

class ImGuiMenu {
public:
	void init(const std::vector<std::shared_ptr<WorldTemplate>>& templates) {
		m_templates = templates;
		// Seed mob list from default template's Python config
		if (m_selectedTemplate < (int)m_templates.size()) {
			m_worldGenConfig.mobs.clear();
			for (auto& mc : m_templates[m_selectedTemplate]->pyConfig().mobs)
				m_worldGenConfig.mobs.push_back({mc.type, mc.count, mc.radius, mc.props});
		}
		m_worldMgr.setSavesDir("saves");
		m_worldMgr.refresh();
#ifndef __EMSCRIPTEN__
		// Start listening for LAN broadcasts immediately so we don't miss
		// announcements that arrive before the user opens the Join tab.
		m_discoverySocket.open(MODCRAFT_DISCOVER_PORT);
#endif
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
			ImGui::Text("MODCRAFT");
			ImGui::SetWindowFontScale(1.0f);
			ImGui::PopStyleColor();

			// Player name on right side of top bar
			if (m_playerName && !m_playerName->empty()) {
				std::string display = *m_playerName;
				ImGui::SameLine(W - 300);
				ImGui::SetCursorPosY(18);
				ImGui::TextColored(ImVec4(0.40f, 0.42f, 0.45f, 1), "Playing as:");
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.96f, 0.65f, 0.15f, 1), "%s", display.c_str());
			}
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
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, active ? 0.0f : 1.0f);
				if (active) {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.65f, 0.15f, 1));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.72f, 0.28f, 1));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.90f, 0.55f, 0.10f, 1));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
					ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
				} else {
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.97f, 0.97f, 0.98f, 1));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.93f, 0.94f, 0.96f, 1));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.88f, 0.89f, 0.92f, 1));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.27f, 0.30f, 1));
					ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.82f, 0.84f, 0.86f, 1));
				}
				ImGui::SetCursorPosX(12);
				bool clicked = ImGui::Button(label, ImVec2(btnW, btnH));
				ImGui::PopStyleColor(5);
				ImGui::PopStyleVar(2);

				// Active indicator — accent bar on left
				if (active) {
					ImVec2 p = ImGui::GetItemRectMin();
					dl->AddRectFilled(ImVec2(0, p.y + 4), ImVec2(4, p.y + btnH - 4),
						IM_COL32(244, 166, 38, 255), 2.0f);
				}

				return clicked;
			};

#ifndef __EMSCRIPTEN__
			if (navButton("Start game", Page::Singleplayer)) m_page = Page::Singleplayer;
			ImGui::Spacing();
#endif
			if (navButton("Join a game", Page::Multiplayer)) m_page = Page::Multiplayer;
			ImGui::Spacing();
			if (navButton("Handbook", Page::Handbook)) m_page = Page::Handbook;
			ImGui::Spacing();
			if (navButton("Settings", Page::Settings)) m_page = Page::Settings;

			// Quit at bottom
			ImGui::SetCursorPosY(contentH - btnH - 20);
			ImGui::SetCursorPosX(12);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.97f, 0.97f, 0.98f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.90f, 0.90f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.90f, 0.82f, 0.82f, 1));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.22f, 0.22f, 1));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.82f, 0.84f, 0.86f, 1));
			if (ImGui::Button("Quit", ImVec2(btnW, btnH))) {
				action.type = MenuAction::Quit;
			}
			ImGui::PopStyleColor(5);
			ImGui::PopStyleVar(2);
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

			// Only overwrite action if content page produces one
			// (sidebar Quit button may have already set it)
			MenuAction contentAction;
			switch (m_page) {
			case Page::Singleplayer:
				contentAction = renderSingleplayerContent(W - sideW);
				break;
			case Page::Multiplayer:
				contentAction = renderMultiplayerContent(W - sideW);
				break;
			case Page::Handbook:
				renderHandbookContent(registry, W - sideW, contentH);
				break;
			case Page::Settings:
				renderSettingsContent(W - sideW);
				break;
			}
			if (contentAction.type != MenuAction::None)
				action = contentAction;
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

		ImGui::End();
		return action;
	}

	bool isHandbookOpen() const { return m_page == Page::Handbook; }
	HandbookUI& handbook() { return m_handbook; }
	void setPage(int p) {
		if (p == 0) m_page = Page::Singleplayer;
		else if (p == 1) m_page = Page::Handbook;
		else if (p == 2) m_page = Page::Settings;
	}
	void setGameRunning(bool running) { m_gameRunning = running; }

	// Pre-populate a server from --host/--port CLI args
	void addServerHint(const std::string& host, int port) {
		m_serverHints.push_back({host, port});
		// Pre-fill direct connect field
		snprintf(m_directHost, sizeof(m_directHost), "%s", host.c_str());
		m_directPort = port;
		m_page = Page::Multiplayer; // auto-switch to multiplayer tab
	}
	WorldManager& worldManager() { return m_worldMgr; }

	// Set references for settings UI
	void setControls(ControlManager* c) { m_controls = c; }
	void setAudio(AudioManager* a) { m_audio = a; }
	void setPlayerInfo(std::string* name, std::string* creature) {
		m_playerName = name;
		m_selectedCreature = creature;
	}
	void setCharacterPreview(ArtifactRegistry* reg, ModelPreview* preview,
	                         ModelRenderer* renderer,
	                         std::unordered_map<std::string, BoxModel>* models) {
		m_charRegistry = reg;
		m_modelPreview = preview;
		m_modelRenderer = renderer;
		m_charModels = models;
	}
	const std::string& playerName() const { return m_playerName ? *m_playerName : m_emptyStr; }
	const std::string& selectedCreature() const { return m_selectedCreature ? *m_selectedCreature : m_emptyStr; }

private:
	enum class Page { Singleplayer, Multiplayer, Handbook, Settings };
#ifdef __EMSCRIPTEN__
	Page m_page = Page::Multiplayer; // web: join-only
#else
	Page m_page = Page::Singleplayer;
#endif
	HandbookUI m_handbook;
	std::vector<std::shared_ptr<WorldTemplate>> m_templates;
	int m_selectedTemplate = 0;
	int m_gameMode = 0;  // 0=survival (default), 1=admin
	bool m_gameRunning = false;
	bool m_showCreateWorld = false;
	WorldGenConfig m_worldGenConfig;  // advanced options for new world
	char m_newWorldName[64] = "My World";
	char m_newWorldSeed[16] = "";
	WorldManager m_worldMgr;
	ControlManager* m_controls = nullptr;
	AudioManager* m_audio = nullptr;
	int m_settingsTab = 0; // 0=Controls, 1=Audio
	BehaviorEditorState m_behaviorEditor;

	// Player identity
	std::string* m_playerName = nullptr;
	std::string* m_selectedCreature = nullptr;
	std::string m_emptyStr;
	char m_nameEditBuf[64] = {};

	// Character preview
	ArtifactRegistry* m_charRegistry = nullptr;
	ModelPreview* m_modelPreview = nullptr;
	ModelRenderer* m_modelRenderer = nullptr;
	std::unordered_map<std::string, BoxModel>* m_charModels = nullptr;

	// Direct connect fields
	char m_directHost[128] = "127.0.0.1";
#ifdef __EMSCRIPTEN__
	int m_directPort = 7779; // default websockify port
#else
	int m_directPort = 7777;
#endif

	// Server detection
	struct DetectedServer { std::string host; int port; int players = -1; bool lan = false; };
	std::vector<DetectedServer> m_detectedServers;
	std::vector<DetectedServer> m_serverHints; // from --host CLI
	bool m_probed = false;
#ifndef __EMSCRIPTEN__
	net::UdpSocket m_discoverySocket;
#endif

	void probeServers() {
#ifdef __EMSCRIPTEN__
		m_probed = true; // no LAN discovery on web — use Direct Connect instead
#else
		m_detectedServers.clear();
		for (auto& h : m_serverHints)
			m_detectedServers.push_back({h.host, h.port});

		// (Re)open UDP discovery socket to receive LAN broadcasts
		if (!m_discoverySocket.isOpen())
			m_discoverySocket.open(MODCRAFT_DISCOVER_PORT);

		// Quick TCP probe for localhost servers (same machine)
		for (int port = 7777; port <= 7787; port++) {
			bool skip = false;
			for (auto& s : m_detectedServers)
				if (s.host == "127.0.0.1" && s.port == port) { skip = true; break; }
			if (skip) continue;
			net::TcpClient probe;
			if (probe.connect("127.0.0.1", port, 0.05f)) {
				probe.disconnect();
				m_detectedServers.push_back({"127.0.0.1", port});
			}
		}
		m_probed = true;
#endif
	}

	// Drain the UDP discovery socket — called each frame while on "Join a game".
	// Updates m_detectedServers with LAN servers as announcements arrive.
	void pollDiscovery() {
#ifndef __EMSCRIPTEN__
		if (!m_discoverySocket.isOpen()) return;
		net::UdpSocket::Packet pkt;
		while (m_discoverySocket.tryRecv(pkt)) {
			int port = 0, players = 0;
			if (sscanf(pkt.data.c_str(), "MODCRAFT %d %d", &port, &players) != 2) continue;
			bool found = false;
			for (auto& s : m_detectedServers) {
				// Match exact IP, or absorb a localhost TCP-probe entry for the same port
				// (the server broadcasts its LAN IP but we probed it as 127.0.0.1)
				if (s.port == port && (s.host == pkt.senderIp || s.host == "127.0.0.1")) {
					s.players = players;
					s.lan = true;
					found = true; break;
				}
			}
			if (!found)
				m_detectedServers.push_back({pkt.senderIp, port, players, true});
		}
#endif
	}

	// ── Singleplayer: saved worlds + create new ──
	// Shared player identity UI (name + character picker from artifacts)
	void renderPlayerIdentity(float contentW) {
		if (!m_playerName || !m_selectedCreature) return;

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Your Character");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		// Name input
		if (m_nameEditBuf[0] == '\0' && !m_playerName->empty())
			snprintf(m_nameEditBuf, sizeof(m_nameEditBuf), "%s", m_playerName->c_str());
		ImGui::Text("Name");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(200);
		if (ImGui::InputText("##playername", m_nameEditBuf, sizeof(m_nameEditBuf)))
			*m_playerName = m_nameEditBuf;

		// Character picker — from artifacts/living/ (every living thing is playable).
		ImGui::Spacing();
		ImGui::Text("Character");
		ImGui::Spacing();

		if (m_charRegistry) {
			auto chars = m_charRegistry->byCategory("living");
			float cardW = 110.0f;
			int col = 0;
			for (auto* ch : chars) {
				if (col > 0) ImGui::SameLine();
				bool selected = (*m_selectedCreature == ch->id);

				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 2.0f : 1.0f);
				ImGui::PushStyleColor(ImGuiCol_Button,
					selected ? ImVec4(0.96f, 0.65f, 0.15f, 1) : ImVec4(0.96f, 0.96f, 0.97f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
					selected ? ImVec4(0.98f, 0.72f, 0.28f, 1) : ImVec4(0.92f, 0.93f, 0.95f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,
					selected ? ImVec4(0.90f, 0.55f, 0.10f, 1) : ImVec4(0.88f, 0.89f, 0.92f, 1));
				ImGui::PushStyleColor(ImGuiCol_Text,
					selected ? ImVec4(1, 1, 1, 1) : ImVec4(0.25f, 0.27f, 0.30f, 1));
				ImGui::PushStyleColor(ImGuiCol_Border,
					selected ? ImVec4(0.90f, 0.55f, 0.10f, 1) : ImVec4(0.82f, 0.84f, 0.86f, 1));

				char btnId[64]; snprintf(btnId, sizeof(btnId), "%s##ch%d", ch->name.c_str(), col);
				if (ImGui::Button(btnId, ImVec2(cardW, 48)))
					*m_selectedCreature = ch->id;

				// Tooltip with description and stats
				if (ImGui::IsItemHovered() && !ch->description.empty()) {
					ImGui::BeginTooltip();
					ImGui::Text("%s", ch->name.c_str());
					ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1), "%s", ch->description.c_str());
					ImGui::EndTooltip();
				}

				ImGui::PopStyleColor(5);
				ImGui::PopStyleVar(2);
				col++;
			}

			// 3D model preview of selected character
			if (!chars.empty() && m_modelPreview && m_modelRenderer) {
				ImGui::Spacing();
				// Find model for selected character
				std::string modelKey = *m_selectedCreature;
				auto colon = modelKey.find(':');
				if (colon != std::string::npos) modelKey = modelKey.substr(colon + 1);
				auto mit = m_charModels ? m_charModels->find(modelKey) : m_charModels->end();
				if (mit == m_charModels->end()) mit = m_charModels->find("player");
				if (mit != m_charModels->end()) {
					m_modelPreview->render(*m_modelRenderer, mit->second,
						ImGui::GetIO().DeltaTime, 160);
				}
			}
		}

		ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
	}

	MenuAction renderSingleplayerContent(float contentW) {
		MenuAction action;

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
		ImGui::SetWindowFontScale(1.4f);
		ImGui::Text("Start game");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		// Resume button
		if (m_gameRunning) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.72f, 0.40f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.55f, 0.28f, 1));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
			if (ImGui::Button("Resume Game", ImVec2(220, 48)))
				action.type = MenuAction::ResumeGame;
			ImGui::PopStyleColor(4);
			ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
		}

		// ── Saved Worlds ──
		float cardW = std::min(contentW - 80, 640.0f);
		auto& worlds = m_worldMgr.worlds();
		if (!worlds.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
			ImGui::SetWindowFontScale(1.2f);
			ImGui::Text("Your Worlds");
			ImGui::SetWindowFontScale(1.0f);
			ImGui::PopStyleColor();
			ImGui::Spacing();

			for (size_t i = 0; i < worlds.size(); i++) {
				auto& w = worlds[i];
				ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.98f, 0.97f, 0.96f, 1));
				char wid[32]; snprintf(wid, sizeof(wid), "##world%zu", i);
				ImGui::BeginChild(wid, ImVec2(cardW, 64), true);
				{
					ImGui::SetCursorPos(ImVec2(16, 12));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
					ImGui::SetWindowFontScale(1.1f);
					ImGui::Text("%s", w.name.c_str());
					ImGui::SetWindowFontScale(1.0f);
					ImGui::PopStyleColor();

					ImGui::SetCursorPosX(16);
					// Format last played nicely
					std::string timeStr = w.lastPlayed;
					if (timeStr.size() > 10) timeStr = timeStr.substr(0, 10); // just date
					ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "%s  |  %s",
						w.templateName.c_str(), timeStr.c_str());

					// Play button (right-aligned, inside card bounds)
					float btnX = cardW - 170;
					ImGui::SetCursorPos(ImVec2(btnX, 16));
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.65f, 0.15f, 1));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
					char playId[32]; snprintf(playId, sizeof(playId), "Play##w%zu", i);
					if (ImGui::Button(playId, ImVec2(80, 32))) {
						action.type = MenuAction::LoadWorld;
						action.worldPath = w.path;
						action.worldName = w.name;
						action.templateIndex = w.templateIndex;
					}
					ImGui::PopStyleColor(2);

					// Delete button
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.30f, 0.30f, 1));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
					char delId[32]; snprintf(delId, sizeof(delId), "Delete##w%zu", i);
					if (ImGui::Button(delId, ImVec2(55, 32))) {
						action.type = MenuAction::DeleteWorld;
						action.worldPath = w.path;
					}
					ImGui::PopStyleColor(2);
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}
			ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
		}

		// ── Create New World ──
		if (!m_showCreateWorld) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.65f, 0.15f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.72f, 0.28f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.90f, 0.55f, 0.10f, 1));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
			if (ImGui::Button("+ Create New World", ImVec2(220, 42))) {
				m_showCreateWorld = true;
			}
			ImGui::PopStyleColor(4);
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
			ImGui::SetWindowFontScale(1.2f);
			ImGui::Text("Create New World");
			ImGui::SetWindowFontScale(1.0f);
			ImGui::PopStyleColor();
			ImGui::Spacing();

			ImGui::Text("Name");
			ImGui::SetNextItemWidth(300);
			ImGui::InputText("##worldname", m_newWorldName, sizeof(m_newWorldName));

			ImGui::Spacing();
			ImGui::Text("Seed (blank = random)");
			ImGui::SetNextItemWidth(150);
			ImGui::InputText("##worldseed", m_newWorldSeed, sizeof(m_newWorldSeed));

			ImGui::Spacing();
			ImGui::Text("Template");
			ImGui::Spacing();
			for (int i = 0; i < (int)m_templates.size(); i++) {
				bool selected = (m_selectedTemplate == i);
				ImGui::PushStyleColor(ImGuiCol_ChildBg,
					selected ? ImVec4(0.98f, 0.94f, 0.88f, 1) : ImVec4(0.98f, 0.97f, 0.96f, 1));
				char id[32]; snprintf(id, sizeof(id), "##tmpl%d", i);
				ImGui::BeginChild(id, ImVec2(cardW, 52), true);
				{
					ImGui::SetCursorPos(ImVec2(16, 6));
					ImGui::PushStyleColor(ImGuiCol_Text,
						selected ? ImVec4(0.96f, 0.65f, 0.15f, 1) : ImVec4(0.25f, 0.25f, 0.28f, 1));
					ImGui::Text("%s", m_templates[i]->name().c_str());
					ImGui::PopStyleColor();
					ImGui::SetCursorPosX(16);
					ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "%s",
						m_templates[i]->description().c_str());
				}
				if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
					if (m_selectedTemplate != i) {
						m_selectedTemplate = i;
						// Seed mob list from template's Python config
						m_worldGenConfig.mobs.clear();
						for (auto& mc : m_templates[i]->pyConfig().mobs)
							m_worldGenConfig.mobs.push_back({mc.type, mc.count, mc.radius, mc.props});
					}
				}
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}

			// Advanced options (collapsed by default)
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Advanced Options")) {
				ImGui::Indent(16);
				ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "Creatures");
				ImGui::TextDisabled("Terrain / village / tree params: edit artifacts/worlds/base/village.py");
				for (auto& mob : m_worldGenConfig.mobs) {
					// Strip "base:" prefix and capitalize for display
					std::string label = mob.typeId;
					auto colon = label.find(':');
					if (colon != std::string::npos) label = label.substr(colon + 1);
					if (!label.empty()) label[0] = (char)std::toupper((unsigned char)label[0]);
					ImGui::SliderInt(label.c_str(), &mob.count, 0, 20);
				}
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "Gameplay");
				ImGui::SliderFloat("Pickup Range", &m_worldGenConfig.pickupRange, 0.5f, 30.0f, "%.1f blocks");
				ImGui::TextDisabled("  Max distance any entity can pick up items.");
				ImGui::SliderFloat("Store Range",  &m_worldGenConfig.storeRange,  1.0f, 10.0f, "%.1f blocks");
				ImGui::TextDisabled("  Max distance to store/take items from a chest or entity.");
				ImGui::Unindent(16);
			}

			// ── Creature Behaviors (collapsed by default) ──
			ImGui::Spacing();
			if (ImGui::CollapsingHeader("Creature Behaviors")) {
				ImGui::Indent(16);

				auto& mobs = m_worldGenConfig.mobs;
				auto& be = m_behaviorEditor;

				// Ensure selection arrays match mob count + characters
				if ((int)be.creatureSelected.size() != (int)mobs.size())
					be.creatureSelected.resize(mobs.size(), false);

				// Multi-select creature types
				ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "Select creatures & characters:");
				ImGui::Spacing();
				for (int i = 0; i < (int)mobs.size(); i++) {
					if (i > 0) ImGui::SameLine();
					std::string label = mobs[i].typeId;
					auto colon = label.find(':');
					if (colon != std::string::npos) label = label.substr(colon + 1);
					if (!label.empty()) label[0] = (char)std::toupper((unsigned char)label[0]);
					char cbId[64]; snprintf(cbId, sizeof(cbId), "%s##mob%d", label.c_str(), i);
					bool sel = be.creatureSelected[i];
					if (ImGui::Checkbox(cbId, &sel)) be.creatureSelected[i] = sel;
				}
				bool anySelected = false;
				for (auto b : be.creatureSelected) if (b) anySelected = true;

				if (anySelected) {
					ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

					// ── Behavior expression editor ──
					ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "Behavior for selected:");
					ImGui::Spacing();

					ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.96f, 0.96f, 0.97f, 1));
					ImGui::BeginChild("##behaviorTree", ImVec2(0, 0), true,
						ImGuiWindowFlags_AlwaysAutoResize);
					int idCounter = 0;
					renderExprEditor(be.sharedBehavior, 0, idCounter);
					ImGui::EndChild();
					ImGui::PopStyleColor();
					ImGui::Spacing();

					// Apply + preview
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
					if (ImGui::Button("Apply to Selected", ImVec2(160, 30))) {
						std::string pyCode = compileBehavior(be.sharedBehavior);

						// Save custom behavior as a Python file
						std::string behaviorName = "custom_" + std::to_string(
							std::hash<std::string>{}(pyCode) & 0xFFFFFF);
						{
							namespace fs = std::filesystem;
							fs::create_directories("artifacts/behaviors/player");
							std::ofstream f("artifacts/behaviors/player/" + behaviorName + ".py");
							if (f.is_open()) f << pyCode;
						}

						// Store overrides into WorldGenConfig
						for (int i = 0; i < (int)mobs.size(); i++) {
							if (!be.creatureSelected[i]) continue;
							m_worldGenConfig.behaviorOverrides[mobs[i].typeId] = behaviorName;
							if (!be.sharedItems.empty())
								m_worldGenConfig.startingItems[mobs[i].typeId] = be.sharedItems;
							auto& cfg = be.configs[mobs[i].typeId];
							cfg.typeId = mobs[i].typeId;
							cfg.behaviorId = behaviorName;
							cfg.customBehavior = be.sharedBehavior;
							cfg.startItems = be.sharedItems;
						}
					}
					ImGui::PopStyleColor(2);

					ImGui::SameLine();
					ImGui::Checkbox("Show Python", &be.showPreview);

					if (be.showPreview) {
						// Live preview — always regenerate
						std::string live = compileBehavior(be.sharedBehavior);
						ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 1));
						ImGui::BeginChild("##pypreview", ImVec2(0, 180), true);
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1));
						ImGui::TextUnformatted(live.c_str());
						ImGui::PopStyleColor();
						ImGui::EndChild();
						ImGui::PopStyleColor();
					}

					ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

					// ── Starting items ──
					ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "Starting items for selected:");
					ImGui::Spacing();
					for (int i = 0; i < (int)be.sharedItems.size(); i++) {
						ImGui::PushID(i + 5000);
						char buf[64];
						snprintf(buf, sizeof(buf), "%s", be.sharedItems[i].first.c_str());
						ImGui::SetNextItemWidth(160);
						if (ImGui::InputText("##itype", buf, sizeof(buf)))
							be.sharedItems[i].first = buf;
						ImGui::SameLine();
						ImGui::SetNextItemWidth(60);
						ImGui::InputInt("##icnt", &be.sharedItems[i].second);
						if (be.sharedItems[i].second < 1) be.sharedItems[i].second = 1;
						ImGui::SameLine();
						if (ImGui::SmallButton("x")) {
							be.sharedItems.erase(be.sharedItems.begin() + i);
							ImGui::PopID(); break;
						}
						ImGui::PopID();
					}
					if (ImGui::SmallButton("+ Add Item"))
						be.sharedItems.push_back({"base:wheat", 5});

					if (!be.configs.empty()) {
						ImGui::Spacing();
						ImGui::TextColored(ImVec4(0.4f, 0.7f, 0.4f, 1),
							"%d creature type(s) configured", (int)be.configs.size());
					}
				}
				ImGui::Unindent(16);
			}

			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.65f, 0.15f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.72f, 0.28f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.90f, 0.55f, 0.10f, 1));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
			if (ImGui::Button("Create & Play", ImVec2(180, 42))) {
				int seed = 0;
				if (strlen(m_newWorldSeed) > 0) {
					seed = atoi(m_newWorldSeed);
				} else {
					std::random_device rd;
					seed = (int)rd();
				}
				std::string tmplName = (m_selectedTemplate < (int)m_templates.size())
					? m_templates[m_selectedTemplate]->name() : "Village";
				std::string path = m_worldMgr.createWorld(m_newWorldName, seed, m_selectedTemplate, tmplName);
				action.type = MenuAction::EnterGame;
				action.templateIndex = m_selectedTemplate;
				action.seed = seed;
				action.worldPath = path;
				action.worldName = m_newWorldName;
				action.targetState = GameState::LOADING;
				action.worldGenConfig = m_worldGenConfig;
				m_showCreateWorld = false;
				m_worldMgr.refresh();
			}
			ImGui::PopStyleColor(4);

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(100, 42))) {
				m_showCreateWorld = false;
			}
		}

		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.65f, 0.67f, 0.70f, 1),
			"Press F12 in-game for admin mode.");

		return action;
	}

	// ── Multiplayer: server browser + direct connect ──
	MenuAction renderMultiplayerContent(float contentW) {
		MenuAction action;

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
		ImGui::SetWindowFontScale(1.4f);
		ImGui::Text("Join a game");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1),
			"Join a server on your local network or by address.");
		ImGui::Spacing(); ImGui::Spacing();

		// One-line identity strip
		if (m_playerName && m_selectedCreature) {
			std::string charName = *m_selectedCreature;
			auto colon = charName.find(':');
			if (colon != std::string::npos) charName = charName.substr(colon + 1);
			if (!charName.empty()) charName[0] = (char)std::toupper((unsigned char)charName[0]);

			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.97f, 0.97f, 0.98f, 1));
			ImGui::BeginChild("##identity_strip", ImVec2(contentW - 64, 40), true);
			ImGui::SetCursorPos(ImVec2(12, 10));
			ImGui::TextColored(ImVec4(0.40f, 0.42f, 0.45f, 1), "Playing as:");
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.20f, 0.20f, 0.22f, 1), "%s", m_playerName->c_str());
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "·  %s", charName.c_str());
			ImGui::SameLine();
			ImGui::SetCursorPosX(contentW - 200);
			ImGui::TextColored(ImVec4(0.96f, 0.65f, 0.15f, 1), "Settings > Profile to change");
			ImGui::EndChild();
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		// Resume button (if connected to a server)
		if (m_gameRunning) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.72f, 0.40f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.55f, 0.28f, 1));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
			if (ImGui::Button("Resume Game", ImVec2(220, 48)))
				action.type = MenuAction::ResumeGame;
			ImGui::PopStyleColor(4);
			ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
		}

		if (!m_probed) probeServers();
		pollDiscovery();

		float cardW = std::min(contentW - 80, 640.0f);

		// ── Detected Servers ──
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Servers");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

#ifdef __EMSCRIPTEN__
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1),
			"Browser cannot auto-discover LAN servers.");
		ImGui::TextColored(ImVec4(0.96f, 0.65f, 0.15f, 1),
			"Use Direct Connect below (requires websockify on the server).");
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.40f, 0.42f, 0.45f, 1),
			"Server: websockify 7779 <server-ip>:7777");
#else
		if (m_detectedServers.empty()) {
			ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1),
				"No servers found. Start a server or enter an address below.");
		}
#endif
		for (auto& srv : m_detectedServers) {
			ImVec4 cardBg = srv.lan
				? ImVec4(0.92f, 0.96f, 0.97f, 1)
				: ImVec4(0.94f, 0.96f, 0.94f, 1);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBg);
			char srvId[64]; snprintf(srvId, sizeof(srvId), "##srv_%s_%d", srv.host.c_str(), srv.port);
			ImGui::BeginChild(srvId, ImVec2(cardW, 48), true);
			{
				ImGui::SetCursorPos(ImVec2(16, 14));
				ImGui::TextColored(ImVec4(0.20f, 0.55f, 0.25f, 1),
					"%s:%d", srv.host.c_str(), srv.port);
				if (srv.lan) {
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.12f, 0.55f, 0.72f, 1), "  LAN");
				}
				if (srv.players >= 0) {
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.45f, 0.47f, 0.50f, 1),
						"  %d online", srv.players);
				}
				ImGui::SameLine(cardW - 90);
				ImGui::SetCursorPosY(8);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
				char btnId[64]; snprintf(btnId, sizeof(btnId), "Join##%s_%d", srv.host.c_str(), srv.port);
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

		if (ImGui::SmallButton("Refresh"))
			m_probed = false;

		ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

		// ── Direct Connect ──
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Direct Connect");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		ImGui::Text("Address");
		ImGui::SetNextItemWidth(250);
		ImGui::InputText("##directhost", m_directHost, sizeof(m_directHost));
		ImGui::SameLine();
		ImGui::Text(":");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		ImGui::InputInt("##directport", &m_directPort, 0, 0);

		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.35f, 1));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
		if (ImGui::Button("Connect", ImVec2(140, 36))) {
			action.type = MenuAction::JoinServer;
			action.serverHost = m_directHost;
			action.serverPort = m_directPort;
		}
		ImGui::PopStyleColor(2);

		return action;
	}

	void renderHandbookContent(const ArtifactRegistry& registry, float contentW, float contentH) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.20f, 0.20f, 0.22f, 1));
		ImGui::SetWindowFontScale(1.4f);
		ImGui::Text("Handbook");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();

		ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1),
			"Browse all game content. Custom entries marked with *.");
		ImGui::Spacing();

		// Show all content directly (no Built-in/Custom split)
		ImGui::BeginChild("HandbookEmbed", ImVec2(contentW - 64, contentH - 100), false);
		m_handbook.renderAllContent(registry);
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
			"Controls, audio, and display settings.");
		ImGui::Spacing(); ImGui::Spacing();

		// Tab bar
		if (ImGui::BeginTabBar("SettingsTabs")) {
			if (ImGui::BeginTabItem("Profile")) {
				m_settingsTab = 0;
				ImGui::Spacing();
				renderPlayerIdentity(contentW);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Controls")) {
				m_settingsTab = 1;
				ImGui::Spacing();
				renderControlsTab(contentW);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Audio")) {
				m_settingsTab = 2;
				ImGui::Spacing();
				renderAudioTab(contentW);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Gameplay")) {
				m_settingsTab = 3;
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1),
					"Server gameplay constants. Currently compiled into C++.");
				ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1),
					"Future: loaded from Python at server startup.");
				ImGui::Spacing();

				ImGui::TextColored(ImVec4(0.30f, 0.30f, 0.32f, 1), "Block Interaction");
				ImGui::Text("Break/Place Distance: 8.0 blocks");
				ImGui::Text("Break Hits (Survival): 3");
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.30f, 0.30f, 0.32f, 1), "Items");
				ImGui::SliderFloat("Max Pickup Range", &m_worldGenConfig.pickupRange, 0.5f, 30.0f, "%.1f blocks");
				ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "  (Server-wide cap. Villagers need ~16 to pick up logs from tall trees.)");
				ImGui::SliderFloat("Max Store Range",  &m_worldGenConfig.storeRange,  1.0f, 10.0f, "%.1f blocks");
				ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "  (Max distance to store/take from a chest or entity inventory.)");
				ImGui::Text("Despawn Time: 300s");
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.30f, 0.30f, 0.32f, 1), "TNT");
				ImGui::Text("Fuse: 60 ticks (1s)");
				ImGui::Text("Explosion Radius: 3 blocks");
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.30f, 0.30f, 0.32f, 1), "Entity AI");
				ImGui::Text("Decision Rate: 4 Hz");
				ImGui::Text("Block Scan Radius: 30 blocks");
				ImGui::Spacing();
				ImGui::TextColored(ImVec4(0.30f, 0.30f, 0.32f, 1), "Physics");
				ImGui::Text("Gravity: 20.0");
				ImGui::Text("Tick Rate: 60 TPS");
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}

	void renderControlsTab(float contentW) {
		float tableW = std::min(contentW - 80, 600.0f);

		// --- Configurable bindings ---
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.28f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Key Bindings");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1),
			"Edit config/controls.yaml to remap keys.");
		ImGui::Spacing();

		if (m_controls) {
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12, 6));
			if (ImGui::BeginTable("Bindings", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH,
			                       ImVec2(tableW, 0))) {
				ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, tableW * 0.55f);
				ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				for (auto& b : m_controls->bindings()) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(ImVec4(0.20f, 0.20f, 0.22f, 1), "%s", b.displayName.c_str());
					ImGui::TableSetColumnIndex(1);

					// Key badge
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.93f, 0.95f, 1));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.93f, 0.95f, 1));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.92f, 0.93f, 0.95f, 1));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.32f, 0.35f, 1));
					ImGui::SmallButton(b.keyName.c_str());
					ImGui::PopStyleColor(4);
				}
				ImGui::EndTable();
			}
			ImGui::PopStyleVar();
		}

		ImGui::Spacing(); ImGui::Spacing();

		// --- Shortcut keys (hardcoded with modifiers) ---
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.28f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Shortcuts");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		struct Shortcut { const char* key; const char* desc; };
		Shortcut shortcuts[] = {
			{"Ctrl+M",  "Toggle background music"},
			{"Ctrl+N",  "Toggle effect sounds"},
			{"Ctrl+>",  "Skip & disable current track"},
			{"Ctrl+<",  "Previous track"},
			{"F12",     "Toggle admin mode"},
			{"Tab / I", "Toggle inventory"},
			{"V",       "Cycle camera mode"},
			{"F2",      "Screenshot"},
			{"F3",      "Toggle debug overlay"},
			{"Escape",  "Open/close menu"},
		};

		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(12, 6));
		if (ImGui::BeginTable("Shortcuts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH,
		                       ImVec2(tableW, 0))) {
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, tableW * 0.25f);
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			for (auto& s : shortcuts) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.93f, 0.95f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.92f, 0.93f, 0.95f, 1));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.92f, 0.93f, 0.95f, 1));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.32f, 0.35f, 1));
				ImGui::SmallButton(s.key);
				ImGui::PopStyleColor(4);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextColored(ImVec4(0.20f, 0.20f, 0.22f, 1), "%s", s.desc);
			}
			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
	}

	void renderAudioTab(float contentW) {
		float sliderW = std::min(contentW - 120, 400.0f);

		if (!m_audio) {
			ImGui::Text("Audio not available.");
			return;
		}

		// --- Volume ---
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.28f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Volume");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		float masterVol = m_audio->masterVolume();
		ImGui::SetNextItemWidth(sliderW);
		if (ImGui::SliderFloat("Master Volume", &masterVol, 0.0f, 1.0f, "%.0f%%")) {
			m_audio->setMasterVolume(masterVol);
		}

		float musicVol = m_audio->musicVolume();
		ImGui::SetNextItemWidth(sliderW);
		if (ImGui::SliderFloat("Music Volume", &musicVol, 0.0f, 1.0f, "%.0f%%")) {
			m_audio->setMusicVolume(musicVol);
		}

		ImGui::Spacing(); ImGui::Spacing();

		// --- Toggles ---
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.28f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Toggles");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		bool musicOn = m_audio->musicPlaying();
		if (ImGui::Checkbox("Background Music (Ctrl+M)", &musicOn)) {
			if (musicOn) m_audio->startMusic();
			else m_audio->stopMusic();
		}

		bool effectsOn = !m_audio->effectsMuted();
		if (ImGui::Checkbox("Effect Sounds (Ctrl+N)", &effectsOn)) {
			m_audio->setEffectsMuted(!effectsOn);
		}

		bool globalMute = m_audio->muted();
		if (ImGui::Checkbox("Mute All", &globalMute)) {
			m_audio->setMuted(globalMute);
		}

		ImGui::Spacing(); ImGui::Spacing();

		// --- Now playing ---
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.28f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Now Playing");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		if (m_audio->musicPlaying()) {
			std::string track = m_audio->currentTrackName();
			if (!track.empty()) {
				ImGui::TextColored(ImVec4(0.96f, 0.65f, 0.15f, 1), "%s", track.c_str());
			}
			ImGui::Spacing();
			if (ImGui::Button("Prev", ImVec2(60, 32))) {
				m_audio->prevTrack();
			}
			ImGui::SameLine();
			if (ImGui::Button("Next", ImVec2(60, 32))) {
				m_audio->nextTrack();
			}
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.30f, 0.30f, 1));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
			if (ImGui::Button("Skip & Disable", ImVec2(120, 32))) {
				m_audio->skipAndDisable();
			}
			ImGui::PopStyleColor(2);
			ImGui::TextColored(ImVec4(0.65f, 0.67f, 0.70f, 1),
				"Ctrl+Shift+> skip & disable  |  Ctrl+Shift+< prev");
		} else {
			ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1), "Music is off. Enable above to listen.");
		}

		ImGui::Spacing(); ImGui::Spacing();

		// --- Track list with enable/disable ---
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.28f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Track List");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		for (int i = 0; i < m_audio->trackCount(); i++) {
			bool enabled = !m_audio->isTrackDisabled(i);
			char id[32]; snprintf(id, sizeof(id), "##trk%d", i);
			if (ImGui::Checkbox(id, &enabled)) {
				m_audio->setTrackDisabled(i, !enabled);
			}
			ImGui::SameLine();
			std::string name = m_audio->trackName(i);
			if (m_audio->isTrackDisabled(i)) {
				ImGui::TextColored(ImVec4(0.65f, 0.67f, 0.70f, 1), "%s", name.c_str());
			} else {
				ImGui::Text("%s", name.c_str());
			}
		}

		ImGui::Spacing(); ImGui::Spacing();

		// --- Stats ---
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.25f, 0.28f, 1));
		ImGui::SetWindowFontScale(1.1f);
		ImGui::Text("Library");
		ImGui::SetWindowFontScale(1.0f);
		ImGui::PopStyleColor();
		ImGui::Spacing();

		auto groups = m_audio->groupNames();
		auto& sounds = m_audio->allSounds();
		ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.60f, 1),
			"%zu sound effects in %zu groups", sounds.size(), groups.size());
	}
};

} // namespace modcraft
