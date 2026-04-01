#include "game/hud.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>

namespace agentworld {

void HUD::init(Shader& highlightShader) {
	// No longer needed for hotbar (using TextRenderer), but kept for future use
	(void)highlightShader;
}

void HUD::shutdown() {
}

void HUD::render(const HUDContext& ctx, TextRenderer& text, Shader& highlightShader) {
	(void)highlightShader;
	renderHotbar(ctx, text);
	if (ctx.showInventory)
		renderInventoryPanel(ctx, text);
	renderHealthBars(ctx, text);
	renderModeLabel(ctx, text);
	renderTimeOfDay(ctx, text);
	renderEntityTooltip(ctx, text);
	renderDebugOverlay(ctx, text);
}

// ================================================================
// Hotbar: 10 slots at bottom of screen (keys 1-9, 0)
// ================================================================
void HUD::renderHotbar(const HUDContext& ctx, TextRenderer& text) {
	const int slots = Inventory::HOTBAR_SLOTS;
	float slotW = 0.058f;
	float slotH = slotW * ctx.aspect;
	float gap = 0.006f;
	float totalW = slots * (slotW + gap) - gap;
	float startX = -totalW / 2.0f;
	float startY = -0.96f;

	for (int i = 0; i < slots; i++) {
		float x = startX + i * (slotW + gap);
		bool selected = (i == ctx.selectedSlot);

		// Slot background
		glm::vec4 bg = selected ? glm::vec4(0.35f, 0.40f, 0.50f, 0.85f)
		                        : glm::vec4(0.10f, 0.10f, 0.12f, 0.65f);
		text.drawRect(x, startY, slotW, slotH, bg);

		// Selection border
		if (selected) {
			float bw = 0.003f;
			glm::vec4 bc = {0.9f, 0.9f, 1.0f, 0.9f};
			text.drawRect(x, startY, slotW, bw, bc);
			text.drawRect(x, startY + slotH - bw, slotW, bw, bc);
			text.drawRect(x, startY, bw, slotH, bc);
			text.drawRect(x + slotW - bw, startY, bw, slotH, bc);
		}

		// Item content
		std::string itemId = ctx.inventory.hotbar(i);
		int itemCount = ctx.inventory.hotbarCount(i);
		if (!itemId.empty() && itemCount > 0) {
			const BlockDef* bdef = ctx.blocks.find(itemId);
			glm::vec3 c = bdef ? bdef->color_top : glm::vec3(0.45f, 0.55f, 0.70f);

			float inset = slotW * 0.15f;
			float insetH = slotH * 0.15f;
			text.drawRect(x + inset, startY + insetH,
				slotW - 2*inset, slotH - 2*insetH,
				{c.r, c.g, c.b, 0.9f});

			// Count (bottom-right of slot)
			char buf[16];
			snprintf(buf, sizeof(buf), "%d", itemCount);
			text.drawText(buf,
				x + slotW * 0.52f, startY + slotH * 0.08f,
				0.5f, {1, 1, 1, 0.9f}, ctx.aspect);
		}

		// Key label (top-left, small)
		char key[4];
		snprintf(key, sizeof(key), "%d", (i + 1) % 10);
		text.drawText(key,
			x + slotW * 0.08f, startY + slotH * 0.68f,
			0.4f, {0.6f, 0.6f, 0.6f, 0.5f}, ctx.aspect);
	}
}

// ================================================================
// Inventory panel: Diablo-style -- all items sorted, counts shown.
// Toggle with Tab key. Shows on right side of screen.
// ================================================================
void HUD::renderInventoryPanel(const HUDContext& ctx, TextRenderer& text) {
	float panelW = 0.48f;
	float panelH = 1.4f;
	float panelX = 0.50f;
	float panelY = -0.70f;

	// Panel background
	text.drawRect(panelX, panelY, panelW, panelH, {0.08f, 0.08f, 0.12f, 0.88f});
	// Border
	float bw = 0.003f;
	glm::vec4 bc = {0.35f, 0.35f, 0.45f, 0.8f};
	text.drawRect(panelX, panelY, panelW, bw, bc);
	text.drawRect(panelX, panelY + panelH - bw, panelW, bw, bc);
	text.drawRect(panelX, panelY, bw, panelH, bc);
	text.drawRect(panelX + panelW - bw, panelY, bw, panelH, bc);

	// Title
	text.drawText("Inventory", panelX + 0.12f, panelY + panelH - 0.06f,
		0.9f, {0.9f, 0.85f, 0.6f, 1}, ctx.aspect);
	// Separator
	text.drawRect(panelX + 0.02f, panelY + panelH - 0.08f,
		panelW - 0.04f, 0.002f, {0.35f, 0.35f, 0.45f, 0.6f});

	// Item list: sorted by ID, skip zero counts
	auto items = ctx.inventory.items();
	float rowH = 0.045f;
	float rowY = panelY + panelH - 0.12f;
	float nameX = panelX + 0.04f;
	float countX = panelX + panelW - 0.10f;

	// Column headers
	text.drawText("Item", nameX, rowY, 0.55f, {0.6f, 0.6f, 0.5f, 0.8f}, ctx.aspect);
	text.drawText("Qty", countX, rowY, 0.55f, {0.6f, 0.6f, 0.5f, 0.8f}, ctx.aspect);
	rowY -= rowH * 0.8f;

	int rowIdx = 0;
	for (auto& [id, count] : items) {
		if (rowY < panelY + 0.02f) break; // stop if we run out of panel space

		// Alternate row shading
		if (rowIdx % 2 == 0)
			text.drawRect(nameX - 0.02f, rowY - 0.005f,
				panelW - 0.04f, rowH - 0.005f,
				{0.14f, 0.14f, 0.18f, 0.4f});

		// Item color swatch
		const BlockDef* bdef = ctx.blocks.find(id);
		glm::vec3 swatchColor = bdef ? bdef->color_top : glm::vec3(0.45f, 0.55f, 0.70f);
		float swatchSz = rowH * 0.6f;
		text.drawRect(nameX - 0.01f, rowY + 0.003f, swatchSz, swatchSz,
			{swatchColor.r, swatchColor.g, swatchColor.b, 0.9f});

		// Item name: strip "base:" prefix for display
		std::string displayName = id;
		if (displayName.substr(0, 5) == "base:")
			displayName = displayName.substr(5);
		// Capitalize first letter
		if (!displayName.empty())
			displayName[0] = toupper(displayName[0]);
		// Replace underscores with spaces
		for (auto& c : displayName)
			if (c == '_') c = ' ';

		text.drawText(displayName.c_str(),
			nameX + swatchSz + 0.01f, rowY,
			0.5f, {0.85f, 0.85f, 0.85f, 1}, ctx.aspect);

		// Count
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", count);
		text.drawText(buf, countX, rowY,
			0.5f, {0.7f, 0.8f, 1.0f, 1}, ctx.aspect);

		rowY -= rowH;
		rowIdx++;
	}

	// Footer: total distinct items
	char footer[64];
	snprintf(footer, sizeof(footer), "%d items", ctx.inventory.distinctCount());
	text.drawText(footer, panelX + 0.04f, panelY + 0.02f,
		0.45f, {0.5f, 0.5f, 0.5f, 0.7f}, ctx.aspect);
}

// ================================================================
// Health + Hunger bars (survival only) -- bottom-left, labeled
// ================================================================
void HUD::renderHealthBars(const HUDContext& ctx, TextRenderer& text) {
	if (ctx.state != GameState::SURVIVAL)
		return;

	float barW = 0.22f;
	float barH = 0.014f * ctx.aspect;
	float baseX = -0.97f;
	float hpY = -0.88f;
	float hungerY = hpY - barH - 0.018f;
	float labelW = 0.05f;

	// -- HP --
	// Label
	text.drawText("HP", baseX, hpY + 0.002f, 0.45f, {0.9f, 0.3f, 0.3f, 0.9f}, ctx.aspect);
	// Background
	float bx = baseX + labelW;
	text.drawRect(bx, hpY, barW, barH, {0.20f, 0.05f, 0.05f, 0.7f});
	// Fill
	float hpFrac = std::max(0.0f, (float)ctx.playerHP / ctx.playerMaxHP);
	glm::vec4 hpColor = hpFrac > 0.3f ? glm::vec4(0.75f, 0.15f, 0.12f, 0.85f)
	                                    : glm::vec4(0.90f, 0.10f, 0.10f, 0.95f); // flash red when low
	text.drawRect(bx, hpY, barW * hpFrac, barH, hpColor);
	// Border
	float bw = 0.0015f;
	text.drawRect(bx, hpY, barW, bw, {0.5f, 0.2f, 0.2f, 0.5f});
	text.drawRect(bx, hpY + barH - bw, barW, bw, {0.5f, 0.2f, 0.2f, 0.5f});
	// Value
	char hpStr[16]; snprintf(hpStr, 16, "%d/%d", ctx.playerHP, ctx.playerMaxHP);
	text.drawText(hpStr, bx + barW + 0.01f, hpY + 0.002f, 0.4f, {0.8f, 0.8f, 0.8f, 0.7f}, ctx.aspect);

	// -- Hunger --
	text.drawText("Food", baseX - 0.01f, hungerY + 0.002f, 0.45f, {0.8f, 0.65f, 0.2f, 0.9f}, ctx.aspect);
	text.drawRect(bx, hungerY, barW, barH, {0.15f, 0.12f, 0.03f, 0.7f});
	float hungerFrac = std::max(0.0f, ctx.playerHunger / 20.0f);
	text.drawRect(bx, hungerY, barW * hungerFrac, barH, {0.72f, 0.55f, 0.10f, 0.85f});
	text.drawRect(bx, hungerY, barW, bw, {0.5f, 0.4f, 0.15f, 0.5f});
	text.drawRect(bx, hungerY + barH - bw, barW, bw, {0.5f, 0.4f, 0.15f, 0.5f});
	char hungerStr[16]; snprintf(hungerStr, 16, "%.0f/20", ctx.playerHunger);
	text.drawText(hungerStr, bx + barW + 0.01f, hungerY + 0.002f, 0.4f, {0.8f, 0.8f, 0.8f, 0.7f}, ctx.aspect);
}

void HUD::renderModeLabel(const HUDContext& ctx, TextRenderer& text) {
	const char* modeNames[] = {"1st Person","3rd Person","RPG","RTS"};
	const char* gameNames[] = {"","Creative","Survival"};
	char hud[128];
	snprintf(hud, sizeof(hud), "%s - %s [V]view [Tab]inventory [F3]debug",
		gameNames[(int)ctx.state], modeNames[(int)ctx.camera.mode]);
	text.drawText(hud, -0.98f, 0.92f, 0.65f, {1,1,1,0.5f}, ctx.aspect);
}

void HUD::renderTimeOfDay(const HUDContext& ctx, TextRenderer& text) {
	int hours = (int)(ctx.worldTime * 24.0f) % 24;
	int mins = (int)(ctx.worldTime * 24.0f * 60.0f) % 60;
	char timeBuf[32];
	snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hours, mins);
	text.drawText(timeBuf, 0.82f, 0.92f, 0.8f, {1,1,0.7f,0.7f}, ctx.aspect);
}

