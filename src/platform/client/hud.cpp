#include "client/hud.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cmath>

namespace civcraft {

// ----------------------------------------------------------------
// DST-style circular stat ring helper (HP / Hunger)
// cx, cy: NDC center; fraction: 0–1 fill (clockwise from top)
// r_outer / r_inner: in NDC-x units so circles are round on screen
// ----------------------------------------------------------------
static void drawStatRing(TextRenderer& text, float cx, float cy,
                         float fraction, glm::vec4 fillColor,
                         float r_outer, float r_inner, float aspect) {
	const int segs = 40;
	const float pi = (float)M_PI;
	// Drop shadow (wider, very transparent)
	text.drawArc(cx, cy, r_inner, r_outer + 0.007f, 0, 2*pi,
	             {0.0f, 0.0f, 0.0f, 0.40f}, aspect, segs);
	// Background ring (full circle, dark leather-brown)
	text.drawArc(cx, cy, r_inner, r_outer, 0, 2*pi,
	             {0.16f, 0.12f, 0.08f, 0.88f}, aspect, segs);
	// Colored fill arc: clockwise from top (π/2) for fraction of full circle
	if (fraction > 0.004f) {
		float endA   = pi / 2.0f;
		float startA = endA - std::min(fraction, 1.0f) * 2.0f * pi;
		text.drawArc(cx, cy, r_inner + 0.002f, r_outer - 0.002f,
		             startA, endA, fillColor, aspect, segs);
	}
	// Inner dark disk (the "face" of the gauge)
	text.drawArc(cx, cy, 0, r_inner - 0.003f, 0, 2*pi,
	             {0.10f, 0.08f, 0.06f, 0.90f}, aspect, 32);
	// Thin inner rim
	text.drawArc(cx, cy, r_inner - 0.004f, r_inner - 0.001f, 0, 2*pi,
	             {0.05f, 0.04f, 0.03f, 0.75f}, aspect, 32);
}

// ================================================================
// Circular stat gauges: HP (red) + Hunger (amber) — bottom-left
// ================================================================
void HUD::renderHealthBars(const HUDContext& ctx, TextRenderer& text) {
	if (ctx.state != GameState::PLAYING && ctx.state != GameState::ADMIN) return;

	const float aspect  = ctx.aspect;
	const float r_outer = 0.050f;   // NDC-x radius (~40 px on 1600px wide screen)
	const float r_inner = 0.033f;
	const float cy      = -0.71f;   // moved up to clear hotbar
	const float spacing = 0.20f;
	const float cx0     = -0.88f;   // HP center
	const float cx1     = cx0 + spacing; // Hunger center

	// Subtle backdrop panel unifying the two circles
	{
		float pX = cx0 - r_outer - 0.012f;
		float pW = (cx1 + r_outer + 0.012f) - pX;
		float pY = cy - r_outer * aspect - 0.012f;
		float pH = r_outer * aspect * 2.0f + 0.024f;
		text.drawRect(pX, pY, pW, pH, {0.04f, 0.03f, 0.02f, 0.55f});
	}

	// ── HP ──────────────────────────────────────────────────────────
	float hpFrac = std::max(0.0f, (float)ctx.playerHP / ctx.playerMaxHP);
	glm::vec4 hpFill = hpFrac > 0.30f
	    ? glm::vec4(0.80f, 0.14f, 0.12f, 0.95f)   // deep red
	    : glm::vec4(1.00f, 0.07f, 0.07f, 1.00f);   // bright-flash red when low
	drawStatRing(text, cx0, cy, hpFrac, hpFill, r_outer, r_inner, aspect);

	// HP value centered inside ring (large, readable)
	char buf[16];
	snprintf(buf, sizeof(buf), "%d", ctx.playerHP);
	{
		float tw = strlen(buf) * 0.018f * 1.05f, th = 0.032f * 1.05f;
		text.drawText(buf, cx0 - tw * 0.5f, cy + th * 0.08f,
		              1.05f, {1.0f, 0.72f, 0.72f, 0.97f}, aspect);
		// Tiny "HP" label below number
		text.drawText("HP", cx0 - 0.009f * 2 * 0.38f, cy - th * 0.82f,
		              0.38f, {0.80f, 0.45f, 0.45f, 0.70f}, aspect);
	}

	// ── Hunger ──────────────────────────────────────────────────────
	float hungerFrac = std::max(0.0f, ctx.playerHunger / 20.0f);
	glm::vec4 hungerFill = hungerFrac > 0.25f
	    ? glm::vec4(0.84f, 0.58f, 0.10f, 0.95f)   // amber gold
	    : glm::vec4(1.00f, 0.28f, 0.05f, 1.00f);   // orange-red when starving
	drawStatRing(text, cx1, cy, hungerFrac, hungerFill, r_outer, r_inner, aspect);

	// Hunger value centered inside ring
	snprintf(buf, sizeof(buf), "%.0f", ctx.playerHunger);
	{
		float tw = strlen(buf) * 0.018f * 1.05f, th = 0.032f * 1.05f;
		text.drawText(buf, cx1 - tw * 0.5f, cy + th * 0.08f,
		              1.05f, {1.0f, 0.90f, 0.60f, 0.97f}, aspect);
		// Tiny "FD" label below number
		text.drawText("FD", cx1 - 0.009f * 2 * 0.38f, cy - th * 0.82f,
		              0.38f, {0.80f, 0.65f, 0.35f, 0.70f}, aspect);
	}
}

// ================================================================
// Circular clock: top-right. Day arc gold, night arc blue.
// A bright dot travels the ring showing current time.
// ================================================================
void HUD::renderTimeOfDay(const HUDContext& ctx, TextRenderer& text) {
	const float cx = 0.900f, cy = 0.875f;
	const float ro = 0.042f, ri = 0.026f;
	const float aspect = ctx.aspect;
	const float pi = (float)M_PI;

	// worldTime: 0=midnight, 0.25=sunrise, 0.5=noon, 0.75=sunset
	float t = ctx.worldTime;

	// Drop shadow
	text.drawArc(cx, cy, 0, ro + 0.009f, 0, 2*pi, {0,0,0,0.38f}, aspect, 32);

	// Day arc: sunrise (angle=0, rightward) to sunset (angle=π, leftward) through top
	// angle formula: angle = -π/2 + worldTime * 2π → sunrise @worldTime=0.25 = angle 0
	text.drawArc(cx, cy, ri, ro, 0.0f, pi,
	             {0.76f, 0.58f, 0.12f, 0.88f}, aspect, 32); // gold day half
	// Night arc: sunset to next sunrise (bottom half)
	text.drawArc(cx, cy, ri, ro, pi, 2*pi,
	             {0.10f, 0.15f, 0.32f, 0.88f}, aspect, 32); // deep blue night half

	// Outer rim
	text.drawArc(cx, cy, ro, ro + 0.004f, 0, 2*pi,
	             {0.22f, 0.18f, 0.12f, 0.80f}, aspect, 32);

	// Inner dark disk
	text.drawArc(cx, cy, 0, ri - 0.003f, 0, 2*pi,
	             {0.08f, 0.06f, 0.05f, 0.92f}, aspect, 24);

	// Current time marker: bright dot on the ring
	float timeAngle = -pi / 2.0f + t * 2.0f * pi;
	float midR = (ri + ro) * 0.5f;
	float dotCx = cx + midR * std::cos(timeAngle);
	float dotCy = cy + midR * aspect * std::sin(timeAngle);
	float ds = 0.010f;
	bool isDay = (t >= 0.25f && t < 0.75f);
	glm::vec4 dotColor = isDay ? glm::vec4(1.0f, 0.98f, 0.85f, 1.0f)
	                           : glm::vec4(0.85f, 0.92f, 1.0f,  1.0f);
	// Glow behind dot
	text.drawArc(dotCx, dotCy, 0, ds * 1.6f, 0, 2*pi,
	             {dotColor.r, dotColor.g, dotColor.b, 0.35f}, aspect, 16);
	// Dot itself
	text.drawArc(dotCx, dotCy, 0, ds, 0, 2*pi, dotColor, aspect, 16);

	// Time text centered below clock
	int hours = (int)(t * 24.0f) % 24;
	int mins  = (int)(t * 24.0f * 60.0f) % 60;
	char timeBuf[12];
	snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hours, mins);
	float tw = (int)strlen(timeBuf) * 0.018f * 0.48f;
	text.drawText(timeBuf, cx - tw * 0.5f, cy - ro * aspect - 0.022f,
	              0.48f, {0.90f, 0.83f, 0.65f, 0.80f}, aspect);
}

