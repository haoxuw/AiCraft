# CEF-based UI Rebuild Plan

Status: **draft, phase 1 landing in progress**
Owner: in-tree (client/ui)
Scope: replace the custom Vulkan 8×8-bitmap UI with HTML/CSS/JS rendered
via Chromium Embedded Framework (CEF). The 3D world stays native Vulkan;
only the 2D overlay layer changes.

---

## 0. Motivation

The current UI pipeline is `rhi::IRhi::drawText2D` + `drawRect2D` on top
of an 8×8 bitmap font baked to a 512×192 SDF atlas. That's sufficient
for the HUD, but:

1. **Typography collapses at title sizes.** "SOLARIUM" at scale 3.4×
   upscales a 32px-per-glyph source to ~100px on screen — inherent
   chunkiness no shader fixes.
2. **UI code is hand-rolled per surface.** Every panel (handbook, dialog,
   notifications, menu) reimplements buttons, scroll, pills, hit-test.
   There's no layout engine, just pixel-pushing.
3. **Modder story is weak.** A modder can add artifacts (living, items,
   blocks) but can't add UI. We're not moddable "anything and everything"
   until modders can ship their own screens.
4. **Discoverability issues** like the `< >` arrows / sub-filter pills in
   the handbook keep recurring because the layout logic is bespoke every
   time. HTML gets semantic widgets (`<button>`, `<select>`, `<details>`)
   for free, and CSS gets tabs, grids, and responsive layouts for free.

CEF solves all four. The tradeoff is ~150 MB of shipped binaries and
~500 ms of Chromium bootstrap at client start. Both are acceptable for a
desktop voxel sandbox.

---

## 1. Architecture

### 1.1 Process layout

CEF is multi-process by design. The game's `solarium-ui-vk` becomes
CEF's **browser process**. CEF spawns child processes (renderer, GPU,
utility) that re-exec the same binary with different command-line flags;
`main()` hands control to `CefExecuteProcess` early and only continues
into our game loop when we're the browser process.

```
solarium-ui-vk (browser)  ← our main() after CefExecuteProcess returns
├── solarium-ui-vk --type=renderer   (Chromium renderer per BrowserHost)
├── solarium-ui-vk --type=gpu-process
└── solarium-ui-vk --type=utility
```

A dedicated sub-process executable (`solarium-cef-subprocess`) is
cleaner on Linux — avoids sandbox-init coupling with our giant main
binary. We'll start with the shared-binary pattern and split if it
causes issues.

### 1.2 Rendering — Off-Screen (OSR) mode

CEF can render to a window or to an off-screen buffer. We use OSR:

1. Create a `CefBrowser` with `windowless_rendering_enabled = true`.
2. Implement `CefRenderHandler::OnPaint(...)` — Chromium hands us a
   `void* buffer`, `int width`, `int height`, and a list of dirty rects.
3. Copy the dirty rects into our Vulkan staging buffer, then
   `vkCmdCopyBufferToImage` into a VK_FORMAT_B8G8R8A8_UNORM texture.
4. Draw that texture as a fullscreen (or regional) quad in the
   swapchain pass, **after** the 3D world and **before** any native
   overlays we keep (HUD, crosshair, floaters).

Dirty-rect uploads are critical. A full 1920×1080×4 = ~8 MB/frame is
PCIe-expensive at 60 fps; dirty rects drop typical UI repaint to tens
of KB.

### 1.3 Input plumbing

- Mouse: `SendMouseMoveEvent`, `SendMouseClickEvent`, `SendMouseWheelEvent`.
  Convert GLFW pixel coords → CEF pixel coords (same, minus DPI scale).
- Keyboard: `SendKeyEvent` with `CefKeyEvent{type=KEYEVENT_RAWKEYDOWN/KEYUP/CHAR}`.
  Feed GLFW key + char callbacks both.
- Focus: explicit handoff. When the UI owns input focus (menus, dialog
  open), GLFW-driven player controls freeze; when it doesn't, keys
  route to the game. The existing `m_uiWantsCursor` gate extends here.