void HUD::renderDebugOverlay(const HUDContext& ctx, TextRenderer& text) {
	if (!ctx.showDebug)
		return;

	char dbg[256];
	auto& p = ctx.camera.player.feetPos;
	ChunkPos cp = World::worldToChunk((int)p.x, (int)p.y, (int)p.z);

	snprintf(dbg, sizeof(dbg), "FPS: %.0f", ctx.fps);
	text.drawText(dbg, -0.98f, 0.84f, 0.7f, {1,1,1,0.8f}, ctx.aspect);

	snprintf(dbg, sizeof(dbg), "XYZ: %.1f / %.1f / %.1f", p.x, p.y, p.z);
	text.drawText(dbg, -0.98f, 0.78f, 0.7f, {1,1,1,0.8f}, ctx.aspect);

	snprintf(dbg, sizeof(dbg), "Chunk: %d %d %d", cp.x, cp.y, cp.z);
	text.drawText(dbg, -0.98f, 0.72f, 0.7f, {1,1,1,0.8f}, ctx.aspect);

	snprintf(dbg, sizeof(dbg), "Entities: %zu  Particles: %zu",
		ctx.entityCount, ctx.particleCount);
	text.drawText(dbg, -0.98f, 0.66f, 0.7f, {1,1,1,0.8f}, ctx.aspect);

	snprintf(dbg, sizeof(dbg), "Time: %.3f  Sun: %.2f", ctx.worldTime, ctx.sunStrength);
	text.drawText(dbg, -0.98f, 0.60f, 0.7f, {1,1,1,0.8f}, ctx.aspect);

	if (ctx.hit) {
		auto& bp = ctx.hit->blockPos;
		BlockId bid = ctx.chunkSource ? ctx.chunkSource->getBlock(bp.x, bp.y, bp.z) : BLOCK_AIR;
		const BlockDef& bdef = ctx.blocks.get(bid);
		snprintf(dbg, sizeof(dbg), "Looking at: %s (%d,%d,%d)",
			bdef.display_name.c_str(), bp.x, bp.y, bp.z);
		text.drawText(dbg, -0.98f, 0.54f, 0.7f, {1,1,1,0.8f}, ctx.aspect);
	}
}

