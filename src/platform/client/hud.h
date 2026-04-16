#pragma once

#include "client/types.h"
#include "shared/constants.h"
#include <string>
#include "client/text.h"
#include "client/shader.h"
#include "client/camera.h"
#include "client/particles.h"
#include "client/raycast.h"
#include "client/entity_raycast.h"
#include "server/world.h"
#include "shared/inventory.h"
#include "client/hotbar.h"
#include "client/gfx.h"

namespace civcraft {

struct HUDContext {
	float aspect;
	GameState state;
	int selectedSlot;
	const Inventory& inventory;
	const Hotbar& hotbar;
	const Camera& camera;
	const BlockRegistry& blocks;
	ChunkSource* chunkSource; // for debug block lookup (nullable)
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
	// Frame profiler (F5)
	bool showProfiler;
	float profileWorldMs, profileEntityMs, profileHudMs, profileTotalMs;
	// Server-client position divergence (shown in F3)
	glm::vec3 serverPos;
	glm::vec3 clientPos;
	float posErrorSq;
	// Controlled entity (shown in F3 when driving a non-player Living).
	// id = ENTITY_NONE means "driving own body" — overlay hides the line.
	EntityId controlledId = ENTITY_NONE;
	std::string controlledType;
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
	void renderProfilerOverlay(const HUDContext& ctx, TextRenderer& text);
	void renderEntityTooltip(const HUDContext& ctx, TextRenderer& text);
};

} // namespace civcraft
