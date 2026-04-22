#pragma once

// OOP-decomposed render pipeline for Game.
//
// Game used to own every render*() method. It still owns the *state* (camera,
// server, inventory, m_rhi …) but the presentation code is now split into five
// friend renderer classes, each responsible for one screen-space concern:
//
//   WorldRenderer     — sky, shadows, terrain, entities, particles, ribbons
//   HudRenderer       — 2D HUD (sundial, HP, crosshair)
//   MenuRenderer      — main menu, in-game pause menu, death screen
//   PanelRenderer     — F3 debug overlay, F6 tuning, H handbook
//   EntityUiRenderer  — entity inspect card, RTS selection box
//
// Each renderer holds only a `Game&` — no duplicated state. The friend
// declarations in Game give them private access; method bodies prefix all
// Game members/methods through a local `Game& g = game_;` alias.

namespace civcraft::vk {

class Game;

class WorldRenderer {
public:
	explicit WorldRenderer(Game& g) : game_(g) {}
	void renderWorld(float wallTime);
	void renderEntities(float wallTime);
	void renderEffects(float wallTime);
private:
	Game& game_;

	// Sub-passes of renderEffects(). Each builds its own SceneParams
	// from the current camera; pulled out because renderEffects was a
	// 560-line scroll otherwise.
	void renderBreakEffects(float wallTime);      // break crack + ghost + burst
	void renderSelectionMarkers(float wallTime);  // move order + RTS select rings
};

class HudRenderer {
public:
	explicit HudRenderer(Game& g) : game_(g) {}
	void renderHUD();

	// Inventory + hotbar.
	//   renderInventoryItems3D — main-pass draw of every visible slot's 3D
	//     item model. Must run before the 2D UI pass so box models render into
	//     the main scene and custom-drawn chrome/text composes over them.
	//   renderHotbarBar       — 2D bezel + selection glow + count/key text.
	//   renderInventoryPanel  — Tab-triggered window; player/chest/NPC.
	void renderInventoryItems3D();
	void renderHotbarBar();
	void renderInventoryPanel();
private:
	Game& game_;

	// Sub-passes of renderInventoryPanel(). Kept private; the panel
	// body drives which ones fire each frame.
	void renderInventoryTooltip();   // hover tooltip (name + rarity + count)
};

class MenuRenderer {
public:
	explicit MenuRenderer(Game& g) : game_(g) {}
	void renderMenu();
	void renderGameMenu();
	void renderDeath();
private:
	Game& game_;
};

class PanelRenderer {
public:
	explicit PanelRenderer(Game& g) : game_(g) {}
	void renderDebugOverlay();
	void renderTuningPanel();
	void renderHandbook();
private:
	Game& game_;
};

class EntityUiRenderer {
public:
	explicit EntityUiRenderer(Game& g) : game_(g) {}
	void renderEntityInspect();
	void renderRTSSelect();
	void renderRTSDragCommand();
private:
	Game& game_;
};

} // namespace civcraft::vk