// ================================================================
// Mode + hint label — top-left, minimal
// ================================================================
void HUD::renderModeLabel(const HUDContext& ctx, TextRenderer& text) {
	const char* modeNames[] = {"FPS", "TPS", "RPG", "RTS"};
	const char* adminTag = (ctx.state == GameState::ADMIN) ? " [ADMIN]" : "";
	// Note: fly status shown via F3 debug overlay, not here (too long for HUD)
	char hud[64];
	snprintf(hud, sizeof(hud), "[%s]%s  [V]cam  [Tab]inv  [F3]dbg",
	         modeNames[(int)ctx.camera.mode], adminTag);
	text.drawText(hud, -0.98f, 0.92f, 0.55f, {1,1,1,0.40f}, ctx.aspect);
}

// ================================================================
// Inventory panel — right side (Tab key)
// ================================================================
void HUD::renderInventoryPanel(const HUDContext& ctx, TextRenderer& text) {
	float panelW = 0.46f, panelH = 1.40f;
	float panelX = 0.52f, panelY = -0.70f;
	float bw = 0.003f;

	text.drawRect(panelX, panelY, panelW, panelH, {0.07f, 0.06f, 0.04f, 0.90f});
	glm::vec4 bc = {0.32f, 0.26f, 0.16f, 0.80f};
	text.drawRect(panelX, panelY, panelW, bw, bc);
	text.drawRect(panelX, panelY + panelH - bw, panelW, bw, bc);
	text.drawRect(panelX, panelY, bw, panelH, bc);
	text.drawRect(panelX + panelW - bw, panelY, bw, panelH, bc);

	text.drawText("Inventory", panelX + 0.10f, panelY + panelH - 0.06f,
	              0.85f, {0.90f, 0.82f, 0.55f, 1}, ctx.aspect);
	text.drawRect(panelX + 0.02f, panelY + panelH - 0.08f,
	              panelW - 0.04f, 0.002f, {0.32f, 0.26f, 0.16f, 0.55f});

	auto items = ctx.inventory.items();
	float rowH = 0.046f;
	float rowY = panelY + panelH - 0.12f;
	float nameX = panelX + 0.04f;
	float countX = panelX + panelW - 0.10f;

	text.drawText("Item",  nameX,  rowY, 0.52f, {0.55f, 0.50f, 0.38f, 0.80f}, ctx.aspect);
	text.drawText("Qty",  countX, rowY, 0.52f, {0.55f, 0.50f, 0.38f, 0.80f}, ctx.aspect);
	rowY -= rowH * 0.75f;

	int rowIdx = 0;
	for (auto& [id, count] : items) {
		if (rowY < panelY + 0.02f) break;
		if (rowIdx % 2 == 0)
			text.drawRect(nameX - 0.02f, rowY - 0.004f,
			              panelW - 0.04f, rowH - 0.004f,
			              {0.15f, 0.12f, 0.08f, 0.40f});

		const BlockDef* bdef = ctx.blocks.find(id);
		glm::vec3 sc = bdef ? bdef->color_top : glm::vec3(0.50f, 0.60f, 0.75f);
		float sz = rowH * 0.58f;
		text.drawRect(nameX - 0.01f, rowY + 0.004f, sz, sz,
		              {sc.r, sc.g, sc.b, 0.90f});

		std::string dn = id;
		if (!dn.empty()) dn[0] = (char)toupper((unsigned char)dn[0]);
		for (auto& c : dn) if (c == '_') c = ' ';

		text.drawText(dn.c_str(), nameX + sz + 0.012f, rowY,
		              0.48f, {0.85f, 0.82f, 0.75f, 1}, ctx.aspect);
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", count);
		text.drawText(buf, countX, rowY,
		              0.48f, {0.70f, 0.80f, 1.0f, 1}, ctx.aspect);
		rowY -= rowH;
		rowIdx++;
	}

	char footer[64];
	snprintf(footer, sizeof(footer), "%d item types", ctx.inventory.distinctCount());
	text.drawText(footer, panelX + 0.04f, panelY + 0.02f,
	              0.42f, {0.45f, 0.40f, 0.32f, 0.70f}, ctx.aspect);
}

