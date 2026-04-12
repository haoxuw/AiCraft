#include "client/game.h"
#include "imgui.h"

namespace modcraft {

// ============================================================
// Loading screen — wait for feet chunk before starting gameplay
// ============================================================
void Game::updateLoading(float dt, float aspect) {
	if (!m_server) { m_state = GameState::MENU; return; }
	m_server->tick(dt);
	if (!m_server->isConnected()) { m_state = GameState::MENU; return; }

	Entity* pe = playerEntity();
	if (!pe) {
		// Entity not yet received — keep ticking
		m_ui.beginFrame();
		float sw = (float)m_window.width(), sh = (float)m_window.height();
		ImDrawList* bg = ImGui::GetBackgroundDrawList();
		bg->AddRectFilled({0, 0}, {sw, sh}, IM_COL32(15, 18, 25, 255));
		bg->AddText({sw * 0.5f - 60, sh * 0.5f - 10}, IM_COL32(200, 200, 200, 255), "Connecting...");
		m_ui.endFrame();
		return;
	}

	// Check if the feet chunk is loaded
	int feetBx = (int)std::floor(pe->position.x);
	int feetBy = (int)std::floor(pe->position.y) - 1;
	int feetBz = (int)std::floor(pe->position.z);
	ChunkPos feetCp = {feetBx >> 4, feetBy >> 4, feetBz >> 4};
	bool feetLoaded = m_server->chunks().getChunk(feetCp) != nullptr;

	// Count loaded chunks for progress bar
	int totalChunks = 0, loadedChunks = 0;
	const int R = 3;
	for (int dy = -1; dy <= 1; dy++)
		for (int dz = -R; dz <= R; dz++)
			for (int dx = -R; dx <= R; dx++) {
				totalChunks++;
				ChunkPos cp = {feetCp.x + dx, feetCp.y + dy, feetCp.z + dz};
				if (m_server->chunks().getChunk(cp)) loadedChunks++;
			}

	float progress = totalChunks > 0 ? (float)loadedChunks / totalChunks : 0.0f;

	if (m_server->isServerReady() && feetLoaded && progress > 0.3f) {
		printf("[Game] Loading complete: %d/%d chunks (%.0f%%), entering gameplay\n",
			loadedChunks, totalChunks, progress * 100);
		m_state = GameState::PLAYING;
		m_camera.resetSmoothing();
		m_camera.resetMouseTracking();
		return;
	}

	// Render loading screen
	m_ui.beginFrame();
	float sw = (float)m_window.width(), sh = (float)m_window.height();
	ImDrawList* bg = ImGui::GetBackgroundDrawList();
	bg->AddRectFilled({0, 0}, {sw, sh}, IM_COL32(15, 18, 25, 255));

	// Title
	const char* title = "Loading World...";
	auto titleSize = ImGui::CalcTextSize(title);
	bg->AddText({(sw - titleSize.x) * 0.5f, sh * 0.4f}, IM_COL32(220, 220, 220, 255), title);

	// Progress bar
	float barW = 300, barH = 12;
	float barX = (sw - barW) * 0.5f, barY = sh * 0.5f;
	bg->AddRectFilled({barX, barY}, {barX + barW, barY + barH}, IM_COL32(40, 44, 55, 255), 4.0f);
	bg->AddRectFilled({barX, barY}, {barX + barW * progress, barY + barH},
		IM_COL32(80, 160, 255, 255), 4.0f);

	// Status text
	char status[64];
	snprintf(status, sizeof(status), "%d / %d chunks", loadedChunks, totalChunks);
	auto statusSize = ImGui::CalcTextSize(status);
	bg->AddText({(sw - statusSize.x) * 0.5f, barY + barH + 8}, IM_COL32(150, 150, 160, 255), status);

	m_ui.endFrame();
}

} // namespace modcraft
