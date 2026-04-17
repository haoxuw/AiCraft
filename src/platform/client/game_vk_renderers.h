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
};

class HudRenderer {
public:
	explicit HudRenderer(Game& g) : game_(g) {}
	void renderHUD();
private:
	Game& game_;
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
private:
	Game& game_;
};

} // namespace civcraft::vk
