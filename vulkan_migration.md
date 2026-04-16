# CivCraft: Vulkan Migration + Binary Rename + Model Format Overhaul

Planning document. No code changes here — this file is the source of truth for
the three coupled migrations, in the order we intend to land them:

1. **Binary rename** — `civcraft` → `civcraft-ui`; consolidate `civcraft-client`.
2. **Model format** — Python dicts → Blockbench-editable `.geo.json` + `.meta.json`.
3. **Vulkan migration** — OpenGL → Vulkan on native; Emscripten stays on GLES3.

Each migration is independently landable. Architecture invariants from
`CLAUDE.md` (Rules 0–5, three-process TCP split, Python-as-game) are preserved
throughout — **only `src/platform/client/*`, `src/CivCraft/client/*`, shaders,
model artifacts, and CMake are touched.**

---

## Part 1 — Binary Rename

### Current state

| Binary | Role | Links |
|---|---|---|
| `civcraft` | GUI, singleplayer (spawns local server child) | GL + Python + zstd |
| `civcraft-server` | Headless dedicated server | Python + zstd |
| `civcraft-agent` | Headless Python AI runner (one per NPC) | Python + zstd |
| `civcraft-client` | GUI, network-only (no local server) | GL + Python + zstd |
| `civcraft-test` | Headless e2e | Python + zstd |
| `civcraft-test-pathfinding` | Pathfinding regression | Python + zstd |
| `model-editor` | Standalone model viewer / snapshot tool | GL |

### Target state

| Binary | Role |
|---|---|
| `civcraft-ui` | GUI. `--host/--port` joins remote; otherwise spawns a local `civcraft-server` child (singleplayer = local TCP). Replaces `civcraft` **and** `civcraft-client`. |
| `civcraft-server` | unchanged |
| `civcraft-agent` | unchanged |
| `civcraft-test` | unchanged |
| `civcraft-test-pathfinding` | unchanged |
| `model-editor` | unchanged |

**Rationale for consolidating `civcraft-client` into `civcraft-ui`:** both link
`FULL_CLIENT_SOURCES` and diverge only in whether `ProcessManager` spawns a
local server. One flag (`--host`) decides. Two binaries for one concern is
drift waiting to happen.

### Files to modify

**Build system**
- `CMakeLists.txt`
  - Rename `add_executable(civcraft …)` → `add_executable(civcraft-ui …)`.
  - Delete the separate `add_executable(civcraft-client …)` block; its sources
    are already in `civcraft-ui`.
  - Every `$<TARGET_FILE_DIR:civcraft>` → `$<TARGET_FILE_DIR:civcraft-ui>`.
  - `target_link_libraries(civcraft …)` → `civcraft-ui`.
  - POST_BUILD asset staging: retarget the `civcraft` custom command to
    `civcraft-ui`.
- `Makefile` (root)
  - `game`, `client`, `item_views`, `character_views`, `ui_snapshots` targets:
    replace `./build/civcraft` with `./build/civcraft-ui`.
  - `stop`: replace `pgrep -x civcraft` with `pgrep -x civcraft-ui` (and keep
    the `civcraft-server`, `civcraft-agent` entries).
  - Any `cmake --build build --target civcraft` → `--target civcraft-ui`.
- `src/CivCraft/Makefile`
  - Same forward-to-root edits; keep `GAME=` semantics intact.

**Runtime**
- `src/platform/client/process_manager.h`
  - If the AgentManager re-execs the current binary for any reason, update the
    lookup name. The server-spawn path uses `"civcraft-server"` and stays.
- `src/CivCraft/main.cpp`, `src/CivCraft/main_client.cpp`
  - If either references its own argv[0] in logs or window title, update.
  - Delete `main_client.cpp` **only if** we unify into `main.cpp` with
    `--host`-conditional behavior. Otherwise keep the two entry files as-is
    and just have both link into `civcraft-ui`.
- `src/platform/shared/crash_log.h`
  - Update any embedded binary-name strings.

**Docs / scripts**
- `CLAUDE.md` — rewrite the "Three process types" table, every `./build/civcraft`
  in the Build & Run and Iterative Development sections, `pgrep -x civcraft`,
  and the exact-name-matching caveat (still valid — just update the example).
