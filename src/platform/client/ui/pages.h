#pragma once

// solarium::ui::pages — builders for the simple, mostly-static menu pages.
//
// Each function returns a complete `data:text/html,…` URL for CEF's
// loadUrl. The OOP-lite shape: free functions in a namespace, taking the
// dependencies they need by parameter (theme palette + game ref). No
// hidden state, no lambdas-with-captures, no shared mutable kCss strings
// floating around the boot path.
//
// Pages with substantial state (Handbook, Character Select, Settings,
// Mod Manager, Multiplayer browser, World Picker, Save Slots) are still
// in main.cpp because they read live registry / settings / LAN-browser
// data and would need a richer context object than (Game&) to migrate
// cleanly. That's the next refactor pass.

#include <string>

namespace solarium {
namespace vk { class Game; }
namespace ui {

// Static main menu — title + tagline + six buttons. Doesn't read game
// state at all today (kept by-ref in case a future change wants to e.g.
// gate "Multiplayer" on a settings flag).
std::string mainPage(const vk::Game& game);

// Pause menu opened mid-gameplay (Esc). Resume / Settings / Save & Quit.
std::string pausePage(const vk::Game& game);

// Death overlay. Respawn / Main Menu.
std::string deathPage(const vk::Game& game);

// Multiplayer entry page (NOT the LAN browser — that's multiplayerPage).
// Two static buttons: Join / Host.
std::string multiplayerHubPage(const vk::Game& game);

// LAN-host lobby — "your IP:port, waiting for joiners". Start Game / Cancel.
std::string lobbyPage(const vk::Game& game);

// LAN server browser — `Multiplayer` button leads here. Reads the live
// LanBrowser server list from `game.lanBrowser()`, renders one row per
// discovered server, with a "Hide version mismatches" filter. Rebuilt on
// every Refresh click since the discovered set is dynamic.
std::string multiplayerPage(const vk::Game& game);

// Save-slot picker. Reads the saves/ directory + each save's manifest
// via `solarium::vk::scanSaves`. Tile grid with a "New World" tile and
// one tile per existing save, each with its own delete affordance.
std::string saveSlotsPage(const vk::Game& game);

// Mod manager. Reads `game.artifactRegistry().discoverNamespaces()` for
// the available mod folders + `game.settings().disabled_mods` for the
// current opt-out list. One row per namespace, with an on/off toggle.
// Changes apply on next launch (the registry doesn't hot-reload disabled-
// list changes — see TODO in artifact_registry.h).
std::string modManagerPage(const vk::Game& game);

} // namespace ui
} // namespace solarium
