#pragma once

#include "game/types.h"
#include "client/text.h"
#include "client/shader.h"
#include "client/camera.h"
#include "client/particles.h"
#include "client/raycast.h"
#include "client/entity_raycast.h"
#include "server/world.h"
#include "shared/inventory.h"
#include "client/gl.h"

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
	bool showInventory;
	const std::optional<RayHit>& hit;
	std::optional<EntityHit> entityHit;  // entity under crosshair
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
	void renderHotbar(const HUDContext& ctx, TextRenderer& text);
	void renderInventoryPanel(const HUDContext& ctx, TextRenderer& text);
	void renderHealthBars(const HUDContext& ctx, TextRenderer& text);
	void renderModeLabel(const HUDContext& ctx, TextRenderer& text);
	void renderTimeOfDay(const HUDContext& ctx, TextRenderer& text);
	void renderDebugOverlay(const HUDContext& ctx, TextRenderer& text);
	void renderEntityTooltip(const HUDContext& ctx, TextRenderer& text);
};

} // namespace aicraft
