#include "client/game_vk_renderers.h"
#include "client/game_vk.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>

#include "client/ui/ui_screen.h"
#include "client/ui/ui_theme.h"
#include "client/ui/ui_widgets.h"
#include "client/network_server.h"
#include "client/raycast.h"
#include "net/server_interface.h"
#include "logic/action.h"
#include "logic/inventory.h"
#include "agent/agent_client.h"

namespace civcraft::vk {

// ─────────────────────────────────────────────────────────────────────────
// Entity inspection overlay
// ─────────────────────────────────────────────────────────────────────────

void EntityUiRenderer::renderEntityInspect() {
	Game& g = game_;
	civcraft::Entity* e = g.m_server->getEntity(g.m_inspectedEntity);
	if (!e) { g.m_inspectedEntity = 0; return; }

	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(
		ImVec2(g.m_fbW * 0.5f, g.m_fbH * 0.5f), ImGuiCond_FirstUseEver,
		ImVec2(0.5f, 0.5f));

	std::string displayName = e->def().display_name;
	if (displayName.empty()) {
		displayName = e->typeId();
		auto col = displayName.find(':');
		if (col != std::string::npos) displayName = displayName.substr(col + 1);
	}
	for (auto& c : displayName) if (c == '_') c = ' ';
	if (!displayName.empty()) displayName[0] = (char)toupper((unsigned char)displayName[0]);
	std::string title = displayName + " #" + std::to_string(e->id()) + "###inspect";

	bool open = true;
	if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse)) {

		// ── Vitals ────────────────────────────────────────────────────
		int curHP = e->hp();
		int maxHP = e->def().max_hp;
		ui::SectionHeader("Vitals");
		if (maxHP > 0) {
			float frac = (float)curHP / (float)maxHP;
			ui::KeyValue("HP", "%d / %d", curHP, maxHP);
			ui::Meter(frac);
		} else {
			ui::KeyValue("HP", "%d", curHP);
		}
		ui::KeyValue("Position", "%.1f, %.1f, %.1f",
			e->position.x, e->position.y, e->position.z);
		ui::KeyValue("Entity ID", "%u", (unsigned)e->id());
		ui::KeyValue("Type", "%s", e->typeId().c_str());

		// Behavior + Goal are shown together so it's obvious every living has
		// both — the behavior module that's deciding, and the human-readable
		// goal it most recently emitted. Red goal = decide() raised.
		if (e->def().isLiving()) {
			std::string bid = e->getProp<std::string>(civcraft::Prop::BehaviorId, "");
			if (bid.empty()) {
				ImGui::PushStyleColor(ImGuiCol_Text, ui::kBad);
				ui::KeyValue("Behavior", "(none — missing in artifact)");
				ImGui::PopStyleColor();
			} else {
				ui::KeyValue("Behavior", "%s", bid.c_str());
			}
		}
		if (!e->goalText.empty()) {
			ImVec4 goalCol = e->hasError ? ui::kBad : ui::kOk;
			ImGui::PushStyleColor(ImGuiCol_Text, goalCol);
			ui::KeyValue("Goal", "%s", e->goalText.c_str());
			ImGui::PopStyleColor();
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, ui::kTextOff);
			ui::KeyValue("Goal", "(pending)");
			ImGui::PopStyleColor();
		}

		if (e->hasError && !e->errorText.empty()) {
			ui::VerticalSpace(4.0f);
			ui::ColoredText(ui::kBad, "Error: %s", e->errorText.c_str());
		}

		// ── Agent (plan + decide state) ───────────────────────────────
		if (g.m_agentClient) {
			auto pp = g.m_agentClient->getPlanProgress(e->id());
			if (pp.registered) {
				ui::VerticalSpace();
				ui::SectionHeader("Agent");
				std::string bid = e->getProp<std::string>(civcraft::Prop::BehaviorId, "");
				ui::KeyValue("behavior", "%s", bid.empty() ? "(none)" : bid.c_str());
				ui::KeyValue("plan", "step %d / %d", pp.stepIndex + 1, pp.totalSteps);
				if (auto* viz = g.m_agentClient->getPlanViz(e->id())) {
					if (viz->hasAction) {
						const char* t = "Move";
						switch (viz->actionType) {
							case civcraft::PlanStep::Move:     t = "Move"; break;
							case civcraft::PlanStep::Harvest:  t = "Harvest"; break;
							case civcraft::PlanStep::Attack:   t = "Attack"; break;
							case civcraft::PlanStep::Relocate: t = "Relocate"; break;
						}
						ui::KeyValue("action", "%s @ %.1f, %.1f, %.1f",
							t, viz->actionPos.x, viz->actionPos.y, viz->actionPos.z);
					}
					ui::KeyValue("waypoints", "%zu", viz->waypoints.size());
				}
				ui::KeyValue("since decide", "%.1fs", pp.timeSinceDecide);
				if (pp.stuckAccum > 0.1f)
					ui::ColoredText(ui::kWarn, "stuck: %.1fs", pp.stuckAccum);
				if (pp.overridePauseTimer > 0.01f)
					ui::ColoredText(ui::kWarn, "player override: %.1fs left", pp.overridePauseTimer);
			}
		}

		// ── Ownership ─────────────────────────────────────────────────
		ui::VerticalSpace();
		int owner = e->getProp<int>(civcraft::Prop::Owner, 0);
		civcraft::EntityId myId = g.m_server->localPlayerId();
		if (owner == (int)myId)
			ui::ColoredText(ui::kOk, "Owner: you");
		else if (owner != 0)
			ui::ColoredText(ui::kText, "Owner: player #%d", owner);
		else
			ui::ColoredText(ui::kTextDim, "Owner: world");

		// ── Definition ────────────────────────────────────────────────
		ui::VerticalSpace();
		ui::SectionHeader("Definition");
		const auto& def = e->def();
		ui::KeyValue("walk_speed", "%.1f", def.walk_speed);
		ui::KeyValue("run_speed", "%.1f", def.run_speed);
		ui::KeyValue("max_hp", "%d", def.max_hp);
		ui::KeyValue("gravity_scale", "%.2f", def.gravity_scale);
		ui::KeyValue("collision",
			"(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)",
			def.collision_box_min.x, def.collision_box_min.y, def.collision_box_min.z,
			def.collision_box_max.x, def.collision_box_max.y, def.collision_box_max.z);

		// ── All properties (raw) ──────────────────────────────────────
		ui::VerticalSpace();
		if (ImGui::CollapsingHeader("All Properties (Raw)")) {
			if (auto* f = ui::mono()) ImGui::PushFont(f);
			for (auto& [key, val] : e->props()) {
				if (auto* iv = std::get_if<int>(&val))
					ImGui::Text("%s = %d", key.c_str(), *iv);
				else if (auto* fv = std::get_if<float>(&val))
					ImGui::Text("%s = %.3f", key.c_str(), *fv);
				else if (auto* sv = std::get_if<std::string>(&val))
					ImGui::Text("%s = \"%s\"", key.c_str(), sv->c_str());
			}
			if (ui::mono()) ImGui::PopFont();
		}
	}
	ImGui::End();
	if (!open) g.m_inspectedEntity = 0;
}

