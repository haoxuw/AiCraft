#include "client/game_vk_renderers.h"
#include "client/game_vk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "client/raycast.h"
#include "client/network_server.h"
#include "net/server_interface.h"
#include "logic/action.h"

namespace civcraft::vk {

// ─────────────────────────────────────────────────────────────────────────
// F3 debug overlay — custom-drawn text column (top-left)
// ─────────────────────────────────────────────────────────────────────────
void PanelRenderer::renderDebugOverlay() {
	Game& g = game_;
	char buf[256];
	float x = -0.96f, y = 0.90f;
	float step = 0.035f;
	const float dim[4] = {0.85f, 0.85f, 0.90f, 0.95f};

	float fps = g.m_fpsDisplay;
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

// Phase-1 stub: render tuning panel is hidden until it's rebuilt
// custom-drawn. The sliders + presets live on m_grading; F6 still toggles
// m_showTuning but there's nothing to draw. Defaults are Vivid.
void PanelRenderer::renderTuningPanel() {
	Game& g = game_;
	(void)g;
	// TODO: custom-drawn sliders for SSAO/Bloom/Vignette/ACES/etc.
}

// Phase-1 stub: handbook is hidden until the tabbed browser is rebuilt
// custom-drawn. H toggles m_handbookOpen but there's nothing to draw.
void PanelRenderer::renderHandbook() {
	Game& g = game_;
	(void)g;
	// TODO: custom-drawn artifact browser with category tabs + detail pane.
}

} // namespace civcraft::vk