- IME: deferred (not needed for English-only MVP; Chromium handles CJK
  input natively when we wire it).

### 1.4 JS ↔ C++ bridge

Two channels:

1. **C++ → JS:** `frame->ExecuteJavaScript("civ.onHp(" + hp + ")", url, 0)`.
   Fine for low-frequency pushes (HP change, inventory update, dialog
   token streamed in).
2. **JS → C++:** register a `CefV8Handler` in the renderer process, expose
   it as `window.civ.emit(event, payload)`. The handler IPCs the
   message to the browser process via `CefProcessMessage`, where our
   main-process router dispatches it to game handlers.

Message schema (initial set):
```
// Main menu
civ.emit("menu.pick", { item: "singleplayer" })
civ.emit("menu.pick", { item: "handbook" })
civ.emit("menu.pick", { item: "quit" })
// Character select
civ.emit("char.pick", { id: "base:villager" })
// Handbook
civ.emit("hb.select", { category: "living", id: "base:guy" })
civ.emit("hb.voice.play", { voice: "en_US-amy-medium" })
// Dialog
civ.emit("dlg.send", { text: "Hello, villager." })
civ.emit("dlg.ptt.start" | "dlg.ptt.end")
civ.emit("dlg.close")
// Settings
civ.emit("settings.set", { key, value })
```

All payloads JSON-serialised; stringly-typed for v0, moved to a generated
enum later if it causes typos.

### 1.5 Asset layout

```
build-perf/
├── solarium-ui-vk          (browser process binary — also used as sub)
├── libcef.so               (from cef_binary)
├── cef_100_percent.pak     (CEF resources)
├── cef_200_percent.pak
├── chrome_100_percent.pak
├── icudtl.dat
├── resources.pak
├── v8_context_snapshot.bin
├── snapshot_blob.bin
├── locales/                (~50 files, ~5 MB)
├── chrome-sandbox          (setuid helper for sandboxing)
└── ui/                     ← our HTML roots
    ├── menu/               index.html + css + js
    ├── handbook/
    ├── dialog/
    └── shared/             fonts, icons, common css
```

The `ui/` tree is loaded via a custom scheme handler (`civ://ui/...`) so
we don't need an HTTP server and don't touch the filesystem on every
navigation. Also means modders can inject their own `ui/` subtrees the
same way artifacts are loaded.

### 1.6 What stays native

- **3D world** — entire render pipeline unchanged.
- **HUD overlay** — FPS, pos, HP bar, mode badge, crosshair, notifications,
  floaters, damage numbers. These are per-frame, tightly coupled to
  entity state and camera projection, and benefit from zero-latency
  native rendering. Porting them buys nothing and costs frame time.
