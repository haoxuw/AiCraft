# CEF Migration — Legacy Code to Delete

The CEF HTML overlay now replaces the native bitmap-font menu (and, when `m_cefMenuActive`
is true, suppresses the native loading bar inside it). The Vulkan-drawn UI for those
states is **dead code** as soon as CEF becomes the only entry point. Track removals here
so we don't delete things that other states still need.

## Status legend

- 🔴 **Dead** — no remaining caller after CEF goes default. Safe to delete.
- 🟡 **Partial** — class has live methods alongside dead ones; surgically remove the dead
  ones, keep the class.
- 🟢 **Keep** — engine-level, not menu UI; CEF is layered on top of these.

---

## Files

### `src/platform/client/game_vk_renderer_menu.cpp` 🟡

Live (in-game UI, **keep**):
- `MenuRenderer::renderGameMenu()` — Esc-during-Playing pause overlay.
- `MenuRenderer::renderDeath()` — death screen.
- shared helpers used by those (`drawMenuFrame`, `drawMenuButton`, `drawMenuList`).

Dead once CEF is default (delete the bodies, keep nothing):
- `MenuRenderer::renderMenu()` — the legacy 5-button main menu, all sub-screens
  (Main, CharacterSelect, Multiplayer, Handbook, Settings, Connecting).
- `drawLoadingBar()` (file-static) — only called from `renderMenu()`'s Connecting case.
- `drawCenteredTitle("SOLARIUM", …)` at line 192 — replaced by HTML `<h1>`.

### `src/platform/client/loading_screen.{h,cpp}` 🔴

Entire `LoadingScreen` class. The Vulkan progress UI no longer renders; the CEF
loading view (TBD) will own it. Removal entails:
- Delete both files.
- Remove `m_loading` field from `Game` (`game_vk.h`).
- Remove `m_loading.tick()`, `m_loading.setWelcome()`, `m_loading.setWorldPrepared()`,
  `m_loading.setChunkProgress()`, `m_loading.setAgentProgress()`, `m_loading.pollDismiss()`,
  `m_loading.reset()` calls in `game_vk.cpp` / `game_vk_playing.cpp`.
- Remove the LoadingScreen reference from the `drawLoadingBar()` signature (already
  removed if `renderMenu` is gone).
- Drop `#include "client/loading_screen.h"` from `game_vk.h`.
- Remove from `CMakeLists.txt` `solarium-ui-vk` source list.

### `src/platform/client/menu_plaza.{h,cpp}` 🟢

**Keep.** The 3D plaza scene is the world background CEF composites over. No menu UI
inside it.

### `src/platform/client/handbook_panel.{h,cpp}` 🔴 (eventually)

Used only by the legacy Handbook menu screen. Once the CEF handbook page lands, this
is dead. Until then, keep as-is — clicking Handbook in CEF still falls back to the
native panel.

---

## Game class (`game_vk.h` / `.cpp`)

Fields → remove with the classes above:
- `LoadingScreen m_loading;`
- `MenuRenderer m_menuRenderer;` — only if we also delete `renderGameMenu`/`renderDeath`,
  otherwise keep as a 🟡 partial.
- `HandbookPanel m_handbookPanel;` — if Handbook moves to CEF.

State plumbing:
- `MenuScreen` enum: prune entries that no longer have a target (CharacterSelect,
  Multiplayer, Handbook, Settings) once their CEF replacements exist.
- `setCefMenuActive(bool)` becomes the default-true path; can be replaced with a
  permanent `m_cefMenuActive = true` once the legacy paths are gone.

Render-loop call sites in `game_vk.cpp`:
- `if (!m_cefMenuActive) m_menuRenderer.renderMenu();` → just delete the whole block
  once the function body is gone.

---

## Asset cleanup

Once the bitmap-font menu is gone, several text2d shader **modes** become unused
(title, button), and the SDF font atlas is only used for in-game HUD/dialog. Audit
`text2d.frag` for branches that exist solely for menu use — likely none, but worth
checking before any shader simplification.

The 8×8 source font (`src/platform/client/rhi/ui_font_8x8.h`) stays — it's the source
for the SDF atlas used by gameplay HUD/dialog/tooltip text. CEF only owns *menu* text.

---

## Order of operations

1. **Don't delete yet** while `m_cefMenuActive` defaults to `false`. Things still rely
   on the gate to suppress legacy paths.
2. Build out CEF replacements for: character select, settings, handbook, in-game pause
   menu, death screen.
3. Flip `m_cefMenuActive` default to `true` permanently.
4. Bulk-delete files & call sites listed above.
5. Verify build, run `make demo`, screenshot.
6. Update `CMakeLists.txt`.

Until step 2 is done, every "🔴" item is *reachable code* via the dismiss-on-action
fallback (clicking Multiplayer in CEF dismisses overlay, native Multiplayer screen
shows). Premature deletion = orphaned UI states.