- `src/CivCraft/docs/*.md` — grep `civcraft ` (space) and `./build/civcraft`,
  update.
- `src/CivCraft/tools/*.sh`, `src/CivCraft/tools/*.py` — same.
- `.claude/skills/refine-model-and-animation/SKILL.md` — same.

### Explicitly NOT renamed

- `/tmp/civcraft_auto_screenshot.ppm`
- `/tmp/civcraft_screenshot_request`
- `/tmp/civcraft_screenshot_N.ppm`
- `/tmp/civcraft_game.log` / `.prev`
- `imgui.ini`
- `saves/`, `screenshots/`, `music/`

These are game-scoped, not binary-scoped. Renaming them churns every QA script,
memory entry, and skill file without clarity gain.

### Risks

- Muscle memory — every existing shell snippet in notes, skills, and memory
  mentions `civcraft`. The rename PR must also sweep `.claude/` skills and
  ~/.claude memory if those contain hardcoded `./build/civcraft` paths.
- `pgrep -x civcraft` will match both `civcraft` (old) and `civcraft-ui` (new)
  during transition if a stale binary lingers in `build/`. `make stop` should
  kill both names until the cutover lands and old binaries are cleaned up.

### Landing order

Single commit, mechanical. Land before Part 2/3 — it reduces churn for the
larger changes that follow.

---

## Part 2 — Model Format: Python → Blockbench

### Current state

- 54 models under `src/CivCraft/artifacts/models/base/*.py`.
- Each file defines a single `model = { … }` dict with keys:
  `id, height, scale, walk_speed, idle_bob, walk_bob, hand_r/l, pivot_r/l,
  head_pivot, equip, parts`.
- Each entry in `parts` is a box: `{offset, size, color, name?, head?, pivot?,
  swing_axis?, amplitude?, phase?, speed?}`.
- Loader: `src/platform/client/model_loader.h` — a tokenizer-only Python
  parser (no interpreter). Known limitation (memory: model-loader gotchas):
  no variable references, no expressions beyond `math.pi`.
- In-game editor: `model_editor_ui.h` + `model_writer.h` writes the dict back
  as Python.

### Target state

Per model:
- **`<name>.geo.json`** — Blockbench "Bedrock Entity" export. Human-editable
  in Blockbench: visual box editing, named bones with pivots, head bone
  convention, optional UV layout if we later go textured.
- **`<name>.meta.json`** — engine-only fields that Bedrock geo can't carry:
  equip transform, procedural swing animation, per-cube flat colors,
  walk/idle bob, walk_speed.

Both files sit side-by-side in
`src/CivCraft/artifacts/models/base/<name>.geo.json` /
`<name>.meta.json`. Blockbench users edit `.geo.json` visually; tuning
constants live in `.meta.json` as readable JSON.

### Format choice rationale

- **Bedrock geometry** (not `.bbmodel`) — stable, documented, version-pinned
  schema; Blockbench exports it natively via "File → Export → Bedrock
  Geometry"; re-imports cleanly.
- **Not glTF** — overkill for box-only models, loses our pivot-bone
  semantics, and `.gltf` JSON is far less hand-editable than Bedrock geo.
- **Sidecar `.meta.json`** (not embedded in geo) — keeps the geo file
  round-trippable through Blockbench without custom fields being silently
  dropped.

### Field mapping

| Python | New location | Notes |
|---|---|---|
| `id` | `.geo.json` → `minecraft:geometry.description.identifier` | e.g. `"geometry.knight"` |
| `parts[].offset`, `size` | `.geo.json` → `bones[].cubes[].origin` + `size` | Blockbench origin is corner, not center — converter does `origin = center - size/2`. |
| `parts[].color` | `.meta.json` → `cube_colors[i]` (indexed by cube order in geo) | Bedrock expects UV'd textures; indexed flat colors keep us on the existing renderer path. |
| `parts[].name` | `.geo.json` → `bones[].name` | One bone per named part; unnamed parts grouped under a `root` bone. |
| `parts[].head: true` | Bone named `head` with `pivot` set | Matches Minecraft convention; loader auto-detects by name. |
| `parts[].pivot`, `swing_axis`, `amplitude`, `phase`, `speed` | `.meta.json` → `procedural_anim.<bone_name>` | Custom; no Blockbench equivalent. |
| `head_pivot` | Bone `head` → `pivot` in geo | |
| `hand_r/l`, `pivot_r/l` | Bones `hand_r`, `hand_l` with `pivot` set | |
| `equip: {rotation, offset, scale}` | `.meta.json` → `equip` | |
| `height`, `scale`, `walk_speed`, `walk_bob`, `idle_bob` | `.meta.json` top-level | |

