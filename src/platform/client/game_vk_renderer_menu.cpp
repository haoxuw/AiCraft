#include "client/game_vk_renderers.h"
#include "client/game_vk.h"
#include "client/ui_kit.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "client/game_logger.h"
#include "client/network_server.h"
#include "logic/artifact_registry.h"
#include "net/server_interface.h"

namespace civcraft::vk {

// Phase-1 menus: custom-drawn, text-only. Arrow keys / WS move the
// highlight, Enter/Space activate, Esc backs out. Shared primitives live
// in ui_kit.{h,cpp} so the inventory, overlays, and menus agree on text
// metrics, outline style, and palette.

namespace {

using namespace ui::color;

struct MenuListInput {
	rhi::IRhi*   rhi;
	GLFWwindow*  window;
	float        mouseX;
	float        mouseY;
	bool         mouseReleased;
};

// Framed backdrop for a menu screen: shadow + dark fill + thick brass
// border + an inner hair-line so the panel reads as "chiseled metal"
// over the 3D plaza. Title strip above the content area.
void drawMenuFrame(rhi::IRhi* r, float x, float y, float w, float h,
                   const char* title) {
	const float shadow[4]    = {0.00f, 0.00f, 0.00f, 0.60f};
	const float fill[4]      = {0.07f, 0.06f, 0.06f, 0.94f};
	const float brass[4]     = {0.72f, 0.54f, 0.22f, 1.00f};
	const float brassIn[4]   = {0.95f, 0.78f, 0.35f, 0.85f};
	const float titleFill[4] = {0.14f, 0.10f, 0.07f, 0.95f};
	const float titleCol[4]  = {1.00f, 0.86f, 0.45f, 1.00f};

	ui::drawShadowPanel(r, x, y, w, h, shadow, fill, brass, 0.004f);
	// Inner brass pinstripe — sits 0.010 inside the outer border.
	ui::drawOutline(r, x + 0.010f, y + 0.010f,
	                w - 0.020f, h - 0.020f, 0.0015f, brassIn);

	if (title && *title) {
		const float titleH = 0.085f;
		const float ty = y + h - titleH - 0.016f;
		r->drawRect2D(x + 0.018f, ty, w - 0.036f, titleH, titleFill);
		ui::drawOutline(r, x + 0.018f, ty, w - 0.036f, titleH, 0.0012f, brassIn);
		ui::drawCenteredTitle(r, title, x + w * 0.5f, ty + 0.022f, 1.35f, titleCol);
	}
}

// Chunky bevel-style menu button. Dark fill, thick brass border; on
// hover/select the fill warms and a brass accent bar lights up on the
// left edge. Label text is centered, brass when active, off-white
// otherwise. Returns true the frame it's clicked.
bool drawMenuButton(rhi::IRhi* r, float x, float y, float w, float h,
                    const char* label, bool active,
                    float mouseX, float mouseY, bool mouseReleased) {
	bool hover = ui::rectContainsNdc(x, y, w, h, mouseX, mouseY);

	const float bgIdle[4] = {0.10f, 0.09f, 0.09f, 0.96f};
	const float bgHov [4] = {0.18f, 0.14f, 0.10f, 0.98f};
	const float bgSel [4] = {0.28f, 0.20f, 0.10f, 0.98f};
	const float brass [4] = {0.78f, 0.58f, 0.22f, 1.00f};
	const float brassLt[4]= {0.98f, 0.84f, 0.42f, 1.00f};
	const float txtIdle[4]= {0.85f, 0.82f, 0.78f, 1.00f};
	const float txtActv[4]= {1.00f, 0.88f, 0.48f, 1.00f};

	const float* bg = active ? bgSel : (hover ? bgHov : bgIdle);
	r->drawRect2D(x, y, w, h, bg);

	// Top inner highlight — 1-px warm line across the top, simulates bevel.
	const float hiTop[4] = {0.95f, 0.80f, 0.40f, 0.22f};
	r->drawRect2D(x + 0.004f, y + h - 0.005f, w - 0.008f, 0.002f, hiTop);
	// Bottom inner shadow — darker line for bevel complement.
	const float loBot[4] = {0.00f, 0.00f, 0.00f, 0.40f};
	r->drawRect2D(x + 0.004f, y + 0.003f, w - 0.008f, 0.002f, loBot);

	ui::drawOutline(r, x, y, w, h, 0.0025f, active ? brassLt : brass);

	// Left accent bar — only on active row, a warm rectangle glowing beside.
	if (active) {
		const float ac[4] = {0.98f, 0.80f, 0.30f, 1.00f};
		r->drawRect2D(x - 0.012f, y + 0.006f, 0.008f, h - 0.012f, ac);
		const float acGlow[4] = {0.98f, 0.80f, 0.30f, 0.18f};
		r->drawRect2D(x - 0.028f, y + 0.004f, 0.016f, h - 0.008f, acGlow);
	}

	ui::drawCenteredText(r, label, x + w * 0.5f,
		y + h * 0.5f - ui::kCharHNdc * 1.05f * 0.5f,
		1.05f, active ? txtActv : txtIdle);

	return hover && mouseReleased;
}

// Vertical list of chunky buttons. Returns activated index or -1.
int drawMenuList(const MenuListInput& in,
                 const std::vector<std::string>& items,
                 int& cursor, float cx, float topY, float rowH,
                 float rowW) {
	if (items.empty()) return -1;
	cursor = std::clamp(cursor, 0, (int)items.size() - 1);

	if (ui::keyEdge(in.window, GLFW_KEY_DOWN) || ui::keyEdge(in.window, GLFW_KEY_S))
		cursor = (cursor + 1) % (int)items.size();
	if (ui::keyEdge(in.window, GLFW_KEY_UP) || ui::keyEdge(in.window, GLFW_KEY_W))
		cursor = (cursor - 1 + (int)items.size()) % (int)items.size();

	int activated = -1;
	if (ui::keyEdge(in.window, GLFW_KEY_ENTER) || ui::keyEdge(in.window, GLFW_KEY_SPACE))
		activated = cursor;

	float x = cx - rowW * 0.5f;
	for (int i = 0; i < (int)items.size(); ++i) {
		float y = topY - i * (rowH + 0.014f);
		bool hover = ui::rectContainsNdc(x, y, rowW, rowH, in.mouseX, in.mouseY);
		if (hover) cursor = i;
		bool click = drawMenuButton(in.rhi, x, y, rowW, rowH,
			items[i].c_str(), (i == cursor),
			in.mouseX, in.mouseY, in.mouseReleased);
		if (click) activated = i;
	}
	return activated;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────
// Main menu (pre-game)
// ─────────────────────────────────────────────────────────────────────
void MenuRenderer::renderMenu() {
	Game& g = game_;
	rhi::IRhi* R = g.m_rhi;
	MenuListInput in{R, g.m_window, g.m_mouseNdcX, g.m_mouseNdcY, g.m_mouseLReleased};

	bool escPressed = ui::keyEdge(g.m_window, GLFW_KEY_ESCAPE);
	if (escPressed && g.m_menuScreen != MenuScreen::Main) {
		if (g.m_menuScreen == MenuScreen::Connecting) {
			if (g.m_server) g.m_server->disconnect();
			g.m_connecting = false;
			g.m_menuScreen = MenuScreen::CharacterSelect;
		} else {
			g.m_menuScreen = MenuScreen::Main;
		}
	}

	ui::drawCenteredTitle(R, "CIVCRAFT", 0.0f, 0.72f, 3.4f, ui::color::kBrass);

	switch (g.m_menuScreen) {
	case MenuScreen::Main: {
		drawMenuFrame(R, -0.30f, 0.02f, 0.60f, 0.56f, nullptr);
		static int cursor = 0;
		std::vector<std::string> items = {
			"SINGLEPLAYER", "MULTIPLAYER", "SETTINGS", "QUIT"
		};
		int picked = drawMenuList(in, items, cursor,
			0.0f, 0.46f, 0.090f, 0.50f);
		if (picked == 0) g.m_menuScreen = MenuScreen::Singleplayer;
		else if (picked == 1) g.m_menuScreen = MenuScreen::Multiplayer;
		else if (picked == 2) g.m_menuScreen = MenuScreen::Settings;
		else if (picked == 3) g.m_shouldQuit = true;

		if (!g.m_lastDeathReason.empty())
			ui::drawCenteredText(R, g.m_lastDeathReason.c_str(), 0.0f, -0.78f, 0.70f, kDanger);
		break;
	}
	case MenuScreen::Singleplayer: {
		drawMenuFrame(R, -0.36f, -0.32f, 0.72f, 0.82f, "Your Worlds");
		ui::drawCenteredText(R, "(world list coming in Stage C)",
			0.0f, 0.20f, 0.72f, kTextHint);

		static int cursor = 0;
		std::vector<std::string> items = {
			"START VILLAGE WORLD", "BACK"
		};
		int picked = drawMenuList(in, items, cursor,
			0.0f, 0.04f, 0.100f, 0.56f);
		if (picked == 0) {
			g.m_connectError.clear();
			g.m_menuScreen = MenuScreen::CharacterSelect;
			cursor = 0;
		} else if (picked == 1) {
			g.m_menuScreen = MenuScreen::Main;
			cursor = 0;
		}
		break;
	}
	case MenuScreen::CharacterSelect: {
		// Panel stays on the LEFT so the model preview has the right half.
		drawMenuFrame(R, -0.92f, -0.70f, 0.62f, 1.40f, "Choose Your Character");
		ui::drawCenteredText(R, "Arrow keys to pick, Enter to enter.",
			-0.61f, 0.48f, 0.65f, kTextDim);

		struct PlayableItem { std::string id, name; };
		std::vector<PlayableItem> playables;
		for (auto* e : g.m_artifactRegistry.byCategory("living")) {
			auto it = e->fields.find("playable");
			if (it == e->fields.end()) continue;
			if (it->second != "True" && it->second != "true") continue;
			playables.push_back({e->id, e->name});
		}
		std::sort(playables.begin(), playables.end(),
			[](const PlayableItem& a, const PlayableItem& b) { return a.name < b.name; });

		if (playables.empty()) {
			ui::drawCenteredText(R, "No playable livings registered.",
				-0.61f, 0.10f, 0.90f, kDanger);
		} else {
			std::vector<std::string> names;
			names.reserve(playables.size() + 1);
			for (auto& p : playables) names.push_back(p.name);
			names.push_back("BACK");

			static int cursor = 0;
			int picked = drawMenuList(in, names, cursor,
				-0.61f, 0.38f, 0.085f, 0.52f);

			if (cursor >= 0 && cursor < (int)playables.size())
				g.m_previewCreatureId = playables[cursor].id;

			if (picked >= 0 && picked < (int)playables.size()) {
				if (g.beginConnectAs(playables[picked].id))
					g.m_menuScreen = MenuScreen::Connecting;
			} else if (picked == (int)playables.size()) {
				g.m_menuScreen = MenuScreen::Singleplayer;
				cursor = 0;
			}
		}

		if (!g.m_connectError.empty())
			ui::drawCenteredText(R, g.m_connectError.c_str(),
				-0.61f, -0.62f, 0.70f, kDanger);
		break;
	}
	case MenuScreen::Connecting: {
		drawMenuFrame(R, -0.40f, -0.18f, 0.80f, 0.50f, "Entering World");
		int dots = (int)(g.m_wallTime * 2.0f) % 4;
		const char* dotStr[] = {"", ".", "..", "..."};
		char buf[64];
		std::snprintf(buf, sizeof(buf), "Streaming world%s", dotStr[dots]);
		ui::drawCenteredText(R, buf, 0.0f, 0.08f, 0.90f, kText);
		ui::drawCenteredText(R, "[Esc] Cancel", 0.0f, -0.06f, 0.70f, kTextDim);

		if (g.m_server && g.m_server->pollWelcome()) {
			g.m_connecting = false;
			g.enterPlaying();
		} else if (g.m_server && !g.m_server->isConnected()) {
			const std::string& err = g.m_server->lastError();
			g.m_connectError = err.empty() ? "connection lost" : err;
			g.m_connecting = false;
			g.m_menuScreen = MenuScreen::CharacterSelect;
		} else if (g.m_wallTime - g.m_connectStartTime > 60.0f) {
			if (g.m_server) g.m_server->disconnect();
			g.m_connectError = "timeout waiting for welcome (60s)";
			g.m_connecting = false;
			g.m_menuScreen = MenuScreen::CharacterSelect;
		}
		break;
	}
	case MenuScreen::Multiplayer: {
		drawMenuFrame(R, -0.44f, -0.28f, 0.88f, 0.72f, "Multiplayer");
		ui::drawCenteredText(R, "LAN browser coming in Stage C.",
			0.0f, 0.16f, 0.80f, kText);
		ui::drawCenteredText(R, "For now: civcraft-ui-vk --host HOST --port PORT",
			0.0f, 0.08f, 0.65f, kTextHint);

		static int cursor = 0;
		std::vector<std::string> items = { "BACK" };
		int picked = drawMenuList(in, items, cursor,
			0.0f, -0.08f, 0.100f, 0.40f);
		if (picked == 0) { g.m_menuScreen = MenuScreen::Main; cursor = 0; }
		break;
	}
	case MenuScreen::Settings: {
		drawMenuFrame(R, -0.40f, -0.42f, 0.80f, 1.00f, "Controls");

		struct KV { const char* k; const char* v; };
		const KV kvs[] = {
			{"Move",           "WASD"},
			{"Jump",           "Space"},
			{"Sprint",         "Shift"},
			{"Look",           "Mouse"},
			{"Attack / Place", "LMB / RMB"},
			{"Drop",           "Q"},
			{"Inventory",      "Tab"},
			{"Handbook",       "H"},
			{"Camera",         "V"},
			{"Pause",          "Esc"},
			{"Debug / Tuning", "F3 / F6"},
			{"Screenshot",     "F2"},
		};
		float y = 0.40f;
		for (const auto& kv : kvs) {
			char buf[96];
			std::snprintf(buf, sizeof(buf), "%-16s %s", kv.k, kv.v);
			ui::drawCenteredText(R, buf, 0.0f, y, 0.75f, kText);
			y -= 0.040f;
		}

		static int cursor = 0;
		std::vector<std::string> items = { "BACK" };
		int picked = drawMenuList(in, items, cursor,
			0.0f, -0.24f, 0.100f, 0.40f);
		if (picked == 0) { g.m_menuScreen = MenuScreen::Main; cursor = 0; }
		break;
	}
	}
}

// ─────────────────────────────────────────────────────────────────────
// In-game pause menu
// ─────────────────────────────────────────────────────────────────────
void MenuRenderer::renderGameMenu() {
	Game& g = game_;
	rhi::IRhi* R = g.m_rhi;
	MenuListInput in{R, g.m_window, g.m_mouseNdcX, g.m_mouseNdcY, g.m_mouseLReleased};

	R->drawRect2D(-1.0f, -1.0f, 2.0f, 2.0f, ui::color::kScrim);
	ui::drawCenteredTitle(R, "Game Is Not Paused", 0.0f, 0.70f, 2.4f, ui::color::kBrass);

	static bool showLog = false;

	if (!showLog) {
		drawMenuFrame(R, -0.28f, -0.30f, 0.56f, 0.86f, nullptr);
		static int cursor = 0;
		std::vector<std::string> items = {
			"RESUME", "GAME LOG", "MAIN MENU", "QUIT"
		};
		int picked = drawMenuList(in, items, cursor,
			0.0f, 0.40f, 0.100f, 0.48f);
		if (picked == 0) g.closeGameMenu();
		else if (picked == 1) showLog = true;
		else if (picked == 2) g.enterMenu();
		else if (picked == 3) g.m_shouldQuit = true;
	} else {
		auto lines = civcraft::GameLogger::instance().snapshot();
		const float logX = -0.82f, logW = 1.64f;
		const float logY = -0.40f, logH = 1.00f;
		const float logBg[4] = {0.02f, 0.02f, 0.03f, 0.92f};
		R->drawRect2D(logX, logY, logW, logH, logBg);

		const float kLogDecide[4] = { 0.55f, 0.95f, 0.55f, 1.0f };
		const float kLogDeath[4]  = { 0.95f, 0.25f, 0.25f, 1.0f };
		const float kLogAction[4] = { 0.55f, 0.75f, 0.95f, 1.0f };
		const float kLogInv[4]    = { 0.95f, 0.82f, 0.40f, 1.0f };

		float lineH = 0.030f;
		int maxLines = (int)(logH / lineH) - 1;
		int start = std::max(0, (int)lines.size() - maxLines);
		float y = logY + logH - lineH - 0.008f;
		for (int i = start; i < (int)lines.size(); ++i) {
			const std::string& ln = lines[i];
			const float* col = kText;
			if      (ln.find("[DECIDE]") != std::string::npos) col = kLogDecide;
			else if (ln.find("[COMBAT]") != std::string::npos) col = kDanger;
			else if (ln.find("[DEATH]")  != std::string::npos) col = kLogDeath;
			else if (ln.find("[ACTION]") != std::string::npos) col = kLogAction;
			else if (ln.find("[INV]")    != std::string::npos) col = kLogInv;
			R->drawText2D(ln.c_str(), logX + 0.012f, y, 0.60f, col);
			y -= lineH;
		}

		static int cursor = 0;
		std::vector<std::string> items = { "BACK" };
		int picked = drawMenuList(in, items, cursor,
			0.0f, -0.55f, 0.070f, 0.32f);
		if (picked == 0 || ui::keyEdge(g.m_window, GLFW_KEY_ESCAPE)) {
			showLog = false;
			cursor = 0;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────
// Death screen
// ─────────────────────────────────────────────────────────────────────
void MenuRenderer::renderDeath() {
	Game& g = game_;
	rhi::IRhi* R = g.m_rhi;
	MenuListInput in{R, g.m_window, g.m_mouseNdcX, g.m_mouseNdcY, g.m_mouseLReleased};

	R->drawRect2D(-1.0f, -1.0f, 2.0f, 2.0f, ui::color::kScrimDark);
	ui::drawCenteredTitle(R, "YOU DIED", 0.0f, 0.62f, 2.8f, ui::color::kRed);

	drawMenuFrame(R, -0.32f, -0.38f, 0.64f, 0.82f, nullptr);

	if (!g.m_lastDeathReason.empty())
		ui::drawCenteredText(R, g.m_lastDeathReason.c_str(), 0.0f, 0.28f, 0.85f, kDanger);

	ui::drawCenteredText(R, "[R] Respawn    [Esc] Main Menu",
		0.0f, 0.16f, 0.70f, kTextDim);

	static int cursor = 0;
	std::vector<std::string> items = { "RESPAWN", "MAIN MENU" };
	int picked = drawMenuList(in, items, cursor,
		0.0f, -0.02f, 0.100f, 0.46f);
	if (picked == 0) g.respawn();
	else if (picked == 1) g.enterMenu();
}

} // namespace civcraft::vk
