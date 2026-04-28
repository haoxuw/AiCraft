# Web UI (CEF) — Progress & Next Steps

The CEF-hosted menu/handbook/editor stack. This doc is the living handoff for
the visual + architecture pass that started in the seat-aware handbook → in-game
editor work.

---

## What's landed

### Architecture

- **`src/platform/client/ui/components.h`** — single header, `namespace
  solarium::ui`, exports:
  - `themeRoot(const std::string& id)` — emits `:root{...}` CSS variables for
    `brass` / `cobalt` / `lichen` themes. Each theme adds `--ink-soft`,
    `--bg`, `--bg-2` alongside the original `--accent-hi/--accent-mid/--ink`.
  - `baseCss()` — typography, body backdrop, h1 flourishes, `.tag/.note/.filters`
    chip styling, button bevel, panel primitive, **artistic scrollbar**, paper
    grain, `<hr class='div'/>` ornament.
  - `enc(...)` — HTML/URL escape helper.
- **`main.cpp` no longer holds inline kCss / themeRoot.** It calls
  `solarium::ui::baseCss()` and `solarium::ui::themeRoot(game.settings().theme_id)`.
  Adding a theme or tweaking a button is now a one-file edit.

### In-game source editor (Monaco)

- **Handbook detail card has an `EDIT SOURCE` button** that fires
  `edit:<cat>:<id>`.
- `editorUrlFor(cat, id)` lambda in `main.cpp` writes `/tmp/solarium_editor.html`
  with Monaco bootstrap pointing at
  `third_party/monaco-editor/out/monaco-editor/min/vs/`. CEF loads via `file://`.
- CEF gets `--allow-file-access-from-files` + `--disable-web-security` so
  Monaco's AMD loader can fetch sibling modules.
- Save handler (`save_artifact:<cat>:<id>:<base64>`) writes to
  `~/.solarium/forks/<dirName>/<id>.py` and calls `loadAll` + `loadForks` for
  hot-reload.
- **`SOLARIUM_BOOT_PAGE=editor` + `SOLARIUM_BOOT_EDIT=cat:id`** boot hook for
  screenshot iteration.

### Per-user fork registry

- **`ArtifactRegistry::loadForks(forksRoot)`** overlays `<root>/<dirName>/<id>.py`
  on top of base entries, replacing any existing `(category, id)`.
- **`ArtifactRegistry::defaultForksRoot()`** returns `$HOME/.solarium/forks`.
- Wired into client boot (`main.cpp:322`, `game_vk.cpp:133`), server boot
  (`server/main.cpp:235`), and the editor save handler.
- TODO(cloud) annotated in the registry — scheduled follow-up agent on May 11
  (job `d3aef483`) to revisit once auth/profile lands.

### CEF input

- **Mouse drag works** in scrollbars. `sendMouseMove` and `sendMouseClick` now
  take a `modifiers` int carrying `cef_event_flags_t` (held buttons + shift /
  ctrl / alt). `Shell::mouseButtonsHeld` tracks LMB=0x1 / MMB=0x2 / RMB=0x4 and
  is forwarded on every move.
- **Mouse wheel works** in the overlay. `scrollCb` routes to
  `cef->sendMouseWheel(...)` when `cefMenuActive()` is true; falls through to
  game camera zoom otherwise. 100 px/notch matches Chromium's default.

### Visual polish

- **Body backdrop strengthened** — radial scrim from `0.55→0` to
  `0.78→0.55→0.30`, ellipse 90%×80%. Plaza still readable but stops fighting
  chrome.
- **All panel/row/tile alpha bumped** from 0.6/0.75/0.78/0.85 → 0.97 with
  colour shifted to `rgba(20,13,8)`. Tree silhouettes no longer pierce content.
- **`.tag` / `.note` / `.filters` are scrim chips** — dark background, brass
  border, text shadow. Used by every page subtitle.
- **DELETE button retoned** — was screaming red, now brass border / brass text;
  rust-red only on hover.
- **Bottom handbook panel flushes from sidebar to screen edge** with a single
  brass top-border ("horizon line"). Reads as the floor of the menu rather than
  a floating tile.
- **Detail card height = 48vh** — leaves the upper ~52vh for the live 3D
  preview model.
- **Artistic scrollbar** — recessed dark groove track with brass hairlines, cast-
  brass thumb with vertical sheen gradient + three horizontal grip notches,
  brass-cap arrow buttons at top/bottom, hover/active variants.

### Smaller UX wins

- **`SOLARIUM_BOOT_PAGE=editor`** + boot hook variants for every other CEF page
  (`handbook`, `chars`, `settings`, `worlds`, `pause`, `mp`, `saves`, `mods`,
  `lobby`, `death`).
- **`/tmp/solarium_vk_dismiss_loading`** debug trigger to bypass the loading
  screen for headless smoke tests.

---

## Known issues

