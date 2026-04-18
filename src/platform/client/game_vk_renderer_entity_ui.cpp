#include "client/game_vk_renderers.h"
#include "client/game_vk.h"
#include "client/ui_kit.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "client/network_server.h"
#include "net/server_interface.h"
#include "agent/agent_client.h"
#include "logic/entity.h"

namespace civcraft::vk {

// ─────────────────────────────────────────────────────────────────────────
// Entity inspect card — custom-drawn detail panel. Opens when the player
// clicks on a living; ESC closes. Panel is centered, always on top of
// gameplay (scrim underneath so the world dims).
// ─────────────────────────────────────────────────────────────────────────

namespace {

using namespace ui::color;

void drawKV(rhi::IRhi* r, float x, float y, float labelW,
            const char* key, const char* value, const float valueCol[4]) {
	r->drawText2D(key, x, y, 0.70f, kTextDim);
	r->drawText2D(value, x + labelW, y, 0.70f, valueCol);
}

std::string prettyName(const civcraft::Entity& e) {
	std::string n = e.def().display_name;
	if (n.empty()) {
		n = e.typeId();
		auto col = n.find(':');
		if (col != std::string::npos) n = n.substr(col + 1);
	}
	for (auto& c : n) if (c == '_') c = ' ';
	if (!n.empty()) n[0] = (char)toupper((unsigned char)n[0]);
	return n;
}

} // namespace

void EntityUiRenderer::renderEntityInspect() {
	Game& g = game_;
	civcraft::Entity* e = g.m_server->getEntity(g.m_inspectedEntity);
	if (!e) { g.m_inspectedEntity = 0; return; }

	rhi::IRhi* r = g.m_rhi;

	// Scrim dims the world behind the card.
	r->drawRect2D(-1.0f, -1.0f, 2.0f, 2.0f, kScrim);

	// ── Panel geometry ────────────────────────────────────────────────
	const float panelW = 0.86f;
	const float panelH = 1.30f;
	const float panelX = -panelW * 0.5f;
	const float panelY = -panelH * 0.5f;

	const float shadow[4] = {0.00f, 0.00f, 0.00f, 0.55f};
	const float fill[4]   = {0.09f, 0.07f, 0.06f, 0.97f};
	const float brass[4]  = {0.65f, 0.48f, 0.20f, 1.00f};
	ui::drawShadowPanel(r, panelX, panelY, panelW, panelH,
		shadow, fill, brass, 0.003f);

	// ── Title strip ───────────────────────────────────────────────────
	const float titleStripH = 0.090f;
	const float titleY = panelY + panelH - titleStripH;
	const float titleFill[4] = {0.14f, 0.10f, 0.07f, 0.95f};
	r->drawRect2D(panelX + 0.010f, titleY,
		panelW - 0.020f, titleStripH - 0.010f, titleFill);

	char title[128];
	std::snprintf(title, sizeof(title), "%s  #%u",
		prettyName(*e).c_str(), (unsigned)e->id());
	const float titleCol[4] = {1.0f, 0.85f, 0.45f, 1.0f};
	ui::drawCenteredTitle(r, title, 0.0f, titleY + 0.024f, 1.20f, titleCol);

	// Close hint
	const float closeDim[4] = {0.70f, 0.65f, 0.55f, 0.90f};
	r->drawText2D("[Esc] close",
		panelX + panelW - 0.18f, titleY + 0.030f, 0.62f, closeDim);

	// ── Body layout ───────────────────────────────────────────────────
	const float bodyX     = panelX + 0.040f;
	const float bodyRight = panelX + panelW - 0.040f;
	const float labelW    = 0.21f;  // key column width
	float y = titleY - 0.050f;

	auto sectionHeader = [&](const char* label) {
		const float brassSec[4] = {0.95f, 0.78f, 0.35f, 1.00f};
		r->drawText2D(label, bodyX, y, 0.80f, brassSec);
		y -= 0.020f;
		const float rule[4] = {0.45f, 0.32f, 0.14f, 0.85f};
		r->drawRect2D(bodyX, y, bodyRight - bodyX, 0.0015f, rule);
		y -= 0.016f;
	};

	auto rowKV = [&](const char* k, const char* v, const float col[4]) {
		drawKV(r, bodyX, y, labelW, k, v, col);
		y -= 0.032f;
	};

	char buf[256];

	// ── Vitals ────────────────────────────────────────────────────────
	sectionHeader("Vitals");
	int curHP = e->hp();
	int maxHP = e->def().max_hp;
	if (maxHP > 0) {
		std::snprintf(buf, sizeof(buf), "%d / %d", curHP, maxHP);
		const float white[4] = {0.92f, 0.90f, 0.88f, 1.0f};
		rowKV("HP", buf, white);
		// HP bar under the row
		y += 0.014f;
		const float hpBg[4]  = {0.12f, 0.10f, 0.10f, 0.95f};
		const float hpFull[4]= {0.40f, 0.85f, 0.40f, 1.0f};
		const float hpMid[4] = {0.90f, 0.78f, 0.25f, 1.0f};
		const float hpLow[4] = {0.90f, 0.35f, 0.28f, 1.0f};
		float frac = (float)curHP / (float)maxHP;
		const float* bar = frac > 0.66f ? hpFull : (frac > 0.33f ? hpMid : hpLow);
		ui::drawMeter(r, bodyX + labelW, y, bodyRight - bodyX - labelW,
			0.012f, frac, bar, hpBg, brass);
		y -= 0.030f;
	} else {
		std::snprintf(buf, sizeof(buf), "%d", curHP);
		const float white[4] = {0.92f, 0.90f, 0.88f, 1.0f};
		rowKV("HP", buf, white);
	}

	std::snprintf(buf, sizeof(buf), "%.1f, %.1f, %.1f",
		e->position.x, e->position.y, e->position.z);
	rowKV("Position", buf, kText);

	std::snprintf(buf, sizeof(buf), "%s", e->typeId().c_str());
	rowKV("Type", buf, kText);

	// Behavior + Goal
	if (e->def().isLiving()) {
		std::string bid = e->getProp<std::string>(civcraft::Prop::BehaviorId, "");
		if (bid.empty())
			rowKV("Behavior", "(none — missing in artifact)", kDanger);
		else
			rowKV("Behavior", bid.c_str(), kText);

		if (e->goalText.empty()) {
			rowKV("Goal", "(pending)", kTextDim);
		} else {
			const float ok[4] = {0.55f, 0.95f, 0.55f, 1.0f};
			rowKV("Goal", e->goalText.c_str(),
				e->hasError ? kDanger : ok);
		}
		if (e->hasError && !e->errorText.empty()) {
			std::snprintf(buf, sizeof(buf), "Error: %s", e->errorText.c_str());
			r->drawText2D(buf, bodyX, y, 0.68f, kDanger);
			y -= 0.032f;
		}
	}

	// ── Agent plan ────────────────────────────────────────────────────
	if (g.m_agentClient) {
		auto pp = g.m_agentClient->getPlanProgress(e->id());
		if (pp.registered) {
			y -= 0.010f;
			sectionHeader("Agent");
			std::snprintf(buf, sizeof(buf), "step %d / %d",
				pp.stepIndex + 1, pp.totalSteps);
			rowKV("Plan", buf, kText);
			if (auto* viz = g.m_agentClient->getPlanViz(e->id())) {
				if (viz->hasAction) {
					const char* t = "Move";
					switch (viz->actionType) {
						case civcraft::PlanStep::Move:     t = "Move"; break;
						case civcraft::PlanStep::Harvest:  t = "Harvest"; break;
						case civcraft::PlanStep::Attack:   t = "Attack"; break;
						case civcraft::PlanStep::Relocate: t = "Relocate"; break;
					}
					std::snprintf(buf, sizeof(buf), "%s @ %.1f, %.1f, %.1f",
						t, viz->actionPos.x, viz->actionPos.y, viz->actionPos.z);
					rowKV("Action", buf, kText);
				}
				std::snprintf(buf, sizeof(buf), "%zu", viz->waypoints.size());
				rowKV("Waypoints", buf, kText);
			}
			std::snprintf(buf, sizeof(buf), "%.1f/min", pp.decideRatePerMin);
			rowKV("Decide rate", buf, kText);
			if (pp.stuckAccum > 0.1f) {
				const float warn[4] = {0.95f, 0.75f, 0.20f, 1.0f};
				std::snprintf(buf, sizeof(buf), "stuck: %.1fs", pp.stuckAccum);
				rowKV("Status", buf, warn);
			}
		}
	}

	// ── Ownership ─────────────────────────────────────────────────────
	y -= 0.010f;
	sectionHeader("Ownership");
	int owner = e->getProp<int>(civcraft::Prop::Owner, 0);
	civcraft::EntityId myId = g.m_server->localPlayerId();
	const float okCol[4] = {0.55f, 0.95f, 0.55f, 1.0f};
	if (owner == (int)myId)
		rowKV("Owner", "you", okCol);
	else if (owner != 0) {
		std::snprintf(buf, sizeof(buf), "player #%d", owner);
		rowKV("Owner", buf, kText);
	} else {
		rowKV("Owner", "world", kTextDim);
	}

	// ── Definition ────────────────────────────────────────────────────
	y -= 0.010f;
	sectionHeader("Definition");
	const auto& def = e->def();
	std::snprintf(buf, sizeof(buf), "%.1f", def.walk_speed);
	rowKV("walk_speed", buf, kText);
	std::snprintf(buf, sizeof(buf), "%.1f", def.run_speed);
	rowKV("run_speed", buf, kText);
	std::snprintf(buf, sizeof(buf), "%d", def.max_hp);
	rowKV("max_hp", buf, kText);
	std::snprintf(buf, sizeof(buf), "%.2f", def.gravity_scale);
	rowKV("gravity_scale", buf, kText);
	std::snprintf(buf, sizeof(buf), "(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)",
		def.collision_box_min.x, def.collision_box_min.y, def.collision_box_min.z,
		def.collision_box_max.x, def.collision_box_max.y, def.collision_box_max.z);
	rowKV("collision", buf, kText);
}

// ─────────────────────────────────────────────────────────────────────────
// RTS box selection rectangle
// ─────────────────────────────────────────────────────────────────────────
void EntityUiRenderer::renderRTSSelect() {
	Game& g = game_;
	if (g.m_cam.mode != civcraft::CameraMode::RTS) return;

	for (auto eid : g.m_rtsSelect.selected) {
		civcraft::Entity* e = g.m_server->getEntity(eid);
		if (!e) continue;
		glm::vec3 ndc;
		if (!g.projectWorld(e->position + glm::vec3(0, 0.1f, 0), ndc)) continue;
		float rad = 0.025f;
		const float selColor[4] = {0.4f, 0.8f, 1.0f, 0.7f};
		g.m_rhi->drawRect2D(ndc.x - rad, ndc.y - rad * 0.5f, rad * 2, rad, selColor);
	}

	if (!g.m_rtsSelect.dragging) return;
	float x0 = std::min(g.m_rtsSelect.start.x, g.m_rtsSelect.end.x);
	float x1 = std::max(g.m_rtsSelect.start.x, g.m_rtsSelect.end.x);
	float y0 = std::min(g.m_rtsSelect.start.y, g.m_rtsSelect.end.y);
	float y1 = std::max(g.m_rtsSelect.start.y, g.m_rtsSelect.end.y);

	const float rectFill[4] = {0.39f, 0.78f, 1.0f, 0.12f};
	g.m_rhi->drawRect2D(x0, y0, x1 - x0, y1 - y0, rectFill);
	const float edge[4] = {0.39f, 0.78f, 1.0f, 0.75f};
	ui::drawOutline(g.m_rhi, x0, y0, x1 - x0, y1 - y0, 0.003f, edge);
}

} // namespace civcraft::vk