### `.meta.json` schema (illustrative)

```json
{
  "height": 2.0,
  "scale": 1.0,
  "walk_speed": 2.0,
  "walk_bob": 0.05,
  "idle_bob": 0.012,
  "equip": { "rotation": [0,90,0], "offset": [0.08,0,-0.05], "scale": 0.7 },
  "cube_colors": [
    [0.85, 0.70, 0.55, 1.0],
    [0.10, 0.10, 0.12, 1.0]
  ],
  "procedural_anim": {
    "head": { "axis": [1,0,0], "amplitude_deg": 4, "phase": 0, "speed": 2 }
  }
}
```

`cube_colors` is indexed by cube order in the geo file's depth-first bone
traversal. The converter emits both files with identical ordering; the loader
asserts the indices match on load.

### Files

**Create**
- `src/platform/client/model_loader_geo.h` — loads `.geo.json` + `.meta.json`
  into the existing `BoxModel` struct (`client/box_model.h`). Same renderer
  downstream; no renderer changes.
- `src/platform/tools/model_convert/main.cpp` — one-shot migration tool.
  Reads `artifacts/models/base/*.py`, writes `<name>.geo.json` +
  `<name>.meta.json`. Uses the existing tokenizer in `model_loader.h` for
  the read side.
- `src/CivCraft/docs/MODELS.md` — Blockbench workflow: which export setting,
  where to drop files, the `.meta.json` schema, head-bone convention,
  procedural_anim fields.

**Modify**
- `src/platform/client/model_loader.h`
  - Add dispatch: if `<name>.geo.json` exists, call the new loader; else fall
    back to the `.py` loader during transition.
  - After Part 2 cleanup, delete the `.py` loader entirely.
- `src/platform/client/model_writer.h`
  - Replace the Python-dict emitter with a `.geo.json` + `.meta.json` emitter.
  - The in-game model editor (`model_editor_ui.h`, `behavior_editor.h`)
    writes Blockbench-compatible files, so in-game edits and Blockbench
    edits round-trip.
- `src/CivCraft/artifacts/models/base/*.py` → converted to
  `*.geo.json` + `*.meta.json`, then deleted.

**Delete (cleanup pass)**
- Python tokenizer code in `model_loader.h`.
- All `artifacts/models/**/*.py`.
- Any tool scripts that consume the `.py` format.

### Migration procedure

1. Land the new loader + converter behind a feature flag (loader tries geo
   first, falls back to py). No visual change.
2. Run the converter in-repo: `./build/model-convert`. Commit the generated
   `.geo.json` + `.meta.json` alongside the existing `.py` files.
3. Visual diff: `make character_views CHARACTER=base:<name>` against each
   character pre/post conversion. The debug-capture tool (F2 / scenario
   auto-screenshot) produces 6-angle PPMs per model; diff bytewise.
4. Once every model is visually identical, delete the `.py` files and the
   Python tokenizer code in `model_loader.h`.

### Known corner cases

- Five or so models use `math.pi` in `phase` fields. Converter handles
  `math.pi`, `math.pi/2`, etc. explicitly (same subset the current tokenizer
  already supports — no expansion needed).
- Blockbench stores cube positions as corner+size, not center+size. Converter
  normalizes via `origin = center - size/2`.
- Blockbench rotation order is XYZ Euler degrees; our `equip.rotation` is
  also XYZ Euler degrees — 1:1 map, but we note it in `MODELS.md` to avoid
  future confusion.
- Per-cube flat colors are not a first-class Bedrock concept. We keep them
  out-of-band in `.meta.json`; Blockbench users see white cubes in the
  editor (or a placeholder palette texture if we care to bake one). A
  "preview" pass in the converter could emit a 1×N palette PNG so Blockbench
  shows color — optional follow-up.

### Risks

