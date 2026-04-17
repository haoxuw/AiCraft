#include "client/game_vk_renderers.h"
#include "client/game_vk.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>

#include "client/ui/ui_screen.h"
#include "client/ui/ui_theme.h"
#include "client/ui/ui_widgets.h"
#include "client/network_server.h"
#include "net/server_interface.h"

namespace civcraft::vk {

// ── Menu screens ─────────────────────────────────────────────────────
// All menu/pause/death screens compose from the UI kit (ui_screen +
// ui_widgets). No per-screen palette pushes — theme + fonts come from
// ui_theme globally.

static void menuControlsBlock() {
	ui::SectionHeader("Controls");
	ui::KeyValue("Move",           "WASD");
	ui::KeyValue("Jump",           "Space");
	ui::KeyValue("Sprint",         "Shift");
	ui::KeyValue("Look",           "Mouse");
	ui::KeyValue("Attack / Place", "LMB  /  RMB");
	ui::KeyValue("Drop",           "Q");
	ui::KeyValue("Inventory",      "Tab");
	ui::KeyValue("Handbook",       "H");
	ui::KeyValue("Camera",         "V");
	ui::KeyValue("Pause",          "Esc");
	ui::KeyValue("Debug / Tuning", "F3  /  F6");
	ui::KeyValue("Screenshot",     "F2");
}

void MenuRenderer::renderMenu() {
	Game& g = game_;
	// Centered brass title over rotating-world backdrop.
	ui::ScreenTitle(g.m_rhi, "CIVCRAFT", 0.48f, 3.6f, g.m_menuTitleT);

	// ESC in a sub-screen returns to Main (same gesture as closing a popup).
	bool escPressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
	if (escPressed && g.m_menuScreen != MenuScreen::Main) {
		g.m_menuScreen = MenuScreen::Main;
	}

	ui::ScreenOpts opts;
	opts.panelW  = 380.0f;
	opts.anchorY = 0.34f;

	switch (g.m_menuScreen) {
	case MenuScreen::Main: {
		if (ui::BeginScreen("##menu_main", g.m_rhi, opts)) {
			// ImGui's keyboard Nav already activates the focused button on
			// ENTER/SPACE — don't add a second handler or it double-fires on
			// Nav-auto-focus (jumps past the main menu on first frame).
			if (ui::PrimaryButton("SINGLEPLAYER")) g.m_menuScreen = MenuScreen::Singleplayer;
			if (ui::SecondaryButton("MULTIPLAYER")) g.m_menuScreen = MenuScreen::Multiplayer;
			if (ui::SecondaryButton("SETTINGS"))    g.m_menuScreen = MenuScreen::Settings;
			ui::VerticalSpace();
			if (ui::GhostButton("QUIT")) g.m_shouldQuit = true;

			if (!g.m_lastDeathReason.empty()) {
				ui::VerticalSpace();
				ui::Divider();
				ui::ColoredText(ui::kBad, "%s", g.m_lastDeathReason.c_str());
			}
		}
		ui::EndScreen();
		break;
	}
	case MenuScreen::Singleplayer: {
		opts.panelW = 440.0f;
		if (ui::BeginScreen("##menu_sp", g.m_rhi, opts)) {
			ui::SectionHeader("Your Worlds");
			ui::Hint("World list, thumbnails, and create-world arrive in Stage C.");
			ui::VerticalSpace();
			if (ui::PrimaryButton("START  VILLAGE  WORLD")) {
				g.m_connectError.clear();
				g.m_menuScreen = MenuScreen::CharacterSelect;
			}
			ui::VerticalSpace();
			if (ui::GhostButton("Back")) g.m_menuScreen = MenuScreen::Main;
		}
		ui::EndScreen();
		break;
	}
	case MenuScreen::CharacterSelect: {
		opts.panelW = 520.0f;
		if (ui::BeginScreen("##menu_charpick", g.m_rhi, opts)) {
			ui::SectionHeader("Choose Your Character");
			ui::Hint("Any living with \"playable\": True in its artifact can be played.");
			ui::VerticalSpace();

			// Collect playable livings from the artifact registry (data-driven —
			// add a new entry in artifacts/living/*.py and it shows up here).
			struct PlayableItem { std::string id, name, desc; };
			std::vector<PlayableItem> playables;
			for (auto* e : g.m_artifactRegistry.byCategory("living")) {
				auto it = e->fields.find("playable");
				if (it == e->fields.end()) continue;
				// Python's True; parser strips surrounding whitespace.
				if (it->second != "True" && it->second != "true") continue;
				playables.push_back({e->id, e->name, e->description});
			}
			std::sort(playables.begin(), playables.end(),
				[](const PlayableItem& a, const PlayableItem& b) { return a.name < b.name; });

			if (playables.empty()) {
				ui::ColoredText(ui::kBad, "No playable livings registered.");
			} else {
				for (auto& p : playables) {
					if (ui::SecondaryButton(p.name.c_str())) {
						if (g.connectAs(p.id)) g.enterPlaying();
					}
					if (!p.desc.empty()) ui::Hint("%s", p.desc.c_str());
					ui::VerticalSpace(4.0f);
				}
			}

			if (!g.m_connectError.empty()) {
				ui::VerticalSpace();
				ui::ColoredText(ui::kBad, "%s", g.m_connectError.c_str());
			}

			ui::VerticalSpace();
			if (ui::GhostButton("Back")) g.m_menuScreen = MenuScreen::Singleplayer;
		}
		ui::EndScreen();
		break;
	}
	case MenuScreen::Multiplayer: {
		opts.panelW = 440.0f;
		if (ui::BeginScreen("##menu_mp", g.m_rhi, opts)) {
			ui::SectionHeader("Multiplayer");
			ui::Hint("LAN discovery, saved servers, and direct connect arrive in Stage C.");
			ui::VerticalSpace();
			ui::Hint("For now, launch with:  civcraft-ui-vk --host HOST --port PORT");
			ui::VerticalSpace();
			if (ui::GhostButton("Back")) g.m_menuScreen = MenuScreen::Main;
		}
		ui::EndScreen();
		break;
	}
	case MenuScreen::Settings: {
		opts.panelW = 440.0f;
		if (ui::BeginScreen("##menu_settings", g.m_rhi, opts)) {
			menuControlsBlock();
			ui::VerticalSpace();
			ui::Hint("Render tuning is still on F6 in-game — it'll move here in Stage B4.");
			ui::VerticalSpace();
			if (ui::GhostButton("Back")) g.m_menuScreen = MenuScreen::Main;
		}
		ui::EndScreen();
		break;
	}
	}
}

void MenuRenderer::renderGameMenu() {
	Game& g = game_;
	ui::ScreenTitle(g.m_rhi, "Game Is Not Paused", 0.28f, 3.0f, g.m_menuTitleT);

	ui::ScreenOpts opts;
	opts.panelW  = 340.0f;
	opts.anchorY = 0.48f;
	opts.scrimColor = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

	static bool showLog = false;

	if (ui::BeginScreen("##gamemenu", g.m_rhi, opts)) {
		if (ui::PrimaryButton("RESUME")) g.closeGameMenu();
		if (ui::SecondaryButton(showLog ? "HIDE GAME LOG" : "GAME LOG")) showLog = !showLog;
		if (ui::SecondaryButton("MAIN MENU")) g.enterMenu();
		ui::VerticalSpace();
		if (ui::DangerButton("QUIT")) g.m_shouldQuit = true;
	}
	ui::EndScreen();

	if (showLog) {
		auto lines = civcraft::GameLogger::instance().snapshot();
		ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowSize(ImVec2(760, 440), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(
			ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
			ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
		if (ImGui::Begin("Game Log", &showLog)) {
			if (auto* f = ui::mono()) ImGui::PushFont(f);
			for (auto& line : lines) {
				ImVec4 col = ui::kText;
				if      (line.find("[DECIDE]") != std::string::npos) col = ui::kOk;
				else if (line.find("[COMBAT]") != std::string::npos) col = ui::kBad;
				else if (line.find("[DEATH]")  != std::string::npos) col = ImVec4(0.95f, 0.25f, 0.25f, 1.0f);
				else if (line.find("[ACTION]") != std::string::npos) col = ImVec4(0.55f, 0.75f, 0.95f, 1.0f);
				else if (line.find("[INV]")    != std::string::npos) col = ui::kAccent;
				ImGui::TextColored(col, "%s", line.c_str());
			}
			if (ui::mono()) ImGui::PopFont();
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::End();
	}
}

void MenuRenderer::renderDeath() {
	Game& g = game_;
	ui::ScreenTitleRed(g.m_rhi, "YOU DIED", 0.22f, 3.0f);

	ui::ScreenOpts opts;
	opts.panelW  = 360.0f;
	opts.anchorY = 0.48f;
	opts.scrimColor = ImVec4(0.0f, 0.0f, 0.0f, 0.65f);

	if (ui::BeginScreen("##dead", g.m_rhi, opts)) {
		if (!g.m_lastDeathReason.empty()) {
			ui::ColoredText(ui::kBad, "%s", g.m_lastDeathReason.c_str());
			ui::VerticalSpace();
		}
		ui::Hint("Press R to respawn, Esc for main menu.");
		ui::VerticalSpace();
		if (ui::PrimaryButton("RESPAWN")) g.respawn();
		if (ui::GhostButton("MAIN MENU")) g.enterMenu();
	}
	ui::EndScreen();
}

} // namespace civcraft::vk