// ================================================================
// Debug overlay (F3)
// ================================================================
void HUD::renderDebugOverlay(const HUDContext& ctx, TextRenderer& text) {
	if (!ctx.showDebug) return;
	char dbg[256];
	auto& p = ctx.camera.player.feetPos;
	ChunkPos cp = World::worldToChunk((int)p.x, (int)p.y, (int)p.z);
	float lineH = 0.065f;
	float x = -0.98f, y = 0.84f;
	// FPS — red when below 30, yellow below 45, green/white otherwise
	glm::vec4 fpsCol = {1,1,1,0.80f};
	if (ctx.fps < 30.0f)      fpsCol = {1.0f, 0.3f, 0.3f, 0.90f};
	else if (ctx.fps < 45.0f) fpsCol = {1.0f, 0.85f, 0.3f, 0.90f};
	snprintf(dbg, sizeof(dbg), "FPS: %.0f", ctx.fps);
	text.drawText(dbg, x, y, 0.65f, fpsCol, ctx.aspect); y -= lineH;
	snprintf(dbg, sizeof(dbg), "CliXYZ: %.1f / %.1f / %.1f", ctx.clientPos.x, ctx.clientPos.y, ctx.clientPos.z);
	text.drawText(dbg, x, y, 0.65f, {1,1,1,0.80f}, ctx.aspect); y -= lineH;
	snprintf(dbg, sizeof(dbg), "SrvXYZ: %.1f / %.1f / %.1f", ctx.serverPos.x, ctx.serverPos.y, ctx.serverPos.z);
	text.drawText(dbg, x, y, 0.65f, {1,1,1,0.80f}, ctx.aspect); y -= lineH;
	// PosErr² — red when > 4.0 (2-block divergence)
	glm::vec4 errCol = ctx.posErrorSq > 4.0f
		? glm::vec4{1.0f, 0.3f, 0.3f, 0.95f}
		: glm::vec4{0.5f, 1.0f, 0.5f, 0.85f};
	snprintf(dbg, sizeof(dbg), "PosErr2: %.2f", ctx.posErrorSq);
	text.drawText(dbg, x, y, 0.65f, errCol, ctx.aspect); y -= lineH;
	snprintf(dbg, sizeof(dbg), "Chunk: %d %d %d", cp.x, cp.y, cp.z);
	text.drawText(dbg, x, y, 0.65f, {1,1,1,0.80f}, ctx.aspect); y -= lineH;
	snprintf(dbg, sizeof(dbg), "Entities: %zu  Particles: %zu", ctx.entityCount, ctx.particleCount);
	text.drawText(dbg, x, y, 0.65f, {1,1,1,0.80f}, ctx.aspect); y -= lineH;
	// Show controlled entity when driving an NPC (via [/] cycle or Control button).
	// Orange so it stands out — reminds the user their input is steering another body.
	if (ctx.controlledId != ENTITY_NONE) {
		snprintf(dbg, sizeof(dbg), "Controlled: %s #%u",
			ctx.controlledType.c_str(), ctx.controlledId);
		text.drawText(dbg, x, y, 0.65f, {1.0f, 0.75f, 0.3f, 0.95f}, ctx.aspect); y -= lineH;
	}
	snprintf(dbg, sizeof(dbg), "Time: %.3f  Sun: %.2f", ctx.worldTime, ctx.sunStrength);
	text.drawText(dbg, x, y, 0.65f, {1,1,1,0.80f}, ctx.aspect); y -= lineH;
	if (ctx.hit) {
		auto& bp = ctx.hit->blockPos;
		BlockId bid = ctx.chunkSource ? ctx.chunkSource->getBlock(bp.x, bp.y, bp.z) : BLOCK_AIR;
		const BlockDef& bdef = ctx.blocks.get(bid);
		snprintf(dbg, sizeof(dbg), "Block: %s (%d,%d,%d)", bdef.display_name.c_str(), bp.x, bp.y, bp.z);
		text.drawText(dbg, x, y, 0.65f, {1,1,1,0.80f}, ctx.aspect);
	}
}