- **Chunk mesh / particle / ribbon / shadow passes** — unchanged.
- **Loading screen** — stays native (CEF isn't up yet during boot).

### 1.7 What moves to HTML

- **Main menu** (Main / Multiplayer / Settings / Handbook / Quit).
- **Character select** — including the 3D preview injection. The 3D
  character renders *behind* the HTML panel, which has a transparent
  cutout over the preview area.
- **Handbook** — the whole thing. See section 4 for content parity.
- **Dialog panel** — chat UI, input, history, voice tag, PTT hint,
  streaming tokens with subtle styling.
- **Pause menu** — Resume / Save / Quit to Menu.
- **Inventory** — possibly. Defer decision to phase 4.
- **Death / respawn screen** — yes, trivial.
- **Settings** — yes, forms-heavy, HTML shines.

---

## 2. Build system changes

### 2.1 CEF binary acquisition

CEF binaries are hosted at `https://cef-builds.spotifycdn.com`. Pattern:

```
cef_binary_<VER>_linux64[_minimal].tar.bz2
```

The `_minimal` flavour drops the `cefclient`/`cefsimple` samples and
`Debug/` build, shrinking from ~700 MB to ~150 MB. We want `_minimal`.

Pin a specific version (e.g. `131.4.1+gd3d6b75+chromium-131.0.6778.140`)
in CMake:

```cmake
set(CEF_VERSION "131.4.1+gd3d6b75+chromium-131.0.6778.140")
set(CEF_SHA256 "...")
FetchContent_Declare(cef
    URL https://cef-builds.spotifycdn.com/cef_binary_${CEF_VERSION}_linux64_minimal.tar.bz2
    URL_HASH SHA256=${CEF_SHA256}
)
FetchContent_MakeAvailable(cef)
```

Cache hit: FetchContent stashes the tarball in `_deps/cef-subbuild/`,
so CMake re-configures don't re-download.

### 2.2 libcef_dll_wrapper

CEF's API is exposed as a C ABI (`libcef.so`); ergonomic C++ wrappers
live in source form under `libcef_dll/` inside the binary distribution.
You build `libcef_dll_wrapper` as a static library and link it into the
game:

```cmake
add_subdirectory(${cef_SOURCE_DIR}/libcef_dll
                 ${cef_BINARY_DIR}/libcef_dll)
target_link_libraries(solarium-ui-vk PRIVATE
    ${cef_SOURCE_DIR}/Release/libcef.so
    libcef_dll_wrapper
)
target_include_directories(solarium-ui-vk PRIVATE ${cef_SOURCE_DIR})
```

### 2.3 Runtime layout

Post-build: copy `Release/*.so`, `Release/*.pak`, `Release/*.dat`,
`Release/locales/`, `Release/chrome-sandbox` into `$<TARGET_FILE_DIR>`.
Existing POST_BUILD steps already copy `artifacts/`, `resources/`,
`shaders/` — we piggyback.

### 2.4 Binary size impact

- Before: `solarium-ui-vk` is ~40 MB stripped.
- After: plus `libcef.so` ~120 MB + resources ~40 MB = ~200 MB of
  additional distribution. Acceptable for a desktop game.

---

## 3. Phased milestones

Each phase ends with a concrete proof point. No phase starts until the
previous one's smoke test passes.

### Phase 1 — CEF bring-up (2–3 days) ← **in progress**

- [ ] Add CEF FetchContent + libcef_dll_wrapper to CMakeLists.txt.
- [ ] Copy CEF runtime files into `build-perf/` post-build.
- [ ] Add `cef_app.h/cpp` — `CefInitialize` / `CefShutdown` lifecycle,
      early-return for sub-process mode.
- [ ] Create a `WebOverlay` class that:
      - owns a `CefBrowser` with OSR enabled
      - implements `CefRenderHandler::OnPaint`
      - exposes `texture()` → `VkImage`
      - forwards GLFW mouse/keyboard when focused
- [ ] Draw the overlay texture as a fullscreen quad in a new `web_overlay.vert/frag`.
- [ ] Load a stub `civ://ui/menu/index.html` that just renders
      `<h1>Hello Solarium</h1>`.
- **Proof:** screenshot of the menu scene with HTML "Hello Solarium"
  composited over it. Keyboard focus toggles between game and HTML.

### Phase 2 — JS↔C++ bridge (1–2 days)

- [ ] Custom scheme handler for `civ://ui/<path>` serving from the `ui/`
      tree.
- [ ] `window.civ.emit(event, payload)` in the renderer process; IPC
      via `CefProcessMessage` to browser; dispatch table on the browser
      side.
- [ ] C++ → JS push helper: `webOverlay.push("hp.change", json)`.
- [ ] Schema versioning: every payload carries a `"v":1` field so we
      can evolve without re-launching CEF.
- **Proof:** HTML button click triggers a native printf in
  `/tmp/solarium_game.log`; native tick pushes an HP change into JS
  that updates the DOM.

### Phase 3 — Main menu + character select (2–3 days)

- [ ] `ui/menu/index.html` + CSS: Singleplayer / Multiplayer / Handbook
      / Settings / Quit. Match the C&C-style sidebar from
      `handbook_features.md`.
- [ ] Character select screen with 3D cutout. HTML panel has a
      right-side `<div class="preview-cutout">` with zero alpha; the
      world renderer reads that rect from JS and frames the character
      inside it.
- [ ] Translate `MenuScreen::Main` / `::CharacterSelect` /
      `::Multiplayer` / `::Connecting` state machine into JS routes
      (`#/menu`, `#/chars`, `#/lan`, `#/connecting`).
- **Proof:** screenshot of new main menu; click "Handbook" lands on a
  placeholder handbook route; pick a character, game launches.

### Phase 4 — Handbook (3–5 days)

- [ ] Content-parity audit against `handbook_features.md` (see section 4).
- [ ] HTML handbook: category tabs, sub-filter dropdown, entry list,
      detail pane with 3D preview cutout, voice player.
- [ ] Voice preview plumbing: JS emits `hb.voice.play` → native calls
      `TtsVoiceMux::clientFor(name)` → pushes playback-complete event.
- [ ] Delete `handbook_panel.{h,cpp}` when the HTML version covers all
      feature-inventory rows.
- **Proof:** every feature row in `handbook_features.md` lights up on
  the new handbook or is explicitly marked deferred. Voice preview
  plays audio.

### Phase 5 — Dialog + pause + death screens (2 days)

- [ ] `ui/dialog/index.html` — reuse the LLM streaming tokens channel.
- [ ] Pause menu — HTML overlay with transparent background.
- [ ] Death/respawn — HTML.
- [ ] Settings — HTML forms.

### Phase 6 — Font + visual polish (1 day)

- [ ] Embed Roboto + Inter + a monospace (JetBrains Mono) in `ui/shared/`.
- [ ] Design tokens (CSS vars) for the brass/wood palette we already
      have — keep visual continuity with native elements (HUD, floaters).
- [ ] Motion + easing curves for transitions.

### Phase 7 — Decommission native UI kit (1 day)

- [ ] Retire `handbook_panel.{h,cpp}`, `game_vk_renderer_menu.cpp`,
      menu portions of `game_vk_renderer_panel.cpp`, `screen_shell.*`.
- [ ] Keep `rhi::drawText2D/drawRect2D` — HUD and notifications still
      use them.
- [ ] Delete `ui_font_8x8.h` once nothing links to `generateUiFontAtlas`.

**Rough total: 12–17 working days.** Generous headroom because CEF
integration is where this kind of project usually gets stuck — sandbox,
codec, GPU process interactions on Linux all have rough edges.

---

## 4. Handbook content inventory (what we need to show)

Source of truth: `handbook_features.md` at repo root. That doc still
describes an older ImGui-based handbook; we preserve the feature list
and adapt the UI idiom. Sections called out as "partially superseded"
there (`forkEntry`, on-disk `player/` tier) stay deferred — the v1 HTML
handbook is read-only, a browser not an editor.

The current `handbook_panel.cpp` already covers a subset:
- Category grouping (Creatures / Items / World / Gameplay / Modding)
- Sub-filter pills
- Entry list with name + category tag
- 3D preview injection (living / item / annotation / model)
- Voice list + "Play Sample" button
- Minimal detail card (name, id, description, subcategory, tags)

What's missing vs `handbook_features.md` and needs building:

| Feature row | Current | HTML rebuild |
|-------------|---------|---------------|
| Global search (`/` key) | ❌ | ✅ top bar, fuzzy on name+id+tags |
| "Custom" star marker on forked entries | ❌ | defer (no fork yet) |
| Per-category richer detail (stats for Living, damage for Items, block hardness, behavior nodes, world gen params) | ⚠ minimal | ✅ category-specific panels |
| Live 3D preview with rotate/zoom | ✅ pinned camera | ✅ keep injection, add mouse-drag orbit in preview cutout |
| Animation clip picker (for Living with multiple clips) | ❌ | ✅ dropdown per entry |
| Voice sample waveform / playback meter | ❌ | ✅ simple play/stop + progress |
| Markdown / rich-text in description | ❌ | ✅ rendered in HTML naturally |
| Cross-links ("Villager uses behavior X") | ❌ | ✅ clickable refs in detail pane |
| Keyboard-navigation only path | ⚠ partial | ✅ full, /, arrows, enter, esc |
| Breadcrumb nav + back stack | ❌ | ✅ HTML history API |
| Recently viewed list | ❌ | ✅ pin to sidebar |
| Artifact source viewer ("View Python") | ❌ | ✅ read-only syntax-highlit modal, highest modder payoff |

Category-specific detail (from `handbook_features.md`):
- **Living**: max_hp, move_speed, feet_size, sight_range, playable flag,
  behavior tree link, model link, idle/walk/attack clip names, voice
  link, dialog_system_prompt preview.
- **Item**: damage, durability, stack_max, equip_slot, drop_model,
  pickup_sound.
- **Block**: hardness, tool_required, drops (item ref), transparent,
  emits_light, sound group.
- **Behavior**: purpose, DSL source, used-by list (which Living link to
  it).
- **Effect**: duration, hp_per_tick, visual (model + color).
- **Resource**: value conversion table, spawn_frequency, vein_size.
- **World**: template params — size, biome mix, structure density.
- **Structure**: blueprint preview (top-down), feature overlays.
- **Annotation**: billboard sprite, trigger radius, action.
- **Model**: mesh source (.bbmodel / box list), bone tree, clips.
- **Voice (synthetic)**: piper .onnx path, sample text, users list,
  "Play Sample" → `TtsVoiceMux`.

---

## 5. Risk register

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| CEF multi-process + our Python embed conflict | Med | Python only lives in renderer-side AgentClient; CEF only touches browser process. Keep them out of each other's fork. If it breaks, split CEF sub-process into its own binary. |
| Chromium sandbox + Vulkan validation layer fights | Low-Med | Disable sandbox in Debug builds (`--no-sandbox`), keep it on in release. |
| Texture upload cost in perf-sensitive scenarios | Low | Dirty-rect only. Profile under `make perf_fps`. Budget: ≤1 ms of present time for UI overlay. |
| Keyboard focus ambiguity (game vs browser) | Med | Hard rule: if any CEF panel is open, it owns keyboard. When closed, game owns. One flag, one source of truth. |
| Shipped binary size doubles | Cert | Accepted. Post release, evaluate Ultralight. |
| Chromium version churn (security patches) | Med | Update CEF quarterly. Pin SHA256 in CMake so rebuilds are reproducible. |
| Sandbox helper (`chrome-sandbox`) perms | Med | POST_BUILD `chmod 4755`. Document for distributors. |
| CEF build time (libcef_dll_wrapper ~2 min at -j1) | Low | Cached after first build. |

---

## 6. Non-goals (explicitly out of scope for this plan)

- Porting HUD / floaters / crosshair / damage numbers to HTML.
- Porting the 3D world to HTML (e.g. WebGL inside CEF). The world stays
  native Vulkan.
- In-game code editor for artifacts (deferred; handbook is read-only).
- Modder-owned UI trees. The plan supports this (scheme handler is
  directory-agnostic) but we don't build modder UX in this pass.
- Web-deployable build (the existing `handbook_features.md` mentions
  Emscripten targets — we're native-only for now).
- Full-fidelity JS<->C++ typed RPC codegen. Stringly-typed v0 is fine.
- IME / accessibility / localisation. All standard browser features
  that come for free when needed; not MVP.

---

## 7. Rollback plan

The native UI kit (`handbook_panel`, menu renderer, etc.) stays in the
tree throughout phases 1–6. Only phase 7 deletes it. If any phase's
proof point fails unrecoverably, revert that phase and keep shipping
the native UI. The two UIs are composable during migration — we ship
with CEF only lit up on surfaces that proved out.
