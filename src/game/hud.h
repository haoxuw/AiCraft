#pragma once

#include "game/types.h"
#include "client/text.h"
#include "client/shader.h"
#include "client/camera.h"
#include "client/particles.h"
#include "client/raycast.h"
#include "common/world.h"
#include "common/inventory.h"
#include <glad/gl.h>

namespace aicraft {

struct HUDContext {
	float aspect;
	GameState state;
	int selectedSlot;
	const Inventory& inventory;
	const Camera& camera;
	const World& world;
	float worldTime;
	float fps;
	bool showDebug;
	const std::optional<RayHit>& hit;
	float sunStrength;
	size_t entityCount;
	size_t particleCount;
	int playerHP, playerMaxHP;
	float playerHunger;
};

class HUD {
public:
	void init(Shader& highlightShader);
	void shutdown();
	void render(const HUDContext& ctx, TextRenderer& text, Shader& highlightShader);

private:
	void renderHotbar(const HUDContext& ctx, TextRenderer& text, Shader& highlightShader);
	void renderHealthBars(const HUDContext& ctx, TextRenderer& text);
	void renderModeLabel(const HUDContext& ctx, TextRenderer& text);
	void renderTimeOfDay(const HUDContext& ctx, TextRenderer& text);
	void renderDebugOverlay(const HUDContext& ctx, TextRenderer& text);

	GLuint m_quadVAO = 0, m_quadVBO = 0;
};

} // namespace aicraft