// ─────────────────────────────────────────────────────────────────────────
// RTS box selection rectangle
// ─────────────────────────────────────────────────────────────────────────
void EntityUiRenderer::renderRTSSelect() {
	Game& g = game_;
	if (g.m_cam.mode != civcraft::CameraMode::RTS) return;

	// Draw selection highlight rings around selected units
	for (auto eid : g.m_rtsSelect.selected) {
		civcraft::Entity* e = g.m_server->getEntity(eid);
		if (!e) continue;
		glm::vec3 ndc;
		if (!g.projectWorld(e->position + glm::vec3(0, 0.1f, 0), ndc)) continue;
		float r = 0.025f;
		const float selColor[4] = {0.4f, 0.8f, 1.0f, 0.7f};
		g.m_rhi->drawRect2D(ndc.x - r, ndc.y - r * 0.5f, r * 2, r, selColor);
	}

	// Draw drag rectangle
	if (!g.m_rtsSelect.dragging) return;
	float x0 = std::min(g.m_rtsSelect.start.x, g.m_rtsSelect.end.x);
	float x1 = std::max(g.m_rtsSelect.start.x, g.m_rtsSelect.end.x);
	float y0 = std::min(g.m_rtsSelect.start.y, g.m_rtsSelect.end.y);
	float y1 = std::max(g.m_rtsSelect.start.y, g.m_rtsSelect.end.y);

	// Semi-transparent fill
	const float fill[4] = {0.39f, 0.78f, 1.0f, 0.12f};
	g.m_rhi->drawRect2D(x0, y0, x1 - x0, y1 - y0, fill);

	// Outline (4 edges)
	float t = 0.003f;
	const float edge[4] = {0.39f, 0.78f, 1.0f, 0.75f};
	g.m_rhi->drawRect2D(x0, y0, x1 - x0, t, edge);           // top
	g.m_rhi->drawRect2D(x0, y1 - t, x1 - x0, t, edge);       // bottom
	g.m_rhi->drawRect2D(x0, y0, t, y1 - y0, edge);            // left
	g.m_rhi->drawRect2D(x1 - t, y0, t, y1 - y0, edge);        // right
}

} // namespace civcraft::vk
