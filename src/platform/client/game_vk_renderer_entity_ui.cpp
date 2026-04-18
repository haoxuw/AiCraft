#include "client/game_vk_renderers.h"
#include "client/game_vk.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "client/network_server.h"
#include "net/server_interface.h"

namespace civcraft::vk {

// Phase-1 stub: entity-inspect card will be rebuilt custom-drawn
// alongside the nameplates/HP bars. Clicking a living used to pop a
// detail window; for now it just clears the selection so the player
// isn't stuck with an empty inspect handle.
void EntityUiRenderer::renderEntityInspect() {
	Game& g = game_;
	civcraft::Entity* e = g.m_server->getEntity(g.m_inspectedEntity);
	if (!e) { g.m_inspectedEntity = 0; return; }
	// TODO: custom-drawn detail panel (vitals, behavior, plan, definition).
}

// ─────────────────────────────────────────────────────────────────────────
// RTS box selection rectangle — unchanged, pure drawRect2D
// ─────────────────────────────────────────────────────────────────────────
void EntityUiRenderer::renderRTSSelect() {
	Game& g = game_;
	if (g.m_cam.mode != civcraft::CameraMode::RTS) return;

	for (auto eid : g.m_rtsSelect.selected) {
		civcraft::Entity* e = g.m_server->getEntity(eid);
		if (!e) continue;
		glm::vec3 ndc;
		if (!g.projectWorld(e->position + glm::vec3(0, 0.1f, 0), ndc)) continue;
		float r = 0.025f;
		const float selColor[4] = {0.4f, 0.8f, 1.0f, 0.7f};
		g.m_rhi->drawRect2D(ndc.x - r, ndc.y - r * 0.5f, r * 2, r, selColor);
	}

	if (!g.m_rtsSelect.dragging) return;
	float x0 = std::min(g.m_rtsSelect.start.x, g.m_rtsSelect.end.x);
	float x1 = std::max(g.m_rtsSelect.start.x, g.m_rtsSelect.end.x);
	float y0 = std::min(g.m_rtsSelect.start.y, g.m_rtsSelect.end.y);
	float y1 = std::max(g.m_rtsSelect.start.y, g.m_rtsSelect.end.y);

	const float fill[4] = {0.39f, 0.78f, 1.0f, 0.12f};
	g.m_rhi->drawRect2D(x0, y0, x1 - x0, y1 - y0, fill);

	float t = 0.003f;
	const float edge[4] = {0.39f, 0.78f, 1.0f, 0.75f};
	g.m_rhi->drawRect2D(x0, y0, x1 - x0, t, edge);
	g.m_rhi->drawRect2D(x0, y1 - t, x1 - x0, t, edge);
	g.m_rhi->drawRect2D(x0, y0, t, y1 - y0, edge);
	g.m_rhi->drawRect2D(x1 - t, y0, t, y1 - y0, edge);
}

} // namespace civcraft::vk
