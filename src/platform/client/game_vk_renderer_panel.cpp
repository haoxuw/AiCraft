#include "client/game_vk_renderers.h"
#include "client/game_vk.h"
#include "client/ui_kit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "client/raycast.h"
#include "client/network_server.h"
#include "net/server_interface.h"
#include "logic/action.h"

namespace solarium::vk {

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

	solarium::Entity* me = g.playerEntity();
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
	auto hit = solarium::raycastBlocks(g.m_server->chunks(), eye, dir, 10.0f);
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

	if (g.m_cam.mode == solarium::CameraMode::RTS) {
		size_t activeWaypoints = 0;
		for (const auto& [eid, mo] : g.m_moveOrders)
			if (mo.active) activeWaypoints++;
		snprintf(buf, sizeof(buf), "Waypoints: %zu active", activeWaypoints);
		const float blue[4] = {0.35f, 0.75f, 1.0f, 0.95f};
		g.m_rhi->drawText2D(buf, x, y, 0.60f, blue); y -= step;
	}
}

// ─────────────────────────────────────────────────────────────────────────
// F6 tuning panel — sliders for GradingParams (post-process look)
//
// Tracks are clickable + draggable: press within the track sets the value
// from cursor x; holding keeps updating while the mouse is down. Presets
// snap the whole GradingParams to one of the rhi::IRhi::GradingParams
// named factories (Vivid / Dungeons / neutral reset).
// ─────────────────────────────────────────────────────────────────────────
void PanelRenderer::renderTuningPanel() {
	Game& g = game_;
	rhi::IRhi* r = g.m_rhi;
	using namespace ui::color;

	// Panel geometry — top-right, fits beside the inventory button column.
	const float panelW = 0.58f;
	const float panelH = 0.92f;
	const float panelX = 1.0f - panelW - 0.020f;
	const float panelY = 0.50f - panelH * 0.5f;

	const float shadow[4] = {0.00f, 0.00f, 0.00f, 0.55f};
	const float fill[4]   = {0.08f, 0.07f, 0.08f, 0.96f};
	const float brass[4]  = {0.65f, 0.48f, 0.20f, 1.00f};
	ui::drawShadowPanel(r, panelX, panelY, panelW, panelH,
		shadow, fill, brass, 0.003f);

	// Title strip.
	const float titleH = 0.075f;
	const float titleY = panelY + panelH - titleH;
	const float titleFill[4] = {0.14f, 0.10f, 0.07f, 0.95f};
	r->drawRect2D(panelX + 0.010f, titleY, panelW - 0.020f, titleH - 0.010f, titleFill);
	const float titleCol[4] = {1.0f, 0.85f, 0.45f, 1.0f};
	ui::drawCenteredTitle(r, "Look & Tuning",
		panelX + panelW * 0.5f, titleY + 0.020f, 1.05f, titleCol);
	const float closeDim[4] = {0.70f, 0.65f, 0.55f, 0.90f};
	r->drawText2D("[F6] close",
		panelX + panelW - 0.17f, titleY + 0.026f, 0.60f, closeDim);

	// Body layout.
	const float bodyX     = panelX + 0.035f;
	const float bodyRight = panelX + panelW - 0.035f;
	const float labelW    = 0.16f;
	const float valueW    = 0.06f;
	const float trackX    = bodyX + labelW;
	const float trackW    = bodyRight - bodyX - labelW - valueW;
	const float trackH    = 0.012f;
	const float rowStep   = 0.055f;
	float y = titleY - 0.050f;

	// Slider — click/drag within the track sets the value.
	auto slider = [&](const char* label, float* v, float vmin, float vmax) {
		// Label + numeric value
		r->drawText2D(label, bodyX, y, 0.65f, kTextDim);
		char num[32];
		std::snprintf(num, sizeof(num), "%.2f", *v);
		r->drawText2D(num, bodyRight - valueW + 0.005f, y, 0.65f, kText);

		float tY = y + 0.004f;
		const float trkBg[4]   = {0.05f, 0.05f, 0.06f, 1.00f};
		const float trkFill[4] = {0.55f, 0.75f, 0.95f, 0.90f};
		float frac = (*v - vmin) / (vmax - vmin);
		if (frac < 0.0f) frac = 0.0f;
		if (frac > 1.0f) frac = 1.0f;
		ui::drawMeter(r, trackX, tY, trackW, trackH, frac, trkFill, trkBg, brass);

		// Knob (square, centered on frac).
		const float knobW = 0.010f;
		const float knobH = 0.028f;
		float knobX = trackX + trackW * frac - knobW * 0.5f;
		float knobY = tY + trackH * 0.5f - knobH * 0.5f;
		const float knobCol[4] = {0.98f, 0.84f, 0.40f, 1.0f};
		r->drawRect2D(knobX, knobY, knobW, knobH, knobCol);

		// Hit-test: a generous Y band around the track; click or drag updates.
		const float hitPadY = 0.010f;
		bool inBand = g.m_mouseNdcX >= trackX && g.m_mouseNdcX <= trackX + trackW
		           && g.m_mouseNdcY >= tY - hitPadY
		           && g.m_mouseNdcY <= tY + trackH + hitPadY;
		if ((g.m_mouseLPressed && inBand) || (g.m_mouseLHeld && inBand)) {
			float nf = (g.m_mouseNdcX - trackX) / trackW;
			if (nf < 0.0f) nf = 0.0f;
			if (nf > 1.0f) nf = 1.0f;
			*v = vmin + nf * (vmax - vmin);
		}
		y -= rowStep;
	};

	slider("SSAO",       &g.m_grading.ssao,       0.0f, 1.0f);
	slider("Bloom",      &g.m_grading.bloom,      0.0f, 1.0f);
	slider("Vignette",   &g.m_grading.vignette,   0.0f, 0.5f);
	slider("ACES",       &g.m_grading.aces,       0.0f, 1.0f);
	slider("Exposure",   &g.m_grading.exposure,   0.3f, 1.5f);
	slider("Warm Tint",  &g.m_grading.warmTint,   0.0f, 1.0f);
	slider("S-Curve",    &g.m_grading.sCurve,     0.0f, 0.2f);
	slider("Saturation", &g.m_grading.saturation, -1.0f, 1.0f);

	// Preset buttons row.
	y -= 0.010f;
	const float btnH = 0.044f;
	const float btnGap = 0.012f;
	const float btnW = (bodyRight - bodyX - btnGap * 2) / 3.0f;
	auto button = [&](float bx, const char* label,
	                  rhi::IRhi::GradingParams target) {
		bool hov = ui::rectContainsNdc(bx, y, btnW, btnH,
			g.m_mouseNdcX, g.m_mouseNdcY);
		const float bgIdle[4] = {0.14f, 0.13f, 0.12f, 0.95f};
		const float bgHov [4] = {0.24f, 0.20f, 0.12f, 0.98f};
		r->drawRect2D(bx, y, btnW, btnH, hov ? bgHov : bgIdle);
		ui::drawOutline(r, bx, y, btnW, btnH, 0.002f, brass);
		const float* lc = hov ? titleCol : kText;
		ui::drawCenteredText(r, label, bx + btnW * 0.5f, y + btnH * 0.5f - 0.010f,
			0.75f, lc);
		if (hov && g.m_mouseLPressed) g.m_grading = target;
	};
	button(bodyX,                           "Vivid",    rhi::IRhi::GradingParams::Vivid());
	button(bodyX + btnW + btnGap,           "Dungeons", rhi::IRhi::GradingParams::Dungeons());
	button(bodyX + (btnW + btnGap) * 2,     "Reset",    rhi::IRhi::GradingParams{});
}

// ─────────────────────────────────────────────────────────────────────────
} // namespace solarium::vk