- Ordering drift: if a user reorders cubes in Blockbench, the
  `cube_colors[i]` mapping breaks silently. Mitigation: loader prints a
  warning if `cube_colors.length != total_cubes`, and the in-game model
  editor re-emits both files together so edits stay consistent.
- Blockbench version churn — Bedrock geo format has a `format_version`
  field; pin to `1.12.0` and document the pin.

---

## Part 3 — Vulkan Migration

### Scope

- Native only. `__EMSCRIPTEN__` branch remains GLES3 (no Vulkan in the
  browser; `WebGPU` is a separate future project, out of scope).
- Only `src/platform/client/*` and `src/CivCraft/client/*` + shaders + CMake
  touched. Server, agent, shared, Python, protocol, all unchanged.

### Strategy: ship a parallel binary, don't replace the old one

**The Vulkan client is built as a separate binary — `civcraft-ui-vk` —
alongside the existing OpenGL `civcraft-ui`. We do NOT delete the GL
renderer until feature parity is proven side-by-side.**

Why:
- We can run both binaries against the same `civcraft-server` and visually
  diff F2 screenshots, menus, model icons, shadow bounds, fog falloff,
  particles, UI text — every feature, at every phase, without losing the
  baseline.
- Regressions are immediately reversible: the user keeps playing on
  `civcraft-ui` while we iterate on `civcraft-ui-vk`.
- The RHI surface (`rhi_gl.cpp` + `rhi_vk.cpp`) is not a transitional
  shim — **both backends are shipped**, selected at link time via the
  CMake target. The GL backend stays as a living reference right up until
  the moment we're ready to retire it (see "Cutover" below).

Build layout:

| Target | Backend | Purpose |
|---|---|---|
| `civcraft-ui` | OpenGL (existing) | production; unchanged behavior |
| `civcraft-ui-vk` | Vulkan (new) | parallel; grows to feature parity |
| `civcraft-ui-web` (Emscripten) | GLES3 | unchanged |

Both native targets share the same source tree; the only differences are:
- Linked `rhi_*.cpp`.
- A single compile-def (`AICRAFT_RHI_VK` vs `AICRAFT_RHI_GL`) gating the
  backend-specific headers inside `gfx.h`.
- Asset staging: Vulkan binary stages `.spv` shaders; GL binary stages
  `.vert`/`.frag`. Both live in `build/shaders/` side-by-side; they don't
  collide.

Makefile additions:
- `make game-vk` → builds and runs `civcraft-ui-vk`.
- `make compare` → launches both binaries against a shared server, captures
  F2 screenshots from each into `/tmp/civcraft_vk_*.ppm` and
  `/tmp/civcraft_gl_*.ppm` for pixel-diff.
- `make game` still maps to the GL binary. Default behavior unchanged.

### Cutover (happens at the END, not during)

Only after `civcraft-ui-vk` passes every feature-parity gate (see
"Verification gates" below) do we:
1. Rename `civcraft-ui` → `civcraft-ui-gl` (archived).
2. Rename `civcraft-ui-vk` → `civcraft-ui` (new primary).
3. Leave `civcraft-ui-gl` buildable for one release cycle as a fallback.
4. Only after a cycle with no reported regressions do we execute Phase 5
   (legacy deletion). **Phase 5 is explicitly NOT part of the migration
   proper — it's a separate, later commit.**

### Current GL surface area

~475 GL call sites across 24 files. Biggest consumers:

| File | GL calls | Role |
|---|---:|---|
| `src/CivCraft/client/renderer.cpp` | 207 | terrain, sky, fog, shadow pass, offscreen FBOs |
| `src/platform/client/model.cpp` | 46 | box-model VAO/VBO |
| `src/platform/client/text.cpp` | 36 | glyph atlas + quads |
| `src/CivCraft/client/chunk_mesher.cpp` | 29 | per-chunk VBO uploads |
| `src/platform/client/model_icon_cache.h` | 29 | FBO-to-texture snapshots |
| `src/platform/client/particles.cpp` | 25 | instanced quads |
| `src/platform/client/model_preview.h` | 25 | offscreen model preview |
| `src/platform/client/shader.cpp` | 24 | GLSL compile + uniform setters |
| `src/CivCraft/client/fog_of_war.h` | 18 | texture upload |
| `src/platform/client/text_input.h`, `face.h`, `input_source.h`, `controls.h`, `camera.h`, `code_editor.h`, `window.*`, `ui.*`, `game*.cpp`, `gameplay*.cpp`, `gl.h` | low | misc state / GLFW keys |

