#include "client/game_vk_renderers.h"
#include "client/game_vk.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

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
#include "logic/constants.h"
#include "logic/inventory.h"
#include "logic/material_values.h"

namespace civcraft::vk {

// ─────────────────────────────────────────────────────────────────────────
// HUD — lightbulbs, HP bars, hotbar, player HP, crosshair
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

// Rarity tiers by material value (Diablo-inspired)
static glm::vec4 rarityColor(float matVal) {
	if (matVal >= 15.0f) return {1.0f, 0.65f, 0.15f, 1.0f};  // legendary gold
	if (matVal >= 10.0f) return {0.70f, 0.45f, 0.90f, 1.0f};  // epic purple
	if (matVal >=  5.0f) return {0.35f, 0.60f, 0.95f, 1.0f};  // rare blue
	if (matVal >=  3.0f) return {0.40f, 0.85f, 0.40f, 1.0f};  // uncommon green
	return                      {0.82f, 0.80f, 0.76f, 1.0f};  // common off-white
}

static std::string prettyItemName(const std::string& itemId, size_t maxLen = 0) {
	std::string name = itemId;
	auto colon = name.find(':');
	if (colon != std::string::npos) name = name.substr(colon + 1);
	for (auto& c : name) if (c == '_') c = ' ';
	if (!name.empty()) name[0] = (char)toupper((unsigned char)name[0]);
	if (maxLen > 0 && name.size() > maxLen) name = name.substr(0, maxLen - 1) + "~";
	return name;
}

// Render each hotbar slot's held item as a real 3D box-model anchored just in
// front of the camera. Items sit at d = 0.6 m (past the 0.1 m near plane) so
// depth-test always wins against world geo; their world-space size is sized
// from the slot's NDC footprint so they read as proper 3D inventory icons.
// Runs BEFORE renderHUD so slot borders/count badges drawn later (2D swapchain
// pass) still compose on top.
void HudRenderer::renderHotbarItems3D() {
	Game& g = game_;
	civcraft::Entity* me = g.m_server->getEntity(g.m_server->localPlayerId());
	if (!me || !me->inventory) return;

	const int   N     = 10;
	const float h     = 0.14f;
	const float slotW = h / g.m_aspect;
	const float gap   = 0.008f;
	float totalW = N * slotW + (N - 1) * gap;
	float x0 = -totalW * 0.5f;
	float y0 = -0.97f;

	glm::vec3 camPos   = g.m_cam.position;
	glm::vec3 camFwd   = glm::normalize(g.m_cam.front());
	glm::vec3 worldUp  = glm::vec3(0, 1, 0);
	if (std::abs(glm::dot(camFwd, worldUp)) > 0.99f) worldUp = glm::vec3(0, 0, 1);
	glm::vec3 camRight = glm::normalize(glm::cross(camFwd, worldUp));
	glm::vec3 camUp    = glm::normalize(glm::cross(camRight, camFwd));

	const float d      = 0.6f;
	const float fovRad = glm::radians(g.m_cam.fov);
	const float halfH  = d * std::tan(fovRad * 0.5f);
	const float halfW  = halfH * g.m_aspect;

	auto& boxes = g.m_scratch.charBoxes;
	boxes.clear();

	for (int i = 0; i < N; i++) {
		std::string itemId = g.m_hotbar.get(i);
		if (itemId.empty()) continue;
		if (me->inventory->count(itemId) <= 0) continue;

		std::string stem = itemId;
		auto colon = stem.find(':');
		if (colon != std::string::npos) stem = stem.substr(colon + 1);
		auto it = g.m_models.find(stem);
		if (it == g.m_models.end()) {
			it = g.m_models.find(itemId);
			if (it == g.m_models.end()) continue;
		}

		float slotCx = x0 + i * (slotW + gap) + slotW * 0.5f;
		float slotCy = y0 + h * 0.5f;
		glm::vec3 itemWorldPos = camPos + camFwd * d
		                       + camRight * (slotCx * halfW)
		                       + camUp    * (slotCy * halfH);

		float slotWorld    = h * halfH;
		float targetWorldH = slotWorld * 0.36f;

		civcraft::BoxModel m = it->second;

		float s0 = m.modelScale;
		float minX =  1e9f, maxX = -1e9f;
		float minY =  1e9f, maxY = -1e9f;
		float minZ =  1e9f, maxZ = -1e9f;
		for (auto& part : m.parts) {
			glm::vec3 o = part.offset   * s0;
			glm::vec3 hs= part.halfSize * s0;
			minX = std::min(minX, o.x - hs.x); maxX = std::max(maxX, o.x + hs.x);
			minY = std::min(minY, o.y - hs.y); maxY = std::max(maxY, o.y + hs.y);
			minZ = std::min(minZ, o.z - hs.z); maxZ = std::max(maxZ, o.z + hs.z);
		}
		float dy = std::max(maxY - minY, 0.01f);
		float dx = std::max(maxX - minX, 0.01f);
		float dz = std::max(maxZ - minZ, 0.01f);
		float maxDim = std::max(dy, std::max(dx, dz));
		float scale = targetWorldH / maxDim;
		for (auto& part : m.parts) {
			part.offset   *= scale;
			part.halfSize *= scale;
		}
		float cy = (minY + maxY) * 0.5f * scale;
		glm::vec3 feet = itemWorldPos - glm::vec3(0.0f, cy, 0.0f);

		civcraft::AnimState anim{};
		anim.time = g.m_wallTime;
		float slowSpin = (float)g.m_wallTime * 45.0f + i * 27.0f;
		float spinYaw  = 90.0f + g.m_cam.lookYaw + slowSpin;

		civcraft::appendBoxModel(boxes, m, feet, spinYaw, anim);
	}

	if (boxes.empty()) return;

	rhi::IRhi::SceneParams scene{};
	glm::mat4 vp = g.viewProj();
	std::memcpy(scene.viewProj, &vp[0][0], sizeof(float) * 16);
	scene.camPos[0] = camPos.x; scene.camPos[1] = camPos.y; scene.camPos[2] = camPos.z;
	scene.time = g.m_wallTime;
	glm::vec3 sun = glm::normalize(glm::vec3(0.35f, 0.90f, 0.45f));
	scene.sunDir[0] = sun.x; scene.sunDir[1] = sun.y; scene.sunDir[2] = sun.z;
	scene.sunStr = 0.95f;

	g.m_rhi->drawBoxModel(scene, boxes.data(), (uint32_t)(boxes.size() / 19));
}

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

