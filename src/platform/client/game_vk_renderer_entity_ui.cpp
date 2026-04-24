#include "client/game_vk_renderers.h"
#include "client/game_vk.h"
#include "client/ui_kit.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "client/network_server.h"
#include "client/raycast.h"
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

	// Shared peek-input bundle — rebuilt per frame, reused by every
	// peekable-text row so the Game-dependency stays concentrated here.
	ui::PeekableTextInput peekIn{
		g.m_mouseNdcX, g.m_mouseNdcY, g.m_mouseLPressed,
		[&g](glm::ivec3 c) { g.enterCoordPeek(c); }
	};
	auto rowKV_peekable = [&](const char* k, const std::string& value,
	                          const float baseCol[4]) {
		r->drawText2D(k, bodyX, y, 0.70f, kTextDim);
		ui::drawPeekableText(r, peekIn, bodyX + labelW, y, 0.70f,
		                     value, baseCol);
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

	// Home — clickable peek target. No per-entity home tracking yet, so this
	// is the floored current position: always present on every Inspect and
	// serves as the manual-peek handle. When owned-entity home coords land,
	// swap this to the tracked value.
	{
		std::string home = "(" + std::to_string((int)std::floor(e->position.x))
		                 + ", " + std::to_string((int)std::floor(e->position.y))
		                 + ", " + std::to_string((int)std::floor(e->position.z))
		                 + ")";
		rowKV_peekable("Home", home, kText);
	}

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
			const float ok[4]    = {0.55f, 0.95f, 0.55f, 1.0f};
			const float* baseCol = e->hasError ? kDanger : ok;
			rowKV_peekable("Goal", e->goalText, baseCol);
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
	int ownerSeat = e->getProp<int>(civcraft::Prop::Owner, 0);
	uint32_t mySeat = g.m_server->localSeatId();
	const float okCol[4] = {0.55f, 0.95f, 0.55f, 1.0f};
	if (mySeat != 0 && ownerSeat == (int)mySeat)
		rowKV("Owner", "you", okCol);
	else if (ownerSeat != 0) {
		std::snprintf(buf, sizeof(buf), "seat #%d", ownerSeat);
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

// ─────────────────────────────────────────────────────────────────────────
// RTS drag-command: drag circle (while RMB held) and action wheel (on release)
// ─────────────────────────────────────────────────────────────────────────
void EntityUiRenderer::renderRTSDragCommand() {
	Game& g = game_;
	if (g.m_cam.mode != civcraft::CameraMode::RTS) return;
	const float kPi = 3.14159265f;
	float aspect = g.m_aspect > 0 ? g.m_aspect : 1.0f;

	// Smart cursor hint — while selection exists and no drag/wheel is active,
	// raycast under the cursor and show a one-word hint telling the player
	// what a quick RMB-drag (or LMB action) will do. Helps differentiate the
	// four wheel slices before the wheel even opens.
	if (!g.m_rtsSelect.selected.empty()
	    && !g.m_rtsDragCmd.active && !g.m_rtsWheel.active && g.m_server) {
		double mxD, myD;
		glfwGetCursorPos(g.m_window, &mxD, &myD);
		float fbWf = (float)g.m_fbW, fbHf = (float)g.m_fbH;
		if (fbWf > 0 && fbHf > 0) {
			float ndcX = (float)(mxD / fbWf) * 2.0f - 1.0f;
			float ndcY = 1.0f - (float)(myD / fbHf) * 2.0f;

			// pickViewProj() — matches the raycasts elsewhere (no Y-flip).
			glm::mat4 invVP = glm::inverse(g.pickViewProj());
			glm::vec4 nW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f); nW /= nW.w;
			glm::vec4 fW = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f); fW /= fW.w;
			glm::vec3 eye(nW);
			glm::vec3 dir = glm::normalize(glm::vec3(fW) - glm::vec3(nW));

			const char*  label = nullptr;
			const float  kGatherCol[4] = {0.55f, 0.95f, 0.55f, 0.95f};
			const float  kMineCol[4]   = {0.82f, 0.82f, 0.88f, 0.95f};
			const float  kAttackCol[4] = {1.00f, 0.45f, 0.45f, 0.95f};
			const float  kWalkCol[4]   = {0.98f, 0.78f, 0.24f, 0.85f};
			const float* col = kWalkCol;

			// Entities first — attack target wins over the block behind it.
			std::vector<civcraft::RaycastEntity> ents;
			g.m_server->forEachEntity([&](civcraft::Entity& e) {
				if (!e.def().isLiving()) return;
				if (e.def().hasTag("humanoid")) return;
				if (e.removed || e.hp() <= 0) return;
				ents.push_back({e.id(), e.typeId(), e.position,
					e.def().collision_box_min, e.def().collision_box_max,
					e.goalText, e.hasError});
			});
			auto hitEnt = civcraft::raycastEntities(ents, eye, dir, 48.0f,
				g.m_server->localPlayerId());
			if (hitEnt) {
				label = "ATTACK";
				col   = kAttackCol;
			} else {
				auto hit = civcraft::raycastBlocks(
					g.m_server->chunks(), eye, dir, 48.0f);
				if (hit) {
					const auto& bdef =
						g.m_server->blockRegistry().get(hit->blockId);
					const std::string& id = bdef.string_id;
					if (id == "leaves" || id == "logs" || id == "wood") {
						label = "GATHER"; col = kGatherCol;
					} else if (id == "stone"   || id == "cobblestone"
					        || id == "granite" || id == "marble"
					        || id == "sandstone") {
						label = "MINE"; col = kMineCol;
					} else {
						label = "WALK"; col = kWalkCol;
					}
				}
			}

			if (label) {
				float scale = 0.62f;
				float charW = 0.018f * scale;
				float w     = (float)std::strlen(label) * charW;
				float lx    = ndcX - w * 0.5f;
				float ly    = ndcY - 0.065f;
				g.m_rhi->drawText2D(label, lx, ly, scale, col);
			}
		}
	}

	// Drag-in-progress ring — sampled around the world circle and raycast
	// straight down to find ground height per sample, so the ring drapes
	// over hills/valleys. Segments whose endpoints project off-screen or
	// hit no terrain are skipped; occluders naturally break the ring.
	if (g.m_rtsDragCmd.active && g.m_rtsDragCmd.hasStartWorld
	    && g.m_rtsDragCmd.radiusWorld > 0.5f && g.m_server) {
		const int   N         = 48;
		const float thickness = 0.004f;
		float cx = g.m_rtsDragCmd.startWorld.x;
		float cz = g.m_rtsDragCmd.startWorld.z;
		float r  = g.m_rtsDragCmd.radiusWorld;
		auto& chunks = g.m_server->chunks();
		std::vector<glm::vec2> pts(N);
		std::vector<bool>      ok(N, false);
		for (int i = 0; i < N; i++) {
			float a  = (2.0f * kPi * (float)i) / (float)N;
			float wx = cx + r * std::cos(a);
			float wz = cz + r * std::sin(a);
			auto hit = civcraft::raycastBlocks(
				chunks, glm::vec3(wx, 256.0f, wz),
				glm::vec3(0.0f, -1.0f, 0.0f), 300.0f);
			if (!hit) continue;
			glm::vec3 surface(wx, (float)hit->blockPos.y + 1.02f, wz);
			glm::vec3 ndc;
			if (!g.projectWorld(surface, ndc)) continue;
			pts[i] = {ndc.x, ndc.y};
			ok[i]  = true;
		}
		std::vector<float> verts;
		verts.reserve((size_t)N * 24);
		for (int i = 0; i < N; i++) {
			int j = (i + 1) % N;
			if (!ok[i] || !ok[j]) continue;
			glm::vec2 a = pts[i], b = pts[j];
			glm::vec2 t = b - a;
			float L = glm::length(t);
			if (L < 1e-5f) continue;
			t /= L;
			glm::vec2 n(-t.y, t.x);
			glm::vec2 o  = n * (thickness * 0.5f);
			glm::vec2 a0 = a - o, a1 = a + o;
			glm::vec2 b0 = b - o, b1 = b + o;
			const float seg[] = {
				a0.x, a0.y, 0, 0,  b0.x, b0.y, 0, 0,  b1.x, b1.y, 0, 0,
				a0.x, a0.y, 0, 0,  b1.x, b1.y, 0, 0,  a1.x, a1.y, 0, 0,
			};
			verts.insert(verts.end(), std::begin(seg), std::end(seg));
		}
		if (!verts.empty()) {
			const float ring[4] = {0.98f, 0.78f, 0.24f, 0.90f};
			g.m_rhi->drawUi2D(verts.data(),
				(uint32_t)(verts.size() / 4), /*mode=*/1, ring);
		}
	}
	// Path preview — dashed line from each selected unit to the circle center,
	// so the player can see who will converge. Straight NDC line; not a real
	// navmesh preview (cheap, and the ring itself already indicates terrain).
	if (g.m_rtsDragCmd.active && g.m_rtsDragCmd.hasStartWorld
	    && !g.m_rtsSelect.selected.empty() && g.m_server) {
		glm::vec3 ndcCenter;
		if (g.projectWorld(g.m_rtsDragCmd.startWorld + glm::vec3(0, 0.05f, 0),
		                   ndcCenter)) {
			const float thickness = 0.0024f;
			const int   kDashes   = 8;
			const float dashFrac  = 0.55f;  // solid fraction per dash cell
			std::vector<float> verts;
			for (auto eid : g.m_rtsSelect.selected) {
				civcraft::Entity* e = g.m_server->getEntity(eid);
				if (!e) continue;
				glm::vec3 ndcUnit;
				if (!g.projectWorld(e->position + glm::vec3(0, 0.10f, 0),
				                    ndcUnit)) continue;
				glm::vec2 a{ndcUnit.x, ndcUnit.y};
				glm::vec2 b{ndcCenter.x, ndcCenter.y};
				glm::vec2 d = b - a;
				float L = glm::length(d);
				if (L < 1e-4f) continue;
				glm::vec2 t = d / L;
				glm::vec2 n(-t.y, t.x);
				glm::vec2 off = n * (thickness * 0.5f);
				for (int k = 0; k < kDashes; k++) {
					float u0 = (float)k / (float)kDashes;
					float u1 = u0 + dashFrac / (float)kDashes;
					glm::vec2 p0 = a + d * u0;
					glm::vec2 p1 = a + d * u1;
					glm::vec2 a0 = p0 - off, a1 = p0 + off;
					glm::vec2 b0 = p1 - off, b1 = p1 + off;
					const float seg[] = {
						a0.x, a0.y, 0, 0,  b0.x, b0.y, 0, 0,  b1.x, b1.y, 0, 0,
						a0.x, a0.y, 0, 0,  b1.x, b1.y, 0, 0,  a1.x, a1.y, 0, 0,
					};
					verts.insert(verts.end(), std::begin(seg), std::end(seg));
				}
			}
			if (!verts.empty()) {
				const float pathCol[4] = {0.98f, 0.78f, 0.24f, 0.55f};
				g.m_rhi->drawUi2D(verts.data(),
					(uint32_t)(verts.size() / 4), /*mode=*/1, pathCol);
			}
		}
	}

	// Cursor-endpoint dot — projected from the ground-anchored currentWorld,
	// so it sits on the terrain where the cursor ray hit, not on the cursor
	// sprite. Also renders during idle hover (no drag) when we have a world pt.
	if (g.m_rtsDragCmd.active && g.m_rtsDragCmd.hasStartWorld) {
		glm::vec3 ndcCur;
		if (g.projectWorld(g.m_rtsDragCmd.currentWorld + glm::vec3(0, 0.05f, 0),
		                   ndcCur)) {
			const float dot[4] = {1.0f, 0.9f, 0.4f, 0.90f};
			g.m_rhi->drawArc2D(ndcCur.x, ndcCur.y, 0.0f, 0.009f,
				0.0f, 2.0f * kPi, dot, aspect, 16);
		}
	}

	// Action wheel — four 90° slices (N Gather / E Attack / S Mine / W Cancel).
	if (g.m_rtsWheel.active) {
		const float kRIn  = 0.04f;
		const float kROut = 0.14f;
		float cx = g.m_rtsWheel.centerNdc.x;
		float cy = g.m_rtsWheel.centerNdc.y;

		// Dim disc at the cursor center so the hub reads against any backdrop.
		const float cap[4] = {0.0f, 0.0f, 0.0f, 0.55f};
		g.m_rhi->drawArc2D(cx, cy, 0.0f, kRIn,
			0.0f, 2.0f * kPi, cap, aspect, 32);

		// Shift-held → non-Cancel slices show "+GATHER" etc. signaling queue.
		bool q = g.m_rtsWheel.shiftQueue;
		struct Slice { float a0, a1; const char* label; float labelAng; };
		const Slice slices[4] = {
			{      kPi / 4,  3.0f * kPi / 4, q ? "+GATHER" : "GATHER",  kPi / 2 }, // 0 N
			{     -kPi / 4,         kPi / 4, q ? "+ATTACK" : "ATTACK",  0.0f    }, // 1 E
			{ -3.0f * kPi / 4,     -kPi / 4, q ? "+MINE"   : "MINE",   -kPi / 2 }, // 2 S
			{  3.0f * kPi / 4,  5.0f * kPi / 4, "CANCEL", kPi    }, // 3 W
		};
		for (int i = 0; i < 4; i++) {
			const Slice& s = slices[i];
			bool hover = (g.m_rtsWheel.hoverSlice == i);
			const float idle[4]      = {0.10f, 0.10f, 0.10f, 0.72f};
			const float hoverC[4]    = {0.39f, 0.78f, 1.0f,  0.88f};
			const float cancelIdle[4]  = {0.78f, 0.32f, 0.32f, 0.72f};
			const float cancelHover[4] = {1.00f, 0.45f, 0.45f, 0.92f};
			const float* col = (i == 3)
				? (hover ? cancelHover : cancelIdle)
				: (hover ? hoverC     : idle);
			g.m_rhi->drawArc2D(cx, cy, kRIn, kROut, s.a0, s.a1,
				col, aspect, 28);
		}

		// Labels — centered along slice midline.
		const float kLabelR = (kRIn + kROut) * 0.5f;
		const float scale   = 0.70f;
		const float charW   = 0.018f * scale;
		const float lineH   = 0.032f * scale;
		for (int i = 0; i < 4; i++) {
			const Slice& s = slices[i];
			float lx = cx + kLabelR * std::cos(s.labelAng);
			float ly = cy + kLabelR * aspect * std::sin(s.labelAng);
			float w  = (float)std::strlen(s.label) * charW;
			lx -= w * 0.5f;
			ly -= lineH * 0.5f;
			const float txtColor[4] = {1.0f, 1.0f, 1.0f, 0.96f};
			g.m_rhi->drawText2D(s.label, lx, ly, scale, txtColor);
		}
	}
}

} // namespace civcraft::vk