Shaders (all GLSL, all need SPIR-V):
- `src/platform/shaders/` — crosshair, highlight, particle, shadow, text (10 files)
- `src/CivCraft/shaders/` — terrain, sky, fog (6 files)

### Target architecture

Introduce a thin **RHI** (render hardware interface) under
`src/platform/client/rhi/`:

```
rhi.h              // public interface: Device, Swapchain, CommandBuffer,
                   //   Buffer, Texture, Pipeline, DescriptorSet, RenderPass,
                   //   Readback
rhi_vk.cpp         // native (desktop) backend
rhi_gl.cpp         // transitional GL backend (native + web) — kept during
                   //   the migration; deleted at Phase 5 for native; web keeps it
rhi_gles3.cpp      // web backend (Emscripten); same surface, GLES3 under the hood
```

Rendering code calls RHI. Backend is chosen at compile time:
- Native + `-DAICRAFT_RHI=vulkan` (default after cutover) → `rhi_vk.cpp`
- Native + `-DAICRAFT_RHI=opengl` (transitional) → `rhi_gl.cpp`
- Emscripten → `rhi_gles3.cpp` (always)

`src/platform/client/gl.h` → renamed **`src/platform/client/gfx.h`**, becomes
the RHI header include switch.

### New dependencies

- `Vulkan::Vulkan` via `find_package(Vulkan REQUIRED)`.
- **VMA** (Vulkan Memory Allocator) via FetchContent — pool/slab allocator,
  spares us from writing our own.
- **shaderc** (runtime GLSL → SPIR-V) OR offline `glslangValidator` in
  CMake POST_BUILD. Preference: **offline** — smaller binaries, faster
  startup, no runtime dep; fall back to the existing runtime compile path
  only if the user is hot-reloading shaders during dev.
- `imgui_impl_vulkan` added to `imgui_lib` on native.

### Phase 0 — Scaffolding (no visual change to civcraft-ui)

Goal: land the RHI abstraction and add a new `civcraft-ui-vk` target that
**initially uses the GL backend** (i.e. behaves identically to
`civcraft-ui`). This proves the seams compile and proves parallel-binary
builds + packaging work, before we write any Vulkan code.

- `CMakeLists.txt`
  - Add `find_package(Vulkan REQUIRED)` (guarded `NOT EMSCRIPTEN`).
  - `FetchContent` VMA.
  - Declare a new `civcraft-ui-vk` executable that links the same
    `FULL_SIM_SOURCES + FULL_CLIENT_SOURCES` as `civcraft-ui`, plus
    `rhi_vk.cpp` (stub initially), with compile-def `AICRAFT_RHI_VK`.
  - Keep `civcraft-ui` unchanged (links `rhi_gl.cpp`, compile-def
    `AICRAFT_RHI_GL`).
  - Add `imgui_impl_vulkan.cpp` to `imgui_lib` native sources.
- **Create** `src/platform/client/rhi/rhi.h` — public types, opaque handles,
  no backend leakage.
- **Create** `src/platform/client/rhi/rhi_gl.cpp` — implements the RHI by
  calling the existing GL code paths already in `shader.cpp`,
  `model.cpp`, etc. This is the baseline both binaries use until Phase 1
  Vulkan work is in.
- **Create** `src/platform/client/rhi/rhi_vk.cpp` — stub returning the RHI
  surface; initially delegates to GL so both binaries are functionally
  identical. Swapped to real Vulkan incrementally starting Phase 1.
- **Rename** `src/platform/client/gl.h` → `src/platform/client/gfx.h`.
  Update every `#include "client/gl.h"` across the tree.

Verification: `make item_views`, `make character_views`, F2 screenshots
from **both** `civcraft-ui` and `civcraft-ui-vk` pixel-identical to
pre-phase baselines.

### Phase 1 — Window + swapchain + ImGui on Vulkan

Goal: title screen renders via Vulkan.