	// ── Top-right DST-style HUD (dial + HP + day) — F3-gated ────────────
	// Pixel-art sundial: sky-coloured face cycles midnight→dawn→noon→dusk,
	// 3-ring season bezel, cardinal markers, sun/moon disc at the current
	// hour. Stats column underneath reads HP and day count as shapes + digits
	// only (no letters), DST-style.
	if (g.m_showDebug) {
		const float tod    = g.m_server ? g.m_server->worldTime() : 0.5f;
		const uint32_t day = g.m_server ? g.m_server->dayCount()  : 0;
		const float season = civcraft::seasonPhase(day, tod);

		// Dial geometry. rX is the NDC horizontal half-extent, derived from
		// the vertical one so the disc reads as a true circle at any aspect.
		constexpr int   N       = 80;        // cells per side
		constexpr float rY      = 0.085f;    // NDC vertical radius
		const     float rX      = rY / g.m_aspect;
		const     float kMargin = 0.018f;
		const     float dcx     = 1.0f - rX - kMargin;
		const     float dcy     = 1.0f - rY - kMargin;
		const     float cellX   = (2.0f * rX) / (float)N;
		const     float cellY   = (2.0f * rY) / (float)N;

		// Sky keyframes: midnight indigo, dawn coral, noon sky, dusk crimson.
		auto skyAt = [](float t) -> glm::vec3 {
			// t ∈ [0,1): 0=midnight, 0.25=dawn, 0.5=noon, 0.75=dusk.
			const glm::vec3 kMid(0.05f, 0.06f, 0.16f);
			const glm::vec3 kDawn(0.95f, 0.55f, 0.45f);
			const glm::vec3 kNoon(0.52f, 0.76f, 0.98f);
			const glm::vec3 kDusk(0.70f, 0.28f, 0.35f);
			float p = t * 4.0f;
			int   i = (int)std::floor(p) & 3;
			float f = p - std::floor(p);
			f = f * f * (3.0f - 2.0f * f);   // smoothstep
			glm::vec3 a, b;
			switch (i) {
				case 0: a = kMid;  b = kDawn; break;
				case 1: a = kDawn; b = kNoon; break;
				case 2: a = kNoon; b = kDusk; break;
				default: a = kDusk; b = kMid; break;
			}
			return glm::mix(a, b, f);
		};

		glm::vec3 face = skyAt(tod);

		// Season tint for the outer bezel ring. i0→i1 blend follows
		// seasonPhase's fractional part so the transition is smooth.
		const glm::vec3 kSeasonCol[4] = {
			glm::vec3(0.45f, 0.78f, 0.32f),   // spring — fresh green
			glm::vec3(0.22f, 0.58f, 0.20f),   // summer — deep green
			glm::vec3(0.82f, 0.50f, 0.18f),   // autumn — ochre
			glm::vec3(0.86f, 0.92f, 0.98f),   // winter — frost
		};
		int   si0 = ((int)std::floor(season)) & 3;
		int   si1 = (si0 + 1) & 3;
		float sf  = season - std::floor(season);
		glm::vec3 bezel = glm::mix(kSeasonCol[si0], kSeasonCol[si1], sf);

		// Sun / moon position. Angle: 0=bottom (midnight), π=top (noon).
		float ang = tod * 6.2831853f;
		float sux = -std::sin(ang) * 0.72f;   // -1..1 in dial-local coords
		float suy = -std::cos(ang) * 0.72f;
		float nightAmt = std::max(0.0f, 1.0f - 2.0f * std::abs(tod - 0.5f));
		nightAmt = 1.0f - nightAmt;           // ≈1 near midnight, ≈0 near noon

		auto hashf = [](int x, int y) {
			uint32_t h = (uint32_t)(x * 374761393u) ^ (uint32_t)(y * 668265263u);
			h = (h ^ (h >> 13)) * 1274126177u;
			return (float)(h & 0xffff) / 65535.0f;
		};

		// Per-cell rasterization: one drawRect2D per "pixel" inside the disc.
		for (int y = 0; y < N; ++y) {
			for (int x = 0; x < N; ++x) {
				float u = (x + 0.5f) / N * 2.0f - 1.0f;   // -1..1
				float v = (y + 0.5f) / N * 2.0f - 1.0f;
				float r = std::sqrt(u*u + v*v);
				if (r > 1.02f) continue;

				glm::vec4 col(0.0f);
				if (r > 0.94f && r <= 1.02f) {
					// Outer bezel: season tint, darker at edge.
					float k = 0.75f + 0.25f * (1.02f - r) / 0.08f;
					col = glm::vec4(bezel * k, 0.95f);
				} else if (r > 0.86f && r <= 0.94f) {
					// Inner bezel ring: dark frame.
					col = glm::vec4(0.10f, 0.08f, 0.06f, 0.92f);
				} else if (r > 0.82f && r <= 0.86f) {
					// Thin highlight line between frame and face.
					col = glm::vec4(bezel * 1.1f, 0.85f);
				} else {
					// Face: sky colour + stars at night.
					glm::vec3 c = face;
					if (nightAmt > 0.05f) {
						float s = hashf(x, y);
						float star = s > 0.985f ? 1.0f : 0.0f;
						c = glm::mix(c, glm::vec3(1.0f), star * nightAmt * 0.9f);
					}
					col = glm::vec4(c, 0.92f);
				}

				// Cardinal tick marks at N/E/S/W (every 90°).
				// Convert (u,v) → angle, check if within a small wedge of each.
				if (r > 0.70f && r <= 0.84f) {
					float a = std::atan2(u, -v);  // 0=bottom, π=top
					if (a < 0) a += 6.2831853f;
					float tickA[4] = { 3.1415927f, 4.7123890f, 0.0f, 1.5707963f };
					for (int k = 0; k < 4; ++k) {
						float d = std::abs(a - tickA[k]);
						if (d > 3.1415927f) d = 6.2831853f - d;
						if (d < 0.05f) {
							col = glm::vec4(0.96f, 0.91f, 0.70f, 0.98f);
							break;
						}
					}
				}

				// Sun / moon disc.
				float du = u - sux, dv = v - suy;
				float dr = std::sqrt(du*du + dv*dv);
				if (dr < 0.14f) {
					glm::vec3 sunCol = nightAmt > 0.5f
						? glm::vec3(0.95f, 0.96f, 1.0f)   // moon
						: glm::vec3(1.0f,  0.92f, 0.55f); // sun
					float k = 1.0f - dr / 0.14f;
					col = glm::vec4(glm::mix(glm::vec3(col), sunCol, k), 1.0f);
				} else if (dr < 0.22f) {
					// Soft halo.
					float k = (0.22f - dr) / 0.08f * 0.5f;
					glm::vec3 halo = nightAmt > 0.5f
						? glm::vec3(0.70f, 0.75f, 0.95f)
						: glm::vec3(1.0f,  0.85f, 0.40f);
					col = glm::vec4(glm::mix(glm::vec3(col), halo, k), col.a);
				}

				if (col.a <= 0.0f) continue;
				float px = dcx + u * rX - cellX * 0.5f;
				float py = dcy + v * rY - cellY * 0.5f;
				g.m_rhi->drawRect2D(px, py, cellX, cellY, &col.x);
			}
		}

		// ── Stats column: heart+HP and sun+day, pure shapes + digits ────
		// 3×5 digit glyphs; 5×5 icon bitmaps.
		static constexpr uint16_t kDigitGlyphs[10] = {
			0b111'101'101'101'111u,  // 0
			0b010'110'010'010'111u,  // 1
			0b111'001'111'100'111u,  // 2
			0b111'001'111'001'111u,  // 3
			0b101'101'111'001'001u,  // 4
			0b111'100'111'001'111u,  // 5
			0b111'100'111'101'111u,  // 6
			0b111'001'010'010'010u,  // 7
			0b111'101'111'101'111u,  // 8
			0b111'101'111'001'111u,  // 9
		};
		static constexpr uint8_t kHeart[5] = {
			0b01010, 0b11111, 0b11111, 0b01110, 0b00100,
		};
		static constexpr uint8_t kSunIco[5] = {
			0b00100, 0b10101, 0b01110, 0b10101, 0b00100,
		};

		// Stats pixel size. Keep cells square in NDC: pxH = pxW * aspect.
		const float pxW = 0.0038f;
		const float pxH = pxW * g.m_aspect;
		auto drawGlyph3x5 = [&](uint16_t glyph, float x0, float y0, const float c[4]) {
			for (int r = 0; r < 5; ++r) {
				for (int cc = 0; cc < 3; ++cc) {
					int bit = (glyph >> ((4 - r) * 3 + (2 - cc))) & 1;
					if (!bit) continue;
					g.m_rhi->drawRect2D(x0 + cc * pxW, y0 - r * pxH, pxW, pxH, c);
				}
			}
		};
		auto drawIcon5x5 = [&](const uint8_t* rows, float x0, float y0, const float c[4]) {
			for (int r = 0; r < 5; ++r) {
				for (int cc = 0; cc < 5; ++cc) {
					if (!((rows[r] >> (4 - cc)) & 1)) continue;
					g.m_rhi->drawRect2D(x0 + cc * pxW, y0 - r * pxH, pxW, pxH, c);
				}
			}
		};
		auto drawNumber = [&](int n, float x0, float y0, const float c[4]) {
			char buf[16];
			int len = std::snprintf(buf, sizeof(buf), "%d", n);
			for (int i = 0; i < len; ++i) {
				int d = buf[i] - '0';
				if (d < 0 || d > 9) continue;
				drawGlyph3x5(kDigitGlyphs[d], x0 + i * (pxW * 4), y0, c);
			}
		};

		// Column origin: just below the dial, right-aligned with it.
		const float col0x = dcx - rX + 0.006f;
		const float col0y = dcy - rY - 0.012f;

		civcraft::Entity* me = g.m_server->getEntity(g.m_server->localPlayerId());
		int hp     = me ? me->hp() : 0;
		int hpMax  = (me && me->def().max_hp > 0) ? me->def().max_hp : 100;
		float hpFrac = std::clamp((float)hp / (float)hpMax, 0.0f, 1.0f);

		const float red[4]    = { 0.92f, 0.22f, 0.20f, 1.0f };
		const float gold[4]   = { 1.00f, 0.88f, 0.45f, 1.0f };
		const float white[4]  = { 0.96f, 0.96f, 0.98f, 1.0f };
		const float dim[4]    = { 0.55f, 0.55f, 0.60f, 1.0f };

		const float* hpCol = hpFrac > 0.35f ? white : red;

		drawIcon5x5(kHeart, col0x, col0y, red);
		drawNumber(hp, col0x + pxW * 7, col0y, hpCol);

		float row2 = col0y - pxH * 7.5f;
		drawIcon5x5(kSunIco, col0x, row2, gold);
		drawNumber((int)day + 1, col0x + pxW * 7, row2, white);

		(void)dim;
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

			float barY = ndc.y - 0.020f;
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

				int maxHp = e.def().max_hp > 0 ? e.def().max_hp : 100;
				float hpFrac = std::clamp((float)e.hp() / (float)maxHp, 0.0f, 1.0f);
				float barW = 0.10f, barH = 0.010f;
				float barX = ndc.x - barW * 0.5f;
				const float hpBg[4]    = {0.08f, 0.04f, 0.04f, 0.85f};
				const float hpGreen[4] = {0.20f, 0.78f, 0.28f, 1.0f};
				const float hpRed[4]   = {0.78f, 0.20f, 0.18f, 1.0f};
				g.m_rhi->drawRect2D(barX, barY, barW, barH, hpBg);
				auto& fill = hpFrac > 0.35f ? hpGreen : hpRed;
				g.m_rhi->drawRect2D(barX, barY, barW * hpFrac, barH, fill);
				float tyW = e.typeId().size() * kCharWNdc * 0.6f;
				float tyX = ndc.x - tyW * 0.5f;
				float tyY = barY - 0.018f;
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

	// Notification stack — newest at bottom above the hotbar.
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

	// ── Player status panel (top-left) ──────────────────────────────────
	// Diablo-style: HP globe as a wide bar with gradient, coins as small
	// counter beneath. Dark glass panel background. F3-gated.
	if (g.m_showDebug) {
		float px = -0.96f, py = 0.88f;
		float pw = 0.38f, ph = 0.10f;
		const float panelBg[4] = {0.04f, 0.03f, 0.05f, 0.82f};
		const float panelBdr[4] = {0.25f, 0.20f, 0.12f, 0.45f};
		g.m_rhi->drawRect2D(px, py, pw, ph, panelBg);
		g.m_rhi->drawRect2D(px, py, pw, 0.002f, panelBdr);       // top edge
		g.m_rhi->drawRect2D(px, py + ph - 0.002f, pw, 0.002f, panelBdr); // bot
		g.m_rhi->drawRect2D(px, py, 0.002f, ph, panelBdr);       // left
		g.m_rhi->drawRect2D(px + pw - 0.002f, py, 0.002f, ph, panelBdr); // right

		// HP bar — red fill with dark inner track
		auto* hpEntity = g.playerEntity();
		float playerHp = hpEntity ? (float)hpEntity->hp() : 0.0f;
		float hpFrac = std::clamp(playerHp / (float)kTune.playerMaxHP, 0.0f, 1.0f);
		float bx = px + 0.015f, by = py + 0.055f;
		float bw = pw - 0.030f, bh = 0.028f;
		const float hpTrack[4] = {0.08f, 0.04f, 0.04f, 0.90f};
		const float hpFill[4]  = {0.82f, 0.18f, 0.15f, 0.95f};
		const float hpGlow[4]  = {0.95f, 0.30f, 0.25f, 0.60f};
		g.m_rhi->drawRect2D(bx, by, bw, bh, hpTrack);
		g.m_rhi->drawRect2D(bx + 0.002f, by + 0.002f,
			(bw - 0.004f) * hpFrac, bh - 0.004f, hpFill);
		if (hpFrac > 0.0f)
			g.m_rhi->drawRect2D(bx + 0.002f, by + 0.002f,
				(bw - 0.004f) * hpFrac, 0.005f, hpGlow);

		char hpBuf[32];
		std::snprintf(hpBuf, sizeof(hpBuf), "%d / %d",
			(int)std::round(playerHp), kTune.playerMaxHP);
		const float hpTxt[4] = {1.0f, 0.92f, 0.88f, 1.0f};
		float hpTxtW = std::strlen(hpBuf) * kCharWNdc * 0.7f;
		g.m_rhi->drawText2D(hpBuf, bx + bw * 0.5f - hpTxtW * 0.5f,
			by + 0.005f, 0.7f, hpTxt);
		const float hpLabel[4] = {0.60f, 0.50f, 0.45f, 0.85f};
		g.m_rhi->drawText2D("HP", bx, by + bh + 0.004f, 0.55f, hpLabel);

		// Coins — small gold counter, top of panel
		if (g.m_coins > 0) {
			char coinBuf[32];
			std::snprintf(coinBuf, sizeof(coinBuf), "%d", g.m_coins);
			const float coinGold[4] = {1.0f, 0.78f, 0.25f, 0.95f};
			const float coinDim[4]  = {0.65f, 0.55f, 0.35f, 0.80f};
			g.m_rhi->drawText2D("Gold", px + 0.015f, py + 0.015f, 0.55f, coinDim);
			g.m_rhi->drawText2D(coinBuf, px + 0.085f, py + 0.015f, 0.65f, coinGold);
		}
	}

	// ── Hotbar (bottom-center) ──────────────────────────────────────────
	// Sleek dark glass with warm gold selection. All slots generic, filled
	// with real inventory items. Rarity-colored item names.
	// Pixel-square slots — keep these constants in sync with
	// renderHotbarItems3D() above.
	{
		const int   N     = 10;
		const float h     = 0.14f;
		const float slotW = h / g.m_aspect;
		const float gap   = 0.008f;
		float totalW = N * slotW + (N - 1) * gap;
		float x0 = -totalW * 0.5f;
		float y0 = -0.97f;

		// Bezel-only frame: the 3D items drawn by renderHotbarItems3D() live
		// in the main pass which *must* finish before we enter the swapchain
		// pass, so any 2D fill drawn here composes *over* them. To keep items
		// reading as "on top" of the hotbar, we only draw the thin top/bottom
		// strips + per-slot fills for empty cells — filled cells get no fill.
		const float panelBdr[4] = {0.22f, 0.18f, 0.10f, 0.55f};
		float pad = 0.008f;
		g.m_rhi->drawRect2D(x0 - pad, y0 - pad, totalW + pad * 2, 0.002f, panelBdr);
		g.m_rhi->drawRect2D(x0 - pad, y0 + h + pad - 0.002f, totalW + pad * 2, 0.002f, panelBdr);

		const float slotEmptyBg[4] = {0.10f, 0.09f, 0.12f, 0.60f};  // only for empty cells
		const float selGlow[4]  = {0.95f, 0.75f, 0.25f, 0.90f};
		const float selInner[4] = {0.95f, 0.75f, 0.25f, 0.15f};
		const float keyDim[4]   = {0.50f, 0.48f, 0.55f, 0.70f};

		civcraft::Entity* me = g.m_server->getEntity(g.m_server->localPlayerId());

		for (int i = 0; i < N; i++) {
			float x = x0 + i * (slotW + gap);
			bool selected = (i == g.m_hotbarSlot);

			// Filled slots get no fill so the 3D item (drawn in the main pass,
			// before the swapchain pass begins) is the topmost thing the eye
			// sees in that cell. Empty slots still get a placeholder fill;
			// selection always wins and paints a warm inner glow.
			std::string probeId = me && me->inventory
			                    ? g.m_hotbar.get(i) : std::string();
			bool slotHasItem = !probeId.empty()
			                && me && me->inventory
			                && me->inventory->count(probeId) > 0;
			if (selected) {
				g.m_rhi->drawRect2D(x, y0, slotW, h, selInner);
			} else if (!slotHasItem) {
				g.m_rhi->drawRect2D(x, y0, slotW, h, slotEmptyBg);
			}

			// Selection border — thin gold edges
			if (selected) {
				float t = 0.003f;
				g.m_rhi->drawRect2D(x, y0, slotW, t, selGlow);
				g.m_rhi->drawRect2D(x, y0 + h - t, slotW, t, selGlow);
				g.m_rhi->drawRect2D(x, y0, t, h, selGlow);
				g.m_rhi->drawRect2D(x + slotW - t, y0, t, h, selGlow);
			}

			// Item content — slot→itemId alias comes from g.m_hotbar (client
			// rearrange layer), resolved against live inventory count.
			// renderHotbarItems3D() already drew the item's box model just in
			// front of the camera, projected into this slot. No 2D fallback —
			// if no model exists, the slot just shows chrome + badge + strip.
			std::string itemId = (me && me->inventory) ? g.m_hotbar.get(i) : std::string();
			int count = (!itemId.empty() && me && me->inventory) ? me->inventory->count(itemId) : 0;
			if (count > 0) {
				std::string rawId = itemId;
				auto colon = rawId.find(':');
				if (colon != std::string::npos) rawId = rawId.substr(colon + 1);
				float matVal = civcraft::getMaterialValue(rawId);
				glm::vec4 rc = rarityColor(matVal);

				// Rarity strip along the top edge of the slot — replaces the
				// in-slot name as the "what tier is this" cue.
				const float rarityStrip[4] = { rc.x, rc.y, rc.z, 0.75f };
				g.m_rhi->drawRect2D(x + 0.004f, y0 + h - 0.004f, slotW - 0.008f, 0.002f, rarityStrip);

				// Count badge — top-right, drawn as a dark chip *over* the
				// swatch so it stays readable regardless of swatch color.
				if (count > 1) {
					char cnt[8]; snprintf(cnt, sizeof(cnt), "x%d", count);
					size_t cntLen = std::strlen(cnt);
					float chipW = 0.010f + 0.011f * (float)cntLen;
					float chipH = 0.022f;
					float chipX = x + slotW - chipW - 0.003f;
					float chipY = y0 + h - chipH - 0.003f;
					const float badge[4] = {0.04f, 0.03f, 0.05f, 0.92f};
					g.m_rhi->drawRect2D(chipX, chipY, chipW, chipH, badge);
					const float wh[4] = {1.0f, 0.97f, 0.85f, 1.0f};
					g.m_rhi->drawText2D(cnt, chipX + 0.003f, chipY + 0.004f, 0.62f, wh);
				}
			}

			// Slot key label — bottom-right, dim. Drawn last so it reads
			// cleanly on top of the swatch.
			char lab[4]; std::snprintf(lab, sizeof(lab), "%d", (i + 1) % 10);
			const float keyBright[4] = {0.90f, 0.85f, 0.55f, 0.95f};
			g.m_rhi->drawText2D(lab, x + slotW - 0.018f, y0 + 0.005f, 0.60f,
				selected ? keyBright : keyDim);
		}

		// ── Hotbar drag/drop overlay ──────────────────────────────────────
		// Transparent ImGui window sized+positioned exactly over the visual
		// hotbar. Each slot is an invisible button that's both a drag source
		// (picks up the current alias) and a drop target (accepts INV_SLOT
		// payloads from hotbar peers OR from the Tab inventory panel).
		// Cursor is only released when inventory is open, so the hotbar is
		// interactive only while Tab is held.
		if (g.m_invOpen && me && me->inventory) {
			ImGuiIO& io = ImGui::GetIO();
			float ww = io.DisplaySize.x, wh = io.DisplaySize.y;
			// NDC→pixel: x_px = (x_ndc + 1) * 0.5 * w;
			// ImGui y is top-down, hotbar sits near bottom: y_px from top = (1 - y_top_ndc) * 0.5 * h
			float pxStartX = (x0 + 1.0f) * 0.5f * ww;
			float pxStartY = (1.0f - (y0 + h)) * 0.5f * wh;
			float pxSlotW  = slotW * 0.5f * ww;
			float pxSlotH  = h     * 0.5f * wh;
			float pxGap    = gap   * 0.5f * ww;

			ImGui::SetNextWindowPos({pxStartX, pxStartY});
			ImGui::SetNextWindowSize({N * (pxSlotW + pxGap), pxSlotH});
			ImGui::SetNextWindowBgAlpha(0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   {pxGap, 0.0f});
			if (ImGui::Begin("##hotbar_dd", nullptr,
				ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoBackground| ImGuiWindowFlags_NoBringToFrontOnFocus |
				ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {
				// Payload: slot=-1 means "from inventory panel"; slot>=0 means "from hotbar".
				struct DragSlot { char itemId[64]; int slot; };
				for (int i = 0; i < N; i++) {
					if (i > 0) ImGui::SameLine(0.0f, pxGap);
					char bid[24]; snprintf(bid, sizeof(bid), "##hdd%d", i);
					ImGui::InvisibleButton(bid, {pxSlotW, pxSlotH});
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
						DragSlot ds{}; ds.slot = i;
						std::snprintf(ds.itemId, sizeof(ds.itemId), "%s", g.m_hotbar.get(i).c_str());
						ImGui::SetDragDropPayload("INV_SLOT", &ds, sizeof(ds));
						if (ds.itemId[0]) ImGui::Text("%s", ds.itemId);
						else              ImGui::TextDisabled("(empty)");
						ImGui::EndDragDropSource();
					}
					if (ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("INV_SLOT")) {
							auto* ds = (const DragSlot*)pl->Data;
							std::string newId = ds->itemId;
							std::string oldId = g.m_hotbar.get(i);
							// Hotbar↔hotbar drags swap; inventory→hotbar (slot=-1)
							// simply writes newId to slot i, leaving oldId free
							// (it's still in inventory, so repopulate would add
							// it back to a free slot on next S_INVENTORY).
							if (ds->slot >= 0) g.m_hotbar.set(ds->slot, oldId);
							g.m_hotbar.set(i, newId);
							// Persist immediately so the arrangement survives
							// process kills / crashes, not just graceful quits.
							g.m_hotbar.saveToFile(g.m_hotbarSavePath);
						}
						ImGui::EndDragDropTarget();
					}
				}
			}
			ImGui::End();
			ImGui::PopStyleVar(2);
		}
	}

	// ── Inventory panel (Tab) — Diablo-style equipment UI ───
	// Shared EquipmentUI lives in platform/client/; see inventory_visuals.h
	// for the iso-cube icon fallback.
	if (g.m_invOpen) {
		civcraft::Entity* me = g.m_server->getEntity(g.m_server->localPlayerId());
		if (me && me->inventory) {
			// Keep EquipmentUI's internal open flag in sync with g.m_invOpen
			// so its close-button (X in titlebar) can also flip our state.
			if (!g.m_equipUI.isOpen()) g.m_equipUI.toggle();
			g.m_equipUI.render(*me->inventory, g.m_server->blockRegistry(),
				(float)g.m_fbW, (float)g.m_fbH);
			if (!g.m_equipUI.isOpen()) g.m_invOpen = false;
		}
	}

	// ── FPS + position readout (bottom-left) ────────────────────────────
	{
		char buf[128];
		auto* fpsMe = g.playerEntity();
		glm::vec3 fpsPos = fpsMe ? fpsMe->position : glm::vec3(0);
		std::snprintf(buf, sizeof(buf), "%.0f fps  |  %.0f %.0f %.0f  |  %zu ent  %zu chk",
			ImGui::GetIO().Framerate,
			fpsPos.x, fpsPos.y, fpsPos.z,
			g.m_server->entityCount(),
			g.m_chunkMeshes.size());
		const float dim[4] = {0.45f, 0.42f, 0.50f, 0.65f};
		g.m_rhi->drawText2D(buf, -0.96f, -0.99f, 0.55f, dim);
	}
}

} // namespace civcraft::vk