// ================================================================
// Frame profiler overlay (F5)
// ================================================================
void HUD::renderProfilerOverlay(const HUDContext& ctx, TextRenderer& text) {
	if (!ctx.showProfiler) return;
	char buf[256];
	float lineH = 0.065f;
	float x = 0.40f, y = 0.84f;

	// Semi-transparent background panel
	text.drawRect(x - 0.02f, y - 0.34f, 0.60f, 0.40f, {0.0f, 0.0f, 0.0f, 0.55f});

	snprintf(buf, sizeof(buf), "-- Frame Profiler --");
	text.drawText(buf, x, y, 0.60f, {1.0f, 0.85f, 0.3f, 0.95f}, ctx.aspect); y -= lineH;
	snprintf(buf, sizeof(buf), "FPS: %.0f  (%.1f ms)", ctx.fps, ctx.profileTotalMs);
	text.drawText(buf, x, y, 0.60f, {1,1,1,0.90f}, ctx.aspect); y -= lineH;

	auto barColor = [](float ms) -> glm::vec4 {
		if (ms > 10.0f) return {1.0f, 0.3f, 0.3f, 0.90f};  // red: >10ms
		if (ms >  5.0f) return {1.0f, 0.8f, 0.2f, 0.90f};  // yellow: >5ms
		return {0.4f, 1.0f, 0.4f, 0.90f};                   // green
	};

	snprintf(buf, sizeof(buf), "World:    %6.2f ms", ctx.profileWorldMs);
	text.drawText(buf, x, y, 0.60f, barColor(ctx.profileWorldMs), ctx.aspect); y -= lineH;
	snprintf(buf, sizeof(buf), "Entities: %6.2f ms", ctx.profileEntityMs);
	text.drawText(buf, x, y, 0.60f, barColor(ctx.profileEntityMs), ctx.aspect); y -= lineH;
	snprintf(buf, sizeof(buf), "HUD:      %6.2f ms", ctx.profileHudMs);
	text.drawText(buf, x, y, 0.60f, barColor(ctx.profileHudMs), ctx.aspect); y -= lineH;
}