- `src/platform/client/window.{h,cpp}`
  - Branch on `AICRAFT_RHI`. GL path unchanged.
  - Vulkan path: `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`, create
    `VkInstance`, pick physical device (discrete > integrated), create
    `VkDevice` (graphics + present queue), `glfwCreateWindowSurface`, create
    `VkSwapchainKHR`, MAX_FRAMES_IN_FLIGHT = 2, per-frame `VkSemaphore`
    (image-available, render-finished) and `VkFence`.
- `src/platform/client/ui.{h,cpp}`
  - Replace `imgui_impl_opengl3` with `imgui_impl_vulkan` on native;
    dedicated descriptor pool; wire to the swapchain render pass.

Verification: menu renders, clicking through menus works. No gameplay yet —
all in-world draw calls short-circuit to a clear-to-cornflower-blue so we
can confirm the swapchain + presentation loop is solid before porting each
drawer.

### Phase 2 — Port `platform/client` drawers through RHI

Order by complexity (simplest first — they validate the RHI surface):

1. `shader.cpp` → pipeline + SPIR-V loader. Offline shader compile step in
   CMake POST_BUILD: `glslangValidator -V input.vert -o input.vert.spv`,
   stage `.spv` into `build/shaders/` (next to existing `.vert`/`.frag` so
   web build still finds GLSL).
2. `text.cpp` — simplest textured quad path; good RHI shakedown.
3. `particles.cpp` — instanced quads; exercises instance buffers.
4. `model.cpp`, `entity_drawer.cpp` — box-model rendering.
5. `lightbulb_drawer.cpp`, `floating_text.cpp` — billboards.
6. `model_preview.h`, `model_icon_cache.h` — offscreen render pass + image
   readback. Validates the readback path that Phase 4 needs.

Platform shaders audit:
- Every `.vert`/`.frag` under `src/platform/shaders/` needs explicit
  `layout(location=...)` for attributes and `layout(binding=...)` for
  samplers/UBOs. `gl_FragColor` → `out vec4`. Emit SPIR-V and keep the
  GLSL source as the single source of truth — web build reads GLSL, native
  reads SPIR-V.

### Phase 2.5 — Standalone Vulkan playable slice ← DONE

Before tackling Phase 3 (the big `renderer.cpp` port), we grew
`civcraft-ui-vk` from a demo into a **self-contained playable game** on top
of the existing RHI surface. Serves two purposes:

1. **Proves the RHI is game-complete** without waiting on the massive
   `renderer.cpp` lift. If a menu→play→combat→death→respawn loop works over
   the current primitives (drawSky, drawVoxels, drawBoxModel, drawParticles,
   drawRibbon, renderShadows, renderBoxShadows, drawUi2D + SDF text), the
   abstraction is validated for cutover-gate work.
2. **Gives a shippable Vulkan binary today.** The full CivCraft game stays
   on the GL path until Phase 3/4 land; this slice is the thing we demo.

Surface:
- `src/platform/tools/civcraft_ui_vk/game_vk.{h,cpp}` — GameState machine
  (Menu / Playing / Dead), procedural village world + heightmap, NPC AI
  (Wander / Chase / Flee / Dying), third-person player + physics, melee
  combat with floating damage numbers + coin drops, per-NPC lightbulb +
  goal label + HP bar, HUD (player HP / coin counter / hotbar / FPS),
  ImGui main menu + death overlay with respawn button.
- `src/platform/tools/civcraft_ui_vk/main.cpp` — thin shell: GLFW window +
  RHI init, then loops `game.runOneFrame(dt, wallTime)`.
- Flags: `--skip-menu` jumps straight into gameplay; `--no-validation`
  disables VK validation layers; F2 screenshots to `/tmp/civcraft_vk_screenshot_N.ppm`;
  `/tmp/civcraft_respawn_request` file-triggers a respawn for CI.
- Fixed validation hazard: `ensureUi2DVertexCapacity` used to destroy the
  old vertex buffer immediately on grow, but earlier `drawUi2D` calls in the
  same frame had already bound it. Added per-frame deferred-destroy queue
  drained after fence wait at the top of `beginFrame`.

### Phase 3 — Port `CivCraft/client`

The bulk of the work:

- `chunk_mesher.cpp` — VBO uploads → staging buffer + transfer queue. Add
  per-chunk deletion queue keyed by frame index so GPU-side buffers aren't
  freed while still in flight.

  **Foundation landed:** the RHI now exposes
  `createVoxelMesh / updateVoxelMesh / drawVoxelsMesh / renderShadowsMesh /
  destroyMesh` with a per-frame deferred-destroy queue. The playable slice's
  static village uploads its 58k voxels once at init and reuses the handle
  every frame instead of streaming them through the dynamic `drawVoxels`
  path. `updateVoxelMesh` keeps the handle stable: fast-path memcpy when
  the new instance count fits in the existing buffer, grow-path 2× realloc
  with defer-destroy of the old buffer when it doesn't — so chunk meshers
  can call it on every block break/place without churning handles.
  Demonstrated by a right-click "dig" mechanic in `civcraft-ui-vk` that
  carves voxels out of the village and re-uploads via `updateVoxelMesh`
  per click. Chunk meshes will go through the same surface — one handle
  per chunk, updated on remesh, dropped via `destroyMesh` when the chunk
  unloads.
- `renderer.cpp` — ~207 call sites. Port pipeline-by-pipeline:
  - Terrain pipeline: vertex/index buffers, UBO for view/proj, push
    constants for per-chunk offset.
  - Sky pipeline.
  - Fog pipeline (probably fold into terrain as a push-constant driven
    post-step — decide during implementation).
  - Shadow pass: separate render pass, depth-only image, sampler in the
    terrain descriptor set.
  - Offscreen targets for any FBO-based effects.
- `fog_of_war.h` — replace `glTexSubImage2D` upload with
  `vkCmdCopyBufferToImage` via a small staging buffer.
- CivCraft shaders (`terrain.*`, `sky.*`, `fog.*`) — same audit + SPIR-V
  compile as platform shaders.

### Phase 4 — Debug / tooling parity

- `src/CivCraft/development/*_scenario.h` + `debug_capture.h` — replace
  `glReadPixels` with `vkCmdCopyImageToBuffer` readback helper in the RHI
  (single function, called by F2, scenario capture, and auto-screenshot).
- `src/platform/tools/model_editor/main.cpp` — port to RHI. Small, isolated,
  good proof that the abstraction holds outside the main client.

### Phase 5 — Cutover + Cleanup (DEFERRED, separate later commit)

**Phase 5 does not happen as part of this migration.** Parallel binaries
ship and coexist. Phase 5 is executed only after a full release cycle on
`civcraft-ui-vk` with no regressions reported against `civcraft-ui`.

Gate to begin Phase 5: every visual QA path — `make item_views`,
`make character_views`, `make test_e2e`, F2 in-game, scenario
auto-screenshots — on `civcraft-ui-vk` matches `civcraft-ui` baselines
pixel-for-pixel (or has documented, accepted differences), **and** one
release cycle has passed with no user-visible regression.

Step 1 — **Cutover** (one commit, reversible):
- Rename binary `civcraft-ui` → `civcraft-ui-gl` (keep building).
- Rename binary `civcraft-ui-vk` → `civcraft-ui` (new primary).
- `make game` now launches the Vulkan binary.
- `make game-gl` is added as an escape hatch to run the GL binary.

Step 2 — **Cleanup** (separate commit, after one more quiet cycle):
- Delete `src/platform/client/rhi/rhi_gl.cpp` (native only — web keeps
  GLES3 backend).
- Delete `civcraft-ui-gl` target from `CMakeLists.txt`.
- Remove `FetchContent_Declare(glad …)` and `glad_add_library(…)` from
  `CMakeLists.txt` native path.
- Remove `OpenGL::GL` from native `target_link_libraries`.
- Delete the runtime GLSL compile code path in `shader.cpp`. Native ships
  only SPIR-V; web continues to compile GLSL at runtime.
- Delete `imgui_impl_opengl3` from `imgui_lib` on native (keep for web).
- Delete any GL-specific debug hooks (`GL_DEBUG_OUTPUT`, etc.).
- Update `src/CivCraft/docs/DEBUGGING.md`, `18_WEB_CLIENT.md`, and
  `00_OVERVIEW.md` to describe the Vulkan native / GLES3 web split.

### File change summary

