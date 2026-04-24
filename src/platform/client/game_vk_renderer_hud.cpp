#include "client/game_vk_renderers.h"
#include "client/game_vk.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "client/box_model_flatten.h"
#include "client/model_loader.h"
#include "client/network_server.h"
#include "client/raycast.h"
#include "net/server_interface.h"
#include "logic/action.h"

namespace civcraft::vk {

// ─────────────────────────────────────────────────────────────────────────
// HUD — lightbulbs, HP bars, player HP, crosshair
// ─────────────────────────────────────────────────────────────────────────

namespace {
// Character cell sizes in rhi_ui.cpp: kCharWNdc=0.018, kCharHNdc=0.032.
// Keep values in sync — if rhi_ui.cpp changes, mirror here.
constexpr float kCharWNdc = 0.018f;
constexpr float kCharHNdc = 0.032f;
}

// ── Named HUD colors ────────────────────────────────────────────────────
static constexpr glm::vec4 kCrosshair   {1.0f, 1.0f, 1.0f, 0.65f};
static constexpr glm::vec4 kTypeLabel   {0.70f, 0.70f, 0.70f, 0.65f};

void HudRenderer::renderHUD() {
	Game& g = game_;
	if (g.m_damageVignette > 0.0f) {
		float a = glm::clamp(g.m_damageVignette, 0.0f, 1.0f);
		float red[4] = { 0.85f, 0.08f, 0.08f, a * 0.35f };
		g.m_rhi->drawRect2D(-1.0f, -1.0f, 2.0f, 2.0f, red);
		float bandA[4] = { 1.0f, 0.12f, 0.10f, a * 0.55f };
		g.m_rhi->drawRect2D(-1.0f,  0.75f, 2.0f, 0.25f, bandA);
		g.m_rhi->drawRect2D(-1.0f, -1.0f,  2.0f, 0.25f, bandA);
		g.m_rhi->drawRect2D(-1.0f, -1.0f,  0.18f, 2.0f, bandA);
		g.m_rhi->drawRect2D( 0.82f,-1.0f,  0.18f, 2.0f, bandA);
	}

	// Crosshair — screen-center in both FPS and TPS (over-the-shoulder
	// shooter style). Hidden in RPG/RTS (cursor is a world ray, not a
	// screen-center reticle).
	if (g.m_cam.mode == civcraft::CameraMode::FirstPerson ||
	    g.m_cam.mode == civcraft::CameraMode::ThirdPerson) {
		float cx = 0.0f, cy = 0.0f;
		// Hitmarker flash: orange on damage, red on kill shot
		glm::vec4 chColor = kCrosshair;
		if (g.m_hitmarkerTimer > 0) {
			float blend = g.m_hitmarkerTimer / 0.18f;
			glm::vec3 flash = g.m_hitmarkerKill
				? glm::vec3(1.0f, 0.15f, 0.05f)
				: glm::vec3(1.0f, 0.80f, 0.25f);
			chColor = glm::vec4(
				glm::mix(1.0f, flash.r, blend),
				glm::mix(1.0f, flash.g, blend),
				glm::mix(1.0f, flash.b, blend),
				0.9f + blend * 0.1f);
		}
		g.m_rhi->drawRect2D(cx - 0.003f, cy - 0.006f,  0.006f, 0.012f, &chColor.x);
		g.m_rhi->drawRect2D(cx - 0.010f, cy - 0.0015f, 0.020f, 0.003f, &chColor.x);
	}

	// ── Entity lightbulb + goal label + HP bar + type label ─────────────
	// Decorations (lightbulb/goal/HP/type) are for Living only — items and
	// structures don't have goals, HP, or AI state worth surfacing.
	// F3-gated: lightbulb + goal + HP are diagnostic info. Type label stays
	// visible in normal play since it's how you tell NPCs apart at a glance.
	{
		EntityId myId = g.m_server->localPlayerId();
		g.m_server->forEachEntity([&](civcraft::Entity& e) {
			if (e.id() == myId) return;
			if (!e.def().isLiving()) return;
			glm::vec3 anchor = e.position + glm::vec3(0, 2.1f, 0);
			glm::vec3 ndc;
			if (!g.projectWorld(anchor, ndc)) return;

			bool broken = e.goalText.find("\xE2\x9A\xA0") != std::string::npos;
			float pulse = 1.0f + 0.08f * std::sin(g.m_wallTime * 3.2f + e.id() * 0.9f);
			float scale = 1.5f * pulse;
			float gw = kCharWNdc * scale;
			float gx = ndc.x - gw * 0.5f;
			float tint[4];
			tint[0] = 1.0f;
			tint[1] = broken ? 0.25f : 1.0f;
			tint[2] = broken ? 0.25f : 0.85f;
			tint[3] = 1.0f;

			if (g.m_showDebug) {
				g.m_rhi->drawTitle2D("!", gx, ndc.y, scale, tint);

				std::string label = e.goalText.empty() ? std::string("…") : e.goalText;
				if (label.size() > 40) label = label.substr(0, 39) + "…";
				float rawW = label.size() * kCharWNdc;
				float maxW = 0.50f;
				float lScale = rawW > maxW ? std::max(0.55f, maxW / rawW) : 0.85f;
				float lW = rawW * lScale;
				float lX = ndc.x - lW * 0.5f;
				float lY = ndc.y + kCharHNdc * scale + 0.020f;
				g.m_rhi->drawText2D(label.c_str(), lX, lY, lScale, tint);

				float tyW = e.typeId().size() * kCharWNdc * 0.6f;
				float tyX = ndc.x - tyW * 0.5f;
				float tyY = ndc.y - 0.038f;
				g.m_rhi->drawText2D(e.typeId().c_str(), tyX, tyY, 0.6f, &kTypeLabel.x);
			}
		});
	}

	// ── Interaction prompts near aimed target ──────────────────────────
	// Contextual hint hovering above the block/entity the player is looking
	// at in FPS/TPS. Modern games surface the action to the target, not
	// just a static tutorial bar. Hidden if any UI owns the cursor.
	if (!g.m_uiWantsCursor &&
	    (g.m_cam.mode == civcraft::CameraMode::FirstPerson ||
	     g.m_cam.mode == civcraft::CameraMode::ThirdPerson)) {
		glm::vec3 eye = g.m_cam.position;
		glm::vec3 dir = g.m_cam.front();
		EntityId myId = g.m_server->localPlayerId();

		auto& ents = g.m_scratch.ents;
		ents.clear();
		g.m_server->forEachEntity([&](civcraft::Entity& e) {
			if (!e.def().isLiving()) return;
			ents.push_back({e.id(), e.typeId(), e.position,
				e.def().collision_box_min, e.def().collision_box_max,
				e.goalText, e.hasError});
		});
		auto eHit = civcraft::raycastEntities(ents, eye, dir, 12.0f, myId);
		auto bHit = civcraft::raycastBlocks(g.m_server->chunks(), eye, dir, 6.0f);

		glm::vec3 anchor;
		std::string prompt;
		glm::vec3 color{0.95f, 0.90f, 0.55f};
		if (eHit && (!bHit || eHit->distance <= bHit->distance)) {
			if (auto* e = g.m_server->getEntity(eHit->entityId))
				anchor = e->position + glm::vec3(0, 2.6f, 0);
			prompt = "[LMB] Attack";
			color = {1.0f, 0.55f, 0.45f};
		} else if (bHit) {
			glm::ivec3 bp = bHit->hasInteract ? bHit->interactPos : bHit->blockPos;
			const auto& bdef = g.m_server->blockRegistry().get(
				g.m_server->chunks().getBlock(bp.x, bp.y, bp.z));
			const std::string& sid = bdef.string_id;
			bool isChest   = sid.find("chest")  != std::string::npos;
			bool isDoor    = sid.find("door")   != std::string::npos;
			bool isButton  = sid.find("button") != std::string::npos;
			bool isLever   = sid.find("lever")  != std::string::npos;
			bool isTnt     = sid.find("tnt")    != std::string::npos;
			anchor = glm::vec3(bp) + glm::vec3(0.5f, 1.25f, 0.5f);
			if (isChest)       prompt = "[E] Open";
			else if (isDoor)   prompt = "[E] Toggle";
			else if (isButton) prompt = "[E] Press";
			else if (isLever)  prompt = "[E] Flip";
			else if (isTnt)    prompt = "[E] Ignite";
			else               prompt = "[LMB] Mine";
			if (isChest || isDoor || isButton || isLever || isTnt)
				color = {0.55f, 0.95f, 0.70f};
		}
		if (!prompt.empty()) {
			glm::vec3 ndc;
			if (g.projectWorld(anchor, ndc)) {
				float scale = 0.85f;
				float rawW = prompt.size() * kCharWNdc * scale;
				float x = ndc.x - rawW * 0.5f;
				float rgba[4] = { color.x, color.y, color.z, 0.92f };
				g.m_rhi->drawText2D(prompt.c_str(), x, ndc.y, scale, rgba);
			}
		}
	}

	// Notification stack — newest at bottom.
	{
		float pillW = 0.30f;
		float pillH = 0.035f;
		float gap   = 0.006f;
		float rightEdge = 0.96f;
		float baseY = -0.82f;
		int shown = 0;
		for (int i = (int)g.m_notifs.size() - 1; i >= 0; --i) {
			const auto& n = g.m_notifs[i];
			float u = n.t / n.lifetime;
			float fadeIn  = glm::clamp(n.t / 0.18f, 0.0f, 1.0f);
			float fadeOut = (u > 0.75f) ? (1.0f - (u - 0.75f) / 0.25f) : 1.0f;
			float alpha = glm::clamp(fadeIn * fadeOut, 0.0f, 1.0f);
			float y = baseY + shown * (pillH + gap);
			float x = rightEdge - pillW;
			float bg[4]    = { 0.05f, 0.04f, 0.06f, 0.78f * alpha };
			float accent[4] = { n.color.x, n.color.y, n.color.z, 0.95f * alpha };
			g.m_rhi->drawRect2D(x, y, pillW, pillH, bg);
			g.m_rhi->drawRect2D(x, y, 0.006f, pillH, accent);
			float txtC[4] = { n.color.x, n.color.y, n.color.z, alpha };
			g.m_rhi->drawText2D(n.text.c_str(),
				x + 0.014f, y + pillH * 0.5f - 0.010f, 0.65f, txtC);
			shown++;
			if (shown >= 6) break;
		}
	}

	// ── Floating damage numbers ─────────────────────────────────────────
	for (const auto& f : g.m_floaters) {
		float u = f.t / f.lifetime;
		glm::vec3 world = f.worldPos + glm::vec3(0, u * f.rise, 0);
		glm::vec3 ndc;
		if (!g.projectWorld(world, ndc)) continue;
		float alpha = 1.0f - u;
		float scale = 1.0f + 0.4f * (1.0f - u);  // pop then shrink
		float rawW = f.text.size() * kCharWNdc * scale;
		float x = ndc.x - rawW * 0.5f;
		float rgba[4] = { f.color.x, f.color.y, f.color.z, alpha };
		g.m_rhi->drawTitle2D(f.text.c_str(), x, ndc.y, scale, rgba);
	}

	// ── Persistent status strip (bottom-left) ───────────────────────────
	// Three stacked rows so a new player can always see: how much HP they
	// have, which camera mode they're in, and what's in their hand. The
	// FPS/pos line stays at the bottom as diag info.
	{
		auto* me = g.playerEntity();

		// Row: HP bar with numeric overlay. Drawn first so the bar sits
		// above the other rows.
		if (me) {
			int hp = me->hp();
			int max = me->def().max_hp > 0 ? me->def().max_hp : 100;
			float frac = glm::clamp((float)hp / (float)max, 0.0f, 1.0f);

			const float barX = -0.96f;
			const float barY = -0.90f;
			const float barW = 0.22f;
			const float barH = 0.022f;

			const float back[4] = {0.05f, 0.04f, 0.05f, 0.70f};
			g.m_rhi->drawRect2D(barX, barY, barW, barH, back);
			// Fill color shifts red as HP drops.
			float fillR = 0.85f;
			float fillG = 0.25f + 0.55f * frac;
			float fillB = 0.20f;
			const float fill[4] = {fillR, fillG, fillB, 0.92f};
			g.m_rhi->drawRect2D(barX, barY, barW * frac, barH, fill);
			const float edge[4] = {0.0f, 0.0f, 0.0f, 0.55f};
			g.m_rhi->drawRect2D(barX,                  barY, barW, 0.0022f, edge);
			g.m_rhi->drawRect2D(barX,                  barY + barH - 0.0022f, barW, 0.0022f, edge);
			g.m_rhi->drawRect2D(barX,                  barY, 0.0022f, barH, edge);
			g.m_rhi->drawRect2D(barX + barW - 0.0022f, barY, 0.0022f, barH, edge);

			char hpBuf[32];
			std::snprintf(hpBuf, sizeof(hpBuf), "HP %d / %d", hp, max);
			const float hpText[4] = {1.0f, 0.95f, 0.90f, 0.95f};
			g.m_rhi->drawText2D(hpBuf, barX + 0.008f,
				barY + barH * 0.5f - kCharHNdc * 0.60f * 0.5f,
				0.60f, hpText);
		}

		// Row: camera-mode badge + held item. Always visible so a mode
		// switch is unambiguous — the transient toast is just the "what
		// each button does" reminder.
		{
			const char* modeNames[] = {"FPS", "TPS", "RPG", "RTS"};
			const char* modeName = modeNames[(int)g.m_cam.mode];
			const std::string& held = g.m_hotbar.get(g.m_hotbar.selected);
			const char* heldName = held.empty() ? "bare hands" : held.c_str();

			char buf[128];
			std::snprintf(buf, sizeof(buf), "%s  |  %s", modeName, heldName);
			const float c[4] = {0.85f, 0.82f, 0.72f, 0.85f};
			g.m_rhi->drawText2D(buf, -0.96f, -0.945f, 0.60f, c);
		}

		// Row: FPS / pos / counts (original diag line, now dimmed).
		{
			char buf[128];
			glm::vec3 fpsPos = me ? me->position : glm::vec3(0);
			std::snprintf(buf, sizeof(buf), "%.0f fps  |  %.0f %.0f %.0f  |  %zu ent  %zu chk",
				g.m_fpsDisplay,
				fpsPos.x, fpsPos.y, fpsPos.z,
				g.m_server->entityCount(),
				g.m_chunkMeshes.size());
			const float dim[4] = {0.45f, 0.42f, 0.50f, 0.65f};
			g.m_rhi->drawText2D(buf, -0.96f, -0.99f, 0.55f, dim);
		}
	}

	// ── F1 controls overlay (held) ──────────────────────────────────────
	// Keys per camera mode are taught as a one-shot toast the first time
	// you switch into the mode. Hold F1 to re-read — works regardless of
	// whether the player already saw the toast.
	if (glfwGetKey(g.m_window, GLFW_KEY_F1) == GLFW_PRESS) {
		struct Binding { const char* key; const char* action; };
		// Shared binds (top of every screen) are listed once so each
		// mode table only has to describe its specifics.
		static const Binding shared[] = {
			{"WASD",    "Move"},
			{"Space",   "Jump"},
			{"Shift",   "Sprint"},
			{"Tab",     "Inventory"},
			{"H",       "Handbook"},
			{"T",       "Talk to NPC (look at)"},
			{"V",       "Cycle camera"},
			{"E",       "Interact"},
			{"F2",      "Screenshot"},
			{"F3",      "Debug overlay"},
			{"Esc",     "Pause"},
		};
		static const Binding fpsTps[] = {
			{"Mouse",   "Look"},
			{"LMB",     "Attack"},
			{"RMB",     "Place / use"},
			{"Scroll",  "Hotbar slot"},
		};
		static const Binding rpg[] = {
			{"RMB-drag","Orbit camera"},
			{"LMB-tile","Move here"},
			{"LMB-drag","Box-select"},
		};
		static const Binding rts[] = {
			{"WASD",    "Pan camera"},
			{"RMB-drag","Orbit camera"},
			{"LMB-drag","Box-select"},
			{"LMB-hold","Build mode"},
		};
		const Binding* modeTable = shared; // placeholder
		int modeCount = 0;
		const char* modeLabel = "";
		switch ((int)g.m_cam.mode) {
			case 0: modeTable = fpsTps; modeCount = (int)(sizeof(fpsTps)/sizeof(*fpsTps)); modeLabel = "FPS"; break;
			case 1: modeTable = fpsTps; modeCount = (int)(sizeof(fpsTps)/sizeof(*fpsTps)); modeLabel = "TPS"; break;
			case 2: modeTable = rpg;    modeCount = (int)(sizeof(rpg)   /sizeof(*rpg));    modeLabel = "RPG"; break;
			case 3: modeTable = rts;    modeCount = (int)(sizeof(rts)   /sizeof(*rts));    modeLabel = "RTS"; break;
		}
		const int sharedCount = (int)(sizeof(shared)/sizeof(*shared));
		const int totalRows   = sharedCount + 1 /*sep*/ + modeCount;

		const float rowH = 0.036f;
		const float panelW = 0.46f;
		const float panelH = rowH * totalRows + 0.10f;
		const float px = -panelW * 0.5f;
		const float py = -panelH * 0.5f;

		const float scrim[4] = {0.0f, 0.0f, 0.0f, 0.55f};
		g.m_rhi->drawRect2D(-1.0f, -1.0f, 2.0f, 2.0f, scrim);
		const float fill[4]   = {0.08f, 0.07f, 0.06f, 0.96f};
		const float brass[4]  = {0.72f, 0.54f, 0.22f, 1.0f};
		g.m_rhi->drawRect2D(px, py, panelW, panelH, fill);
		g.m_rhi->drawRect2D(px, py, panelW, 0.003f, brass);
		g.m_rhi->drawRect2D(px, py + panelH - 0.003f, panelW, 0.003f, brass);
		g.m_rhi->drawRect2D(px, py, 0.003f, panelH, brass);
		g.m_rhi->drawRect2D(px + panelW - 0.003f, py, 0.003f, panelH, brass);

		char title[64];
		std::snprintf(title, sizeof(title), "Controls — %s", modeLabel);
		const float titleC[4] = {1.0f, 0.88f, 0.48f, 1.0f};
		float titleW = (float)std::strlen(title) * kCharWNdc * 1.0f;
		g.m_rhi->drawText2D(title, -titleW * 0.5f,
			py + panelH - 0.058f, 1.0f, titleC);

		const float colKeyX = px + 0.024f;
		const float colActX = px + 0.16f;
		const float txt[4]  = {0.90f, 0.87f, 0.80f, 1.0f};
		const float key[4]  = {0.98f, 0.80f, 0.35f, 1.0f};
		const float dim[4]  = {0.55f, 0.52f, 0.48f, 1.0f};

		float y = py + panelH - 0.10f;
		for (int i = 0; i < sharedCount; ++i) {
			g.m_rhi->drawText2D(shared[i].key,    colKeyX, y, 0.70f, key);
			g.m_rhi->drawText2D(shared[i].action, colActX, y, 0.70f, txt);
			y -= rowH;
		}
		// Separator row.
		g.m_rhi->drawText2D("— mode —", colKeyX, y, 0.60f, dim);
		y -= rowH;
		for (int i = 0; i < modeCount; ++i) {
			g.m_rhi->drawText2D(modeTable[i].key,    colKeyX, y, 0.70f, key);
			g.m_rhi->drawText2D(modeTable[i].action, colActX, y, 0.70f, txt);
			y -= rowH;
		}
	}
}

} // namespace civcraft::vk
