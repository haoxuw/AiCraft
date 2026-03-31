#include "game/hud.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>

namespace aicraft {

void HUD::init(Shader& highlightShader) {
	float uq[] = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
	glGenVertexArrays(1, &m_quadVAO);
	glGenBuffers(1, &m_quadVBO);
	glBindVertexArray(m_quadVAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(uq), uq, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), nullptr);
	glEnableVertexAttribArray(0);
}

void HUD::shutdown() {
	if (m_quadVBO) { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }
	if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
}

void HUD::render(const HUDContext& ctx, TextRenderer& text, Shader& highlightShader) {
	renderHotbar(ctx, text, highlightShader);
	renderHealthBars(ctx, text);
	renderModeLabel(ctx, text);
	renderTimeOfDay(ctx, text);
	renderDebugOverlay(ctx, text);
}

void HUD::renderHotbar(const HUDContext& ctx, TextRenderer& text, Shader& highlightShader) {
	float slotSize = 0.06f;
	float padding = 0.008f;
	float totalWidth = HOTBAR_SIZE * (slotSize + padding) - padding;
	float startX = -totalWidth / 2.0f;
	float startY = -0.92f;

	highlightShader.use();
	glBindVertexArray(m_quadVAO);

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	GLint loc = glGetUniformLocation(highlightShader.id(), "uColor");

	for (int i = 0; i < HOTBAR_SIZE; i++) {
		float x = startX + i * (slotSize + padding);
		float y = startY;

		// Slot background
		glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
		model = glm::scale(model, glm::vec3(slotSize, slotSize * ctx.aspect, 1.0f));
		highlightShader.setMat4("uMVP", model);

		if (i == ctx.selectedSlot)
			glUniform4f(loc, 1.0f, 1.0f, 1.0f, 0.75f);
		else
			glUniform4f(loc, 0.08f, 0.08f, 0.08f, 0.50f);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		// Block color from inventory
		auto& slot = ctx.inventory.slot(i);
		if (!slot.empty()) {
			const BlockDef* bdef = ctx.world.blocks.find(slot.type);
			glm::vec3 c = bdef ? bdef->color_top : glm::vec3(0.5f);

			float inset = 0.15f;
			glm::mat4 inner = glm::translate(glm::mat4(1.0f),
				glm::vec3(x + slotSize * inset, y + slotSize * ctx.aspect * inset, 0.0f));
			inner = glm::scale(inner,
				glm::vec3(slotSize * (1.0f - 2*inset), slotSize * ctx.aspect * (1.0f - 2*inset), 1.0f));
			highlightShader.setMat4("uMVP", inner);
			glUniform4f(loc, c.r, c.g, c.b, 0.9f);
			glDrawArrays(GL_TRIANGLES, 0, 6);

			// Stack count
			if (slot.count > 1) {
				char countStr[8]; snprintf(countStr, 8, "%d", slot.count);
				text.drawText(countStr,
					x + slotSize * 0.55f,
					y + slotSize * ctx.aspect * 0.15f,
					0.6f, {1,1,1,0.9f}, ctx.aspect);
			}
		}
	}

	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

void HUD::renderHealthBars(const HUDContext& ctx, TextRenderer& text) {
	if (ctx.state != GameState::SURVIVAL)
		return;

	float barW = 0.30f;
	float barH = 0.018f;
	float barX = -barW / 2.0f;
	float hpY = -0.84f;
	float hungerY = -0.87f;

	// HP bar background
	text.drawRect(barX, hpY, barW, barH * ctx.aspect, {0.15f, 0.0f, 0.0f, 0.6f});
	// HP bar fill
	float hpFrac = (float)ctx.playerHP / ctx.playerMaxHP;
	text.drawRect(barX, hpY, barW * hpFrac, barH * ctx.aspect, {0.85f, 0.15f, 0.15f, 0.8f});

	// Hunger bar background
	text.drawRect(barX, hungerY, barW, barH * ctx.aspect, {0.10f, 0.08f, 0.0f, 0.6f});
	// Hunger bar fill
	float hungerFrac = ctx.playerHunger / 20.0f;
	text.drawRect(barX, hungerY, barW * hungerFrac, barH * ctx.aspect, {0.75f, 0.55f, 0.10f, 0.8f});
}

void HUD::renderModeLabel(const HUDContext& ctx, TextRenderer& text) {
	const char* modeNames[] = {"1st Person","3rd Person","God View","RTS"};
	const char* gameNames[] = {"","Creative","Survival"};
	char hud[128];
	snprintf(hud, sizeof(hud), "%s - %s [V]view [F3]debug",
		gameNames[(int)ctx.state], modeNames[(int)ctx.camera.mode]);
	text.drawText(hud, -0.98f, 0.92f, 0.7f, {1,1,1,0.6f}, ctx.aspect);
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
		BlockId bid = const_cast<World&>(ctx.world).getBlock(bp.x, bp.y, bp.z);
		const BlockDef& bdef = ctx.world.blocks.get(bid);
		snprintf(dbg, sizeof(dbg), "Looking at: %s (%d,%d,%d)",
			bdef.display_name.c_str(), bp.x, bp.y, bp.z);
		text.drawText(dbg, -0.98f, 0.54f, 0.7f, {1,1,1,0.8f}, ctx.aspect);
	}
}

} // namespace aicraft
