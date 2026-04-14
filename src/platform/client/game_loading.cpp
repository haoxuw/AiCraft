#include "client/game.h"
#include "client/network_server.h"
#include "imgui.h"

namespace civcraft {

namespace {

// Per-milestone deadlines. Tuned conservative (world-gen can be slow on
// cold disk) but still small enough that a real hang is caught well
// before the user gives up. PREP uses a sliding deadline — any forward
// motion in preparingProgress() resets sincePct.
struct MilestoneCfg {
	const char* name;
	float       timeout;
};
constexpr MilestoneCfg kMilestones[] = {
	/* HELLO_ACK */ {"handshake",     5.0f},
	/* PREP      */ {"world gen",    10.0f},  // sliding; resets on pct advance
	/* WELCOME   */ {"player spawn", 10.0f},
	/* READY     */ {"finalizing",   10.0f},
};

} // namespace

// Abort the current handshake — hand off to the manual-reconnect modal.
void Game::abortHandshake(const char* reason) {
	m_handshake.lastReason = reason;
	printf("[Game] Handshake aborted at %s: %s\n",
		kMilestones[m_handshake.milestone].name, reason);
	enterDisconnected(reason);
}

// Drop into the manual-reconnect modal. Closes TCP, remembers the host/port
// so Reconnect can reuse them. Singleplayer's local server is a 127.0.0.1
// TCP peer so it uses the same path.
void Game::enterDisconnected(const char* reason) {
	m_disconnectReason = reason ? reason : "connection lost";
	if (m_server) { m_server->disconnect(); m_server.reset(); }
	m_reconnectAttempt = 0;  // manual from here on; reset any prior counter
	m_state = GameState::DISCONNECTED;
	glfwSetInputMode(m_window.handle(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

// Modal: dark bg + reason + [Reconnect] [Back to Menu].
void Game::updateDisconnected(float dt, float aspect) {
	(void)dt; (void)aspect;

	m_ui.beginFrame();
	float sw = (float)m_window.width(), sh = (float)m_window.height();
	ImDrawList* bg = ImGui::GetBackgroundDrawList();
	bg->AddRectFilled({0, 0}, {sw, sh}, IM_COL32(10, 12, 16, 240));

	float mw = 440, mh = 220;
	ImGui::SetNextWindowPos({(sw - mw) * 0.5f, (sh - mh) * 0.5f}, ImGuiCond_Always);
	ImGui::SetNextWindowSize({mw, mh}, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
	ImGui::Begin("Disconnected", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.40f, 1.0f));
	ImGui::TextUnformatted("Disconnected");
	ImGui::PopStyleColor();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::PushTextWrapPos(mw - 48);
	ImGui::TextWrapped("%s", m_disconnectReason.c_str());
	ImGui::PopTextWrapPos();

	if (!m_reconnectHost.empty()) {
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.65f, 1.0f));
		ImGui::Text("Server: %s:%d", m_reconnectHost.c_str(), m_reconnectPort);
		ImGui::PopStyleColor();
	}

	// Push buttons to bottom of modal
	float btnRowY = mh - 60;
	ImGui::SetCursorPosY(btnRowY);

	bool canReconnect = !m_reconnectHost.empty();
	if (!canReconnect) ImGui::BeginDisabled();
	if (ImGui::Button("Reconnect", {180, 32})) {
		std::string host = m_reconnectHost;
		int port = m_reconnectPort;
		m_disconnectReason.clear();
		joinServer(host, port, GameState::LOADING);
	}
	if (!canReconnect) ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button("Back to Menu", {180, 32})) {
		m_disconnectReason.clear();
		m_agentMgr.stopAll();  // tear down local server if any
		m_imguiMenu.setGameRunning(false);
		m_state = GameState::MENU;
	}

	ImGui::End();
	ImGui::PopStyleVar(2);
	m_ui.endFrame();
}

// ============================================================
// Loading screen — runs from right after C_HELLO through to
// S_READY, covering the server's async Preparing phase with a
// progress bar driven by S_PREPARING and a per-milestone watchdog.
// ============================================================
void Game::updateLoading(float dt, float aspect) {
	(void)aspect;
	if (!m_server) { m_state = GameState::MENU; return; }

	// Pre-welcome: drain S_PREPARING/S_CHUNK/S_ERROR from the buffer.
	if (auto* net = dynamic_cast<NetworkServer*>(m_server.get())) {
		if (net->localPlayerId() == ENTITY_NONE) {
			if (!net->pollWelcome() && !net->isConnected()) {
				abortHandshake(net->lastError().empty()
					? "connection lost during handshake"
					: net->lastError().c_str());
				return;
			}
			if (net->localPlayerId() != ENTITY_NONE)
				setupAfterConnect(m_connectTargetState);
		}
	}

	m_server->tick(dt);
	if (!m_server->isConnected()) {
		abortHandshake(m_server->lastError().empty()
			? "connection lost"
			: m_server->lastError().c_str());
		return;
	}

	// --- Milestone tracking ---
	// Monotonic: a milestone never moves backwards even if the observable
	// flickers (e.g. progress briefly reads -1 between phases).
	using M = HandshakeProgress::Milestone;
	M prev = m_handshake.milestone;
	M now  = prev;
	{
		bool hasPlayer = playerEntity() != nullptr;
		if (m_server->isServerReady())                now = M::DONE;
		else if (hasPlayer)                           now = std::max(prev, M::READY);
		else if (m_server->preparingProgress() >= 1)  now = std::max(prev, M::WELCOME);
		else if (m_server->preparingProgress() >= 0)  now = std::max(prev, M::PREP);
		else                                          now = std::max(prev, M::HELLO_ACK);
	}

	if (now == M::DONE) {
		printf("[Game] Loading complete (server ready), entering gameplay\n");
		m_state = GameState::PLAYING;
		m_camera.resetSmoothing();
		m_camera.resetMouseTracking();
		m_handshake.milestone = M::DONE;
		return;
	}

	if (now != prev) {
		m_handshake.milestone    = now;
		m_handshake.inMilestoneT = 0;
		m_handshake.sincePct     = 0;
		m_handshake.lastPct      = m_server->preparingProgress();
	} else {
		m_handshake.inMilestoneT += dt;
	}

	// Sliding deadline for PREP: any advance in pct resets sincePct.
	float pct = m_server->preparingProgress();
	if (now == M::PREP) {
		if (pct > m_handshake.lastPct + 0.001f) {
			m_handshake.lastPct  = pct;
			m_handshake.sincePct = 0;
		} else {
			m_handshake.sincePct += dt;
		}
	}

	float deadline = kMilestones[now].timeout;
	float elapsed  = (now == M::PREP) ? m_handshake.sincePct
	                                  : m_handshake.inMilestoneT;
	if (elapsed > deadline) {
		char buf[128];
		snprintf(buf, sizeof(buf), "timed out at %s (%.0fs)",
			kMilestones[now].name, deadline);
		abortHandshake(buf);
		return;
	}

	// --- Render ---
	m_ui.beginFrame();
	float sw = (float)m_window.width(), sh = (float)m_window.height();
	ImDrawList* bg = ImGui::GetBackgroundDrawList();
	bg->AddRectFilled({0, 0}, {sw, sh}, IM_COL32(15, 18, 25, 255));

	const char* title;
	float drawPct;
	switch (now) {
	case M::HELLO_ACK: title = "Connecting..."; drawPct = 0;             break;
	case M::PREP:      title = "Preparing World"; drawPct = pct;         break;
	case M::WELCOME:   title = "Spawning..."; drawPct = 1.0f;            break;
	case M::READY:     title = "Finalizing..."; drawPct = 1.0f;          break;
	default:           title = "Loading..."; drawPct = 0;                break;
	}
	auto titleSize = ImGui::CalcTextSize(title);
	bg->AddText({(sw - titleSize.x) * 0.5f, sh * 0.4f},
		IM_COL32(220, 220, 220, 255), title);

	float barW = 300, barH = 12;
	float barX = (sw - barW) * 0.5f, barY = sh * 0.5f;
	bg->AddRectFilled({barX, barY}, {barX + barW, barY + barH},
		IM_COL32(40, 44, 55, 255), 4.0f);
	if (drawPct > 0)
		bg->AddRectFilled({barX, barY}, {barX + barW * drawPct, barY + barH},
			IM_COL32(80, 160, 255, 255), 4.0f);

	char status[64];
	if (now == M::PREP)
		snprintf(status, sizeof(status), "%.0f%%", drawPct * 100.0f);
	else
		snprintf(status, sizeof(status), "%.1fs", m_handshake.inMilestoneT);
	auto statusSize = ImGui::CalcTextSize(status);
	bg->AddText({(sw - statusSize.x) * 0.5f, barY + barH + 8},
		IM_COL32(150, 150, 160, 255), status);

	// Half-deadline hint — user sees the operation is slow, not frozen.
	if (elapsed > deadline * 0.5f) {
		const char* hint = "Still waiting on server...";
		auto hintSize = ImGui::CalcTextSize(hint);
		bg->AddText({(sw - hintSize.x) * 0.5f, barY + barH + 28},
			IM_COL32(200, 140, 80, 255), hint);
	}

	m_ui.endFrame();
}

} // namespace civcraft