| # | Issue | Severity |
|---|-------|----------|
| 1 | Tree silhouettes still pierce the **gaps between** rows/cards (rows themselves are opaque). | low |
| 2 | `SOLARIUM_BOOT_PAGE=pause` renders the handbook, not the pause menu. | bug |
| 3 | Settings page has both a "AUDIO \| NETWORK \| CONTROLS \| THEME" `.tag` chip AND a tabs row — redundant. | low |
| 4 | `clip-path:polygon(...)` for octagonal cut-corners broke buttons (turned them into ribbons that spilled outside the bounding box). Reverted. Need a different "less straight lines" approach. | reverted |
| 5 | `.tag` chip sometimes sits cramped against the title — needs more vertical breathing room. | low |
| 6 | Native (Vulkan) UI surfaces (HUD, inventory tooltip, entity inspect) are still hand-drawn and don't share the brass theme tokens. | medium |
| 7 | The Inspect tab still has no `Edit` button wiring (Monaco editor only reachable from Handbook today). | medium |

---

## Next steps

### A. Visual — "less straight lines, more retro / artistic"

Goals: chamfered or ornamented corners on panels and buttons; decorative
flourishes around subtitles; cast-metal feel without sacrificing readability.

1. **Decorative corner ornaments via `::before/::after` background-image.** Use
   small inline SVGs (data: URLs) of brass scrollwork at each panel corner.
   Doesn't break layout (no clip-path), keeps rectangular hit-boxes.
2. **Radial-gradient masks for soft corners.** Apply `mask-image` with four
   radial gradients to round corners by a few pixels without `border-radius`
   (which reads too modern). Test that Chromium's `mask` doesn't fight `border`.
3. **Diamond-flanked subtitle ornament.** `<hr class='div'/>` already exists in
   `ui::baseCss()` (radial brass bead + fading hairlines on either side) — wire
   it above each page's `.tag` chip.
4. **Page-level paper-grain backdrop.** Bump the existing
   `repeating-linear-gradient` alpha slightly + add a third diagonal at 30°
   for more visible weave.
5. **Headline ornament.** Replace plain h1 hairlines with a centred bead +
   curved gradient (same recipe as `<hr class='div'/>`).
6. **Engraved button labels.** Add a subtle `text-shadow:0 -1px 0
   rgba(0,0,0,0.6),0 1px 0 rgba(243,196,76,0.15)` so labels look stamped.

### B. Architecture — finish the OOP refactor

The current `ui::components.h` covers `themeRoot`, `baseCss`, and `enc`. The
remaining pages still inline their own per-page CSS strings.

1. **Extract `dexCss()`** (sidebar + detail-card chrome shared by handbook +
   char-select) into `ui::dexCss()`.
2. **Per-page lambdas → free functions.** Move `mainPage()`, `settingsPage()`,
   `saveSlotsPage()`, `multiplayerPage()`, `modManagerPage()`, `worldPickerPage()`,
   `pausePage()`, `lobbyPage()`, `deathPage()`, `editorUrlFor()` out of the giant
   `main()` into `src/platform/client/ui/pages.cpp`. Each takes a `Game&` (or a
   thinner `PageCtx`) and returns `std::string`.
3. **Action handlers → `ui::PageActionRouter`.** The 700-line `setActionCallback`
   lambda in `main.cpp` has one giant `if/else if` chain over action strings.
   Hoist into a registrable router so each page can declare its own action
   handlers locally rather than threading them through one closure.
4. **Migrate to file:// + real CSS files.** Once the data: URL hell is gone,
   pages can `<link rel='stylesheet' href='file://.../components.css'>`. Real
   DevTools, no `%23` escaping, no `%%` percent-doubling. Editor flow already
   proves this works.
5. **Native parity.** `src/platform/client/ui/native.h` exposes
   `drawPanel/drawButton/drawBadge/drawStatGrid` over the existing `ui_kit`
   primitives. HUD, inventory tooltip, entity inspect adopt them so the in-
   world UI matches menu UI.

### C. Bugs

- **Fix `SOLARIUM_BOOT_PAGE=pause`** — currently falls through to handbook.
- **Drop the redundant Settings subtitle chip** (the tabs row already conveys
  navigation).

### D. Inspect tab

- **Wire `EDIT SOURCE` button** into the entity-inspect overlay so the same
  Monaco editor is reachable mid-gameplay (not just from the Handbook).
- Reuse the same `editorUrlFor(cat, id)` helper.

### E. Polish

- **Page transition shimmer** — fade the body backdrop from 0 → 1 over 120ms
  on page navigation so loadUrl swaps don't snap.
- **Theme-switch live preview** — settings page already lists three themes; add
  a tiny preview swatch for each.
- **Keyboard navigation** — all sidebars and tile grids should respond to
  arrow keys + enter for accessibility. Today they're click-only.

---

## File map

```
src/platform/client/
  main.cpp                       # menu page lambdas, action callback, boot hooks
  game_vk.cpp                    # in-game CEF state (open/dismiss handbook/pause)
  cef_app.cpp                    # CEF browser flags (file-access, no-zygote)
  cef_browser_host.{h,cpp}       # OSR pixel snapshot + mouse/wheel forwarding
  ui/
    components.h                 # ← THIS REFACTOR (themeRoot/baseCss/enc)
src/platform/logic/
  artifact_registry.h            # loadForks() + defaultForksRoot()
~/.solarium/forks/<cat>/<id>.py  # per-user fork overlay (TODO: cloud sync)
third_party/monaco-editor/       # git submodule, npm-built into out/
```