**Modify (~25 files)**
- `CMakeLists.txt`, root `Makefile`
- `src/platform/client/{window,shader,model,entity_drawer,particles,text,ui,
  lightbulb_drawer,floating_text,model_preview,model_icon_cache,
  game_render,game,game_playing,game_ui}.cpp/h`
- `src/CivCraft/client/{renderer,chunk_mesher,fog_of_war,gameplay_interaction}`
- `src/platform/tools/model_editor/main.cpp`
- `src/CivCraft/development/*_scenario.h`, `debug_capture.h`
- Every `.vert` / `.frag` under `src/platform/shaders/` and
  `src/CivCraft/shaders/` (bindings + locations audit)

**Create (~8 files)**
- `src/platform/client/rhi/rhi.h`
- `src/platform/client/rhi/rhi_vk.cpp` (+ `rhi_vk_device.cpp`,
  `rhi_vk_swapchain.cpp`, `rhi_vk_pipeline.cpp`, `rhi_vk_readback.cpp` —
  split for readability)
- `src/platform/client/rhi/rhi_gl.cpp` (transitional, deleted Phase 5)
- `src/platform/client/gfx.h` (replaces `gl.h`)
- `tools/compile_shaders.cmake` (offline SPIR-V compile helper)

**Delete (Phase 5)**
- `src/platform/client/gl.h`
- `src/platform/client/rhi/rhi_gl.cpp` (native)
- GLAD `FetchContent` block in `CMakeLists.txt`
- GL-only paths in `shader.cpp`
- `imgui_impl_opengl3` in native `imgui_lib`

### Risks

- `renderer.cpp` is the single biggest file and the single biggest source
  of visual regressions. Vulkan has depth range [0,1] (GL default [-1,1])
  and Y-flipped clip space — these are the classic sources of bugs.
  Mitigation: GLM with `GLM_FORCE_DEPTH_ZERO_TO_ONE`; negative viewport
  height to flip Y; land behind `--gfx=vulkan` flag; pixel-compare
  screenshots every phase.
- Descriptor-set churn on per-entity draws can balloon allocator pressure.
  Use one descriptor pool per frame-in-flight; reset, don't free.
- Shader hot-reload during dev: offline compile breaks the
  "edit-shader-and-restart-client" loop. Mitigation: `--hot-shaders` flag
  that uses `shaderc` at runtime for dev only; production builds use the
  offline `.spv`.
- Emscripten path: the RHI surface must be narrow enough that the GLES3
  backend stays small. Any Vulkan-only feature (compute, mesh shaders,
  etc.) we adopt later must either have a GL fallback or be guarded by a
  capability flag.

### Verification gates

Each phase ends at a green gate before the next begins. **All gates compare
`civcraft-ui-vk` against `civcraft-ui` (GL) side-by-side — GL is the living
baseline throughout.**

1. Phase 0 — both binaries produce pixel-identical screenshots (vk uses GL
   backend stub).
2. Phase 1 — `civcraft-ui-vk` menu renders on real Vulkan; title-screen
   screenshot matches GL.
3. Phase 2 — per-drawer: `make item_views` and `make character_views` run
   against both binaries produce matching output.
4. Phase 3 — full game: `make compare` spawns both clients against one
   server in the same village, F2 screenshot diff is within tolerance;
   `make test_e2e` unchanged.
5. Phase 4 — every scenario under `development/*_scenario.h` produces
   matching PPMs from both binaries.
6. **Ship gate** (end of migration proper) — `civcraft-ui-vk` is released
   alongside `civcraft-ui`. No deletion yet.
7. Phase 5 (later) — cutover + legacy deleted; `rg -w 'gl[A-Z]|GLuint|GLint|
   glad|GL_'` returns only web-build and comment hits.

---

## Landing order (cross-part)

1. **Part 1** (binary rename) — one commit, mechanical. Lands first so
   Parts 2/3 don't churn the new name.
2. **Part 2** (model format) — parallel-safe with Part 3; doesn't touch
   rendering code. Land Phase 0 (loader + converter), then convert all
   models, then cleanup.
3. **Part 3** (Vulkan) — phases 0–5 as above. Each phase is one or more
   commits; each ends on a verification gate.

No part blocks the others beyond Part 1. A rough timeline: Part 1 is hours,
Part 2 is days, Part 3 is weeks.