// ================================================================
// Entity tooltip: show name + goal when crosshair is on an entity
// ================================================================
void HUD::renderEntityTooltip(const HUDContext& ctx, TextRenderer& text) {
	if (!ctx.entityHit) return;

	auto& eh = *ctx.entityHit;

	// Tooltip near center of screen (just below crosshair)
	float cx = 0.0f;
	float cy = -0.08f;

	// Entity name
	char label[128];
	snprintf(label, sizeof(label), "%s", eh.typeId.c_str());

	// Background
	float bgW = 0.35f;
	float bgH = eh.goalText.empty() ? 0.06f : 0.10f;
	text.drawRect(cx - bgW/2, cy - bgH/2, bgW, bgH, {0, 0, 0, 0.6f});

	// Name line
	text.drawText(label, cx - bgW/2 + 0.01f, cy + 0.02f, 0.7f, {1, 0.9f, 0.5f, 1}, ctx.aspect);

	// Goal line (green normal, red on error)
	if (!eh.goalText.empty()) {
		snprintf(label, sizeof(label), "Goal: %s", eh.goalText.c_str());
		glm::vec4 goalColor = eh.hasError ? glm::vec4(1, 0.3f, 0.3f, 1) : glm::vec4(0.8f, 1, 0.8f, 0.9f);
		text.drawText(label, cx - bgW/2 + 0.01f, cy - 0.03f, 0.6f, goalColor, ctx.aspect);
	}
}

} // namespace agentworld