// ================================================================
// Entity tooltip (crosshair on entity)
// ================================================================
void HUD::renderEntityTooltip(const HUDContext& ctx, TextRenderer& text) {
	if (!ctx.entityHit) return;
	auto& eh = *ctx.entityHit;

	bool hasGoal = !eh.goalText.empty();
	float bgW = 0.36f;
	float bgH = hasGoal ? 0.10f : 0.058f;
	float cx = 0.0f, cy = -0.08f;

	// Background panel
	text.drawRect(cx - bgW*0.5f, cy - bgH*0.5f, bgW, bgH, {0.04f,0.03f,0.02f,0.72f});
	float bw = 0.002f;
	glm::vec4 bc = {0.30f, 0.24f, 0.14f, 0.70f};
	text.drawRect(cx-bgW*0.5f, cy-bgH*0.5f, bgW, bw, bc);
	text.drawRect(cx-bgW*0.5f, cy+bgH*0.5f-bw, bgW, bw, bc);
	text.drawRect(cx-bgW*0.5f, cy-bgH*0.5f, bw, bgH, bc);
	text.drawRect(cx+bgW*0.5f-bw, cy-bgH*0.5f, bw, bgH, bc);

	// Entity name
	char label[128];
	const char* rawId = eh.typeId.c_str();
	const char* dispName = rawId;
	snprintf(label, sizeof(label), "%s", dispName);
	if (label[0]) label[0] = (char)toupper((unsigned char)label[0]);
	float nameY = hasGoal ? cy + 0.020f : cy - 0.008f;
	text.drawText(label, cx - bgW*0.5f + 0.012f, nameY,
	              0.65f, {1.0f, 0.88f, 0.50f, 1}, ctx.aspect);

	// Goal line
	if (hasGoal) {
		snprintf(label, sizeof(label), "%s", eh.goalText.c_str());
		glm::vec4 gc = eh.hasError ? glm::vec4(1,0.3f,0.3f,1) : glm::vec4(0.75f,1,0.75f,0.90f);
		text.drawText(label, cx - bgW*0.5f + 0.012f, cy - 0.028f,
		              0.52f, gc, ctx.aspect);
	}
}

// ================================================================
void HUD::init(Shader& highlightShader) { (void)highlightShader; }
void HUD::shutdown() {}

void HUD::render(const HUDContext& ctx, TextRenderer& text, Shader& highlightShader) {
	(void)highlightShader;
	// Inventory panel is now an ImGui window rendered in game.cpp (drag/drop support)
	renderHealthBars(ctx, text);
	renderModeLabel(ctx, text);
	renderTimeOfDay(ctx, text);
	renderEntityTooltip(ctx, text);
	renderDebugOverlay(ctx, text);
	renderProfilerOverlay(ctx, text);
}

} // namespace civcraft
