# Handbook — Feature Inventory & Replacement Spec

> **Partially superseded (2026-04):** the on-disk `player/` artifact tier
> and `ArtifactRegistry::forkEntry()` are gone. User-authored content will
> be served from a DB layer; sections below that describe fork buttons,
> `isBuiltin` gating, `playerNS`, or `player/<category>/<id>.py` save paths
> are kept for visual-design reference only — the save target is no longer
> on-disk. All other Handbook spec (browsing, preview rig, model/behavior
> editors) still stands.

Detailed description of every feature in and around the Handbook, written
so the replacement UI in the Vulkan rewrite can hit at least feature
parity — and ideally look significantly better.

**Scope expanded after rescan.** The Handbook is not a single file; it's
a cluster of cooperating systems: the main-menu shell that hosts it, the
shared 3D preview rig, the model editor, the behavior editor, the audio
preview hooks, the artifact registry's fork machinery, and the
WASM/download path. All are covered below.

Baseline code reviewed:
- `src/platform/client/handbook_ui.h` (462 lines) — the Handbook proper.
- `src/platform/client/model_editor_ui.h` (228 lines) — in-place model editor.
- `src/platform/client/behavior_editor.h` (572 lines) — visual behavior tree editor.
- `src/platform/client/imgui_menu.h` — main-menu shell + Handbook page integration.
- `src/platform/client/model_preview.h` + `model.h` — shared FBO preview rig.
- `src/platform/client/model_writer.h` — `.py` emitter for save round-trip.
- `src/platform/client/audio.h` — blip channel + group/file preview API.
- `src/platform/shared/artifact_registry.h` — registry + `forkEntry()`.
- `src/platform/shared/material_values.h` — authoritative value lookup.
- `src/platform/client/game.cpp:196-219` — boot-time model registration.

---

## 0. What the Handbook is, conceptually

A browseable encyclopedia of every piece of moddable game content,
embedded in the main menu **and** reachable from the pause menu. It's
the primary UI surface where players learn what exists, what it does,
what it's worth, how it behaves — and, critically, **the launch pad for
modding** (fork → edit → save).

The Handbook is deliberately not a settings screen or a debug console.
It's the "Monster Manual" for a game whose content is all user-moddable,
and it's the modding IDE-lite (code viewer + model editor + behavior
editor) for players who want to tweak things without leaving the game.

---

## 1. Menu shell (where the Handbook lives)

**File:** `imgui_menu.h`. Relevant because any replacement Handbook
plugs into this shell — breaking its conventions breaks the whole menu.

- **C&C-inspired left sidebar** with four large buttons: Start game,
  Join a game, **Handbook**, Settings. Quit pinned bottom-left in red.
- Buttons are 48px tall, 6px rounded corners. Active button is orange
  (`0.96, 0.65, 0.15`) with a 4px accent bar on the left edge
  (`IM_COL32(244, 166, 38, 255)`).
- Inactive buttons are off-white with a thin gray border.
- **Web build (Emscripten)** starts on Multiplayer (join-only); native
  starts on Singleplayer.
- **Content pane right of sidebar**, soft-white background
  (`0.97, 0.98, 0.99, 0.80`).
- The Handbook page is one of four enum values: `Page::{Singleplayer,
  Multiplayer, Handbook, Settings}`. `setPage(int)` is a deep-link hook
  (0=SP, 1=Handbook, 2=Settings).
- **`renderHandbookContent()`** at `imgui_menu.h:995` draws a page title
  ("Handbook"), a one-line gray subtitle ("Browse all game content.
  Custom entries marked with *."), and hosts `HandbookUI::renderAllContent()`
  in an embedded child sized `(contentW - 64, contentH - 100)`.
- The `Custom entries marked with *` promise in the subtitle is **not
  currently honored** — there's no star marker on forked entries in the
  list. Bug-level miss; fix in the replacement.

**Parity bar**
- Same four navigation entries (or superset), same active-accent
  treatment, same web/native start-page split, same deep-link API.
- Handbook embedded in page layout, not a floating window.

**Going further**
- Right-click menu on the nav button: "Open in new window", "Pin",
  "Deep link to last viewed entry".
- Search focus (`/`) works globally across all menu pages.
- Remember last-viewed entry per session.

---

## 2. Handbook proper — features in order of screen real estate

### F1. Dynamic category tabs

**What it does**
Top strip of tabs, one per artifact category that currently has entries.
Each tab shows a count: `Creatures (42)`, `Items (18)`, `Blocks (47)`.

**Source of truth**
`ArtifactRegistry::allCategories()` + `byCategory(cat)`. **Tabs are
derived from the registry at render time** — not a hardcoded list.
Adding a category in Python (e.g. `base:structure`) makes a new tab
appear without any C++ change.

**Observed categories today:** `creature`, `character`, `item`, `effect`,
`block`, `behavior`, `resource` (sound groups).

**Subtle details**
- First-letter capitalization on display; underlying id stays lowercase.
- Tab-switch plays an audio blip (`AudioManager::playBlip(1.0, 0.5)`).
  `m_lastTab` gates it so the blip fires only on *change*, not every
  frame — easy to regress.
- Empty categories are skipped entirely.

**Parity bar**
Registry-derived tab set, counts, audio feedback on change, skip empties.

**Going further**
- Icon per category (currently text-only): sword for items, creature
  silhouette for creatures, block cube for blocks, musical note for
  resources.
- Responsive collapse: narrow window → tabs collapse to a dropdown.
- "New" badge on categories that gained entries since last open.
- Saved filters per category (remember last scroll position + filter
  text per tab).

---

### F2. Entry list (left pane)

**What it does**
Scrollable list of entries in the active category, 200px wide. Click to
select; selected row highlights. Selecting a different entry plays a
slightly higher-pitched blip (`playBlip(1.2, 0.4)`).

**Correctness invariants** (must preserve)
- `ImGui::PushID(e->id.c_str())` around each row so two entries with
  the same display name (built-in **Pig** and a fork **Pig**) don't
  collide on ImGui's label-derived widget id. Key rows by id, not by
  name.
- Selection (`m_selectedId`) persists across tab switches.
- Programmatic selection via `selectEntry(id)` — used by starter-select
  and tutorial/demo flows to deep-link.

**Parity bar**
Id-keyed rows, persistent selection, deep-link API, audio on select.

**Going further**
- Search/filter box above the list (`/` to focus). Fuzzy match across
  id + name + description.
- Sort controls: name A–Z, material value, date added, alphabetical
  by mod source.
- **Thumbnails per row** using `model_icon_cache.h` — we already render
  these for inventory icons. Cheap reuse; transforms the list from
  text rows into a proper visual catalog.
- Group by subcategory (Creatures → Animals / Monsters / NPCs) via
  the existing `subcategory` field on entries (currently suppressed
  from the properties table for no visible use — wire it up).
- Star (`*`) or badge for forked/custom entries — fulfills the
  subtitle promise that's currently a lie.

---

### F3. Detail view (right pane) — the biggest surface

Stacked sections, top to bottom:

#### F3a. Title block
- Entry `name` in bold 1.3× font, very dark gray (`0.15, 0.15, 0.15`).
- `id` underneath in muted gray (`0.50, 0.52, 0.56`) — e.g. `base:pig`.
- Horizontal separator.

#### F3b. 3D model preview (`ModelPreview` FBO)

**Shared rig.** `ModelPreview` is a single offscreen FBO passed into
the Handbook AND character-select AND the model editor (see
`imgui_menu.h:229-236` — `setCharacterPreview()`). All three share one
texture, one rotation state, one animation time. Breaking this sharing
means three simultaneous FBOs; costly, and state diverges across UIs.

**Render behavior** (`model_preview.h`):
- 256×256 FBO by default; `glClearColor(0.94, 0.93, 0.90, 1)` warm
  background tied to the menu theme.
- `glm::perspective(35°, aspect, 0.1, 100)` — narrow FOV, no parallax
  distortion.
- Camera at `(0, modelH*0.5, modelH*2.5)` looking at `(0, modelH*0.45, 0)`.
  Auto-scales with `model.totalHeight * model.modelScale`.
- **Auto-rotation**: `m_rotY += dt * 30°/s` when idle (slow showcase spin).
- **Click-and-drag orbit**: on mouse-down over the preview,
  `m_dragging = true`; `ImGui::GetMouseDragDelta(0)` feeds
  `m_rotY += dx*0.5`, `m_rotX += dy*0.3`; vertical clamped to ±45°.
  On mouse-up, `m_dragging = false` and auto-rotation resumes
  immediately (no easing back).
- **Animation time** (`m_animTime`) and clip state are preview-global.
- Flipped UVs (`(0,1) → (1,0)`) on the `ImGui::Image` because OpenGL
  and ImGui disagree on Y.

**Clip selector (handbook-level)** (`handbook_ui.h:198-230`):
- Dropdown rendered next to the preview (`SameLine + BeginGroup`),
  140px wide.
- `idle` option at top, then every named clip on the model.
- Selecting a clip plays it **with walk speed set to 0** so the clip
  drives all motion alone; otherwise the walk cycle competes and
  masks the clip's intent. This is a deliberate trade — preserve it.
- **Per-entry clip ownership** (`m_clipOwnerId`): when the selected
  entry changes, reset the picker so a stale clip doesn't carry
  across unrelated models. Easy to regress.
- Blip on clip change (`playBlip(1.1, 0.4)`).

**Model lookup order** (`handbook_ui.h:178-188`):
1. `entry->fields["model"]` field value (mod can point at a specific model).
2. `entry->name` lowercased.
3. `entry->name` raw.
4. Preview skipped silently if no match.

**Parity bar**
Shared FBO (same instance used by handbook + character-select + editor),
auto-rotation + drag-orbit with ±45° pitch clamp, walk-speed-zero when
named clip active, per-entry clip ownership reset, lookup-order
fallback.

**Going further**
- **Cinematic preview**: full-quality rendering path — soft shadow,
  rim light, subtle ground reflection, depth-of-field on background.
  Currently plain flat-lit model on gray.
- **Backdrop selector**: flat studio / in-world dusk / silhouette.
- **Scale reference toggle**: show a 1-block cube + player silhouette
  next to the model so size is legible.
- **Pose scrub**: timeline slider per clip instead of only loop.
- **Capture button**: write the preview to `screenshots/<id>.png`.
- **Zoom and pan** (not just orbit).
- **Compare view**: side-by-side with the built-in baseline when
  viewing a fork.
- **Lazy rendering**: only tick + re-render when the detail pane is
  visible and this entry is selected. Today runs every frame.

#### F3c. Description
Multi-line wrapped text (`TextWrapped`) from `entry->description`
(derived from Python docstring). Empty string → section skipped.

**Parity bar**
Word wrap, skip when empty.

**Going further**
- Markdown renderer: headers, lists, bold, links.
- Inline images (mod authors drop a PNG next to the Python file).
- Crosslinks: `[base:log]` tokens become clickable jumps.

#### F3d. "Edit model" button
- Appears only when a model is registered AND a source path is known
  (`m_modelPaths[name]` is present — registered via
  `registerModelPath()` during boot).
- Clicking opens `ModelEditorUI` in-place, taking over the right pane
  (section M below).
- Low-pitched blip on click (`playBlip(0.8, 0.4)`).

**Parity bar**
In-place swap (not a new window), conditional on model+path, distinct
audio.

**Going further**
- Secondary "Edit behavior" button for creatures — opens the
  `behavior_editor.h` visual tree (already exists, currently only
  reachable from an entity's right-click context menu; wire it in).
- "Open source in editor" button launching `$EDITOR` (native) or
  pop-up a full-pane in-game code editor (we already have
  `code_editor.h`).

#### F3e. Properties table

Always-shown "Material value" row, then one row per
`entry->fields[key]` minus internal keys, in a two-column table.

**What gets shown:**
1. `Material value` — always first, from `getMaterialValue(entry->id)`
   in `shared/material_values.h`. Single source of truth for
   item/block/entity worth. **For living entities this value doubles as
   inventory capacity.** Document that.
2. All other `entry->fields`, EXCEPT filtered: `name`, `id`,
   `description`, `subcategory`.
3. Label humanization: `underscore_case` → `Title Case` (underscores →
   spaces, first letter uppercased).

**Visual:**
- `CollapsingHeader("Properties")`, default-open.
- `ImGuiTableFlags_RowBg | BordersInnerH`.
- Property column fixed 130px; value column stretches.
- Property label in muted gray (`0.50, 0.52, 0.56`), value in default.

**Parity bar**
Material-value row always present, filter list for internal fields,
label humanization, collapsible.

**Going further**
- Type-aware rendering:
  - numbers right-aligned with units,
  - booleans as ✓/✗,
  - colors as swatches,
  - ids as clickable crosslinks,
  - enums rendered as styled badges.
- **Fork diff**: highlight fields the player has changed from the base.
- **Inherited-from annotation**: show which built-in a custom entry
  forked from.
- **Inventory-capacity subrow** for Living: "Material value: 12 (also
  carries 12 units of inventory)".

#### F3f. Sound preview (resource category only)

When `entry->category == "resource"`:
- Parses `entry->fields["groups"]` as comma-separated list, trims
  whitespace.
- For each group:
  - `TreeNodeEx` with `"<group> (N)"` label, default-open.
  - **Play Random** button right-aligned on the header line
    (`GetContentRegionAvail().x - 80`).
  - Expanded: one row per file with `>` play button + basename.
- Uses `AudioManager::filesInGroup()`, `play(groupName, vol)`,
  `playFile(path, vol)`.
- Rendered inside a nested child window (`0, 250`), light bluish-white
  background (`0.96, 0.97, 0.98`).

**Parity bar**
Group play-random, per-file preview, counts, basenames, auto-expand.

**Going further**
- Waveform visualization per file (small, fixed-height).
- Per-group volume slider.
- Loop toggle for long ambient tracks.
- Tag badges (`attack`, `footstep`, `ambient`) sourced from file names
  or a mod's meta file.
- Drag-drop support to drop new `.ogg` files into a group (mods).

#### F3g. Source code viewer (non-resource categories)

Inside `CollapsingHeader("Source Code")`, collapsed by default:
- Child window, 200px tall, `0.97, 0.97, 0.98` background.
- Line numbers in muted gray, 3-digit right-aligned.
- **Naive per-line highlight**:
  - `#` → green (`0.40, 0.55, 0.40`).
  - `def ` / `class ` → dark blue (`0.15, 0.35, 0.75`).
  - `"""` → green (`0.20, 0.66, 0.33`).
  - `return ` / `import ` / `from ` → purple (`0.55, 0.25, 0.78`).
  - default → near-black (`0.22, 0.22, 0.25`).
- Read-only, no selection, no scroll-to-line, no wrap.

**Parity bar**
Line numbers, per-line highlight, collapsed by default.

**Going further**
- Real tokenizer (tree-sitter-python is small; or `libclang`-style
  Python lexer since we already embed Python).
- Click-to-copy per line and whole-block copy.
- Search within source (`Ctrl+F`).
- Jump-to-definition on function names.
- Side-by-side diff for forks, highlighting changed lines.
- **Edit in place** (not just view) — see "Going further" in F3d.
- Syntax highlight should respect the game's wider color palette
  (currently it clashes with the warm cream UI).

#### F3h. File path footer
Gray one-liner: `File: artifacts/creatures/base/pig.py`. Direct from
`entry->filePath`.

**Parity bar** Always show.

**Going further**
- Click to open in `$EDITOR` / reveal in file manager.
- Copy-path button.
- Show file-modified timestamp; warn if modified externally since load.

#### F3i. Fork button ("Fork to Custom")

Only visible for `entry->isBuiltin == true`:
- Calls `m_registry->forkEntry(entry->id)` → copies source to
  `player/<category>/<id>.py` and reloads the registry.
- On success: auto-switches `m_selectedId` to the new `player:<id>`,
  shows a **3s green** status "Forked to player:pig".
- On failure: **3s red** "Fork failed!".
- Prints verbose debug to stdout (basePath, playerNS, source length).
  Retain — has saved hours of debugging.
- If `m_registry == nullptr`: shows red fallback label "(Fork
  unavailable: no registry)" instead of the button.
- Low-pitched blip on click (`playBlip(0.6, 0.4)`).

**Parity bar**
Gated to built-ins, auto-switch to fork, timed status, no-registry
fallback, debug logging, audio.

**Going further**
- "Fork → name it" modal: let the user pick the new id + friendly
  name, instead of auto-naming.
- Post-fork: auto-open `ModelEditorUI` or the in-game code editor.
- Batch fork: fork all creatures, or every entry a mod touches.
- "Share fork" button → export the forked file(s) as a `.zip` mod
  archive (or a github gist on WASM).

---

## M. Model editor (accessed via F3d)

Full-bleed editor that takes over the Handbook detail pane when
`m_editor.isOpen()`. Not a separate window.

### M1. Header strip
- `Editing: <id>` in 1.15× font, dark gray.
- File path under it in muted gray.
- `Close` small button at `GetContentRegionAvail().x - 60`.
- Horizontal separator.

### M2. Two-pane split — left 42% / right 58%

**Left pane (`EditorLeft`):**
- Live 3D preview at top via shared `ModelPreview` (same instance as
  the Handbook — free animation + orbit drag, no extra FBO).
  Display size `leftW - 20`.
- Separator.
- `Parts (N)` label in muted gray.
- `PartList` child window, 200px tall, scrollable. Each row labeled
  `<i> <name>` (or `<i> (unnamed)`); click to set `m_selectedPart`.
- `+ Cube` and `Delete` small buttons below the list.
  - `+ Cube`: appends a default BodyPart at offset `(0,1,0)`,
    halfSize `(0.1,0.1,0.1)`, color `(0.7,0.7,0.7,1)`, selects it.
  - `Delete`: erases selected; clamps selection to new last.

**Right pane (`EditorRight`):**
Scrollable editor for the selected part + model-level fields + Save.

### M3. Per-part editor (`renderPartEditor`)

#### Identity
- `Name` (64-char buffer).
- `Role` (semantic tag: `head`, `arm_r`, etc.).
- `Head (tracks lookYaw/Pitch)` checkbox — flags the head for
  head-tracking system.

#### Geometry
- `Offset (center)` — `DragFloat3`, step 0.01, range [-10, 10], 3
  decimals. Part center in model-local space.
- `Size (full)` — `DragFloat3`, step 0.01, range [0.001, 10]. Editor
  converts to/from `halfSize` on read/write (loader stores halves).
  **Watch:** mutation must update `halfSize`, not a separate field.
- `Pivot` — `DragFloat3` for animation rotation pivot.
- `Color` — `ColorEdit4` full RGBA picker.

#### Walk swing (procedural animation)
- `Axis` — `DragFloat3`, step 0.05, range [-1, 1].
- `Amplitude (deg)` — `DragFloat`, step 0.5, range [0, 180].
- `Phase (rad)` — `DragFloat`, step 0.05, range [-2π, 2π].
- `Speed` — `DragFloat`, step 0.05, range [0, 10].

### M4. Model-level fields (`renderModelLevelEditor`)

Below per-part editor, separated. Drag-float controls for:
- `Total height` (0.1–10).
- `Scale` (0.1–10).
- `Head pivot` — vec3 (-5..5).
- `Right hand`, `Left hand`, `Right pivot`, `Left pivot` — all vec3.
  Hand anchors drive equip-point placement; pivots drive arm-swing.

### M5. Save

Big green button (`0.20, 0.65, 0.35`), 120×32px.

**Native (`#ifndef __EMSCRIPTEN__`):**
- Opens `m_path` as `ofstream`, writes
  `model_writer::emitModelPy(m_working, m_id)`.
- Success: green status "Saved <path> — rebuild to see in-world."
  auto-clearing after 5s.
- Failure (fopen failed): red status "Failed to open <path>".

**Emscripten (WASM):**
- Same Python text via `emitModelPy`, then `EM_ASM` constructs a
  `Blob`, creates a temporary `<a>` with `download=<id>.py`, clicks
  it, revokes the URL. File lands in browser's downloads folder.
- Success: green "Downloaded <id>.py" for 4s.

### M6. Architectural invariants to preserve

1. **Edits write to a working copy**, never to live `m_models`.
   Changes only appear in-world after restart (the Save message says
   so explicitly). This keeps the editor crash-safe and out of the
   simulation's hair. Any future "hot reload" must snapshot in-use
   entities before swapping — don't relax this by default.
2. **Preview is the shared instance.** Animations + clip selector work
   for free. Don't give the editor its own FBO — state diverges, and
   the user's eye catches the inconsistency.
3. **Save path is the file that `model_loader.h` reads.** The
   round-trip must be lossless. Currently `.py`; post-migration
   `.geo.json + .meta.json` per `vulkan_migration.md`.
4. **WASM path is download, not upload.** No cloud save. Out of scope.
5. **Save generates text via `model_writer::emitModelPy` — not ad hoc
   in the editor.** Single emitter, single format. Even the downloaded
   browser blob uses it.

### M7. Missing from model editor that parity should add

- **Undo/redo** (not in the current editor — biggest UX hole).
- **Copy/paste part** between parts, between models, between sessions.
- **Mirror X** — duplicate selected part mirrored across X=0 for bilateral
  symmetry (ears, arms, legs).
- **Snap-to-grid** option on offset/size (0.05 increments).
- **Gizmos**: 3D translate/scale handles rendered in the preview, not
  just numeric drags.
- **Clip editing**: today the editor only edits procedural `swing_*`
  fields. Named clips (attack, wave, dance) still require hand-editing
  the `.py`. At least surface them read-only in the editor so the user
  sees they exist.
- **Auto-save** every 30s to a sibling `.autosave.py` file.

---

## B. Behavior editor (tangentially part of the Handbook flow)

`behavior_editor.h` — 572 lines of visual behavior-tree editing. Not
currently reachable from the Handbook, but clearly belongs there (the
Handbook is where you learn *what* a creature does; the behavior editor
lets you change *how*). Worth including in the replacement spec.

### Data model
- **`BehaviorFunc`** — a built-in function: `{id, label, hasParam,
  paramHint}`.
- **`BehaviorFuncRegistry`** — catalog:
  - **Conditions** (13): `is_day`, `is_night`, `is_dusk`, `threatened`,
    `startled`, `hp_low`, `see_entity(type)`, `near_block(type)`,
    `far_from_flock`, `near_water`, `random_chance(pct)`.
  - **Actions** (15): `idle`, `wander`, `follow_nearest(type)`,
    `flee_nearest(type)`, `follow_player`, `attack_nearest(type)`,
    `find_block(type)`, `break_block(type)`, `drop_item(type)`,
    `graze`, `nap`, `seek_roost`, `seek_water`, `socialize`, `patrol`.
- **`BehaviorExpr`** — recursive tree node. `NodeType` ∈ `{Action,
  Condition, IfThenElse, Sequence, Priority}`. Children vector.
- **`CreatureConfig`** — per-creature `{typeId, behaviorId,
  customBehavior, startItems}`.
- **`BehaviorCompiler`** — emits Python `decide(self, world)` from the
  tree. Emits `from solarium_engine import Idle, Wander, MoveTo,
  Follow, Flee, BreakBlock, DropItem`. Round-trips cleanly.

### UI (`BehaviorExprEditor`)
- Node-type dropdown: Action / Condition / IF THEN ELSE / Sequence /
  Priority List. Changing type reshapes children.
- Function picker per Action/Condition node (160px combo).
- Parameter input (text, 120px) with tooltip from `paramHint`.
- **If/Then/Else** renders indented IF (blue) / THEN (green) / ELSE
  (amber) labels.
- **Sequence / Priority** render numbered child slots with `+ / x`
  buttons.
- Recursion-depth cap; shows red `(max depth)` if exceeded.

### Parity bar for the replacement
- Same condition/action catalog (13+15 functions).
- Same 5 node types with the same compile semantics.
- Live-compile preview (show the generated Python below the tree).
- File save to `player/behaviors/<name>.py`.

### Going further
- **Integration into Handbook**: "Edit behavior" button on creature
  entries, opens the behavior editor in-place (mirroring the model
  editor flow).
- **Drag-drop** rearrangement of priority rules / sequence steps.
- **Type-safe parameter pickers**: `see_entity("player")` should be a
  dropdown of registered creature types, not a text box. Same for
  blocks and items.
- **Simulation sandbox**: preview the behavior against a dummy entity
  in a 3D mini-world inside the editor pane.
- **Undo/redo**.
- **Visual node graph** option (ue4-blueprints style) as an alternative
  to the indented tree.

---

## 3. Integration points the replacement must honor

- **`ArtifactRegistry`** is the data source. No hardcoded categories
  or entry lists.
- **`ModelPreview` + `ModelRenderer`** are shared singletons passed in
  at boot (see `game.cpp:196-219` for the registration path). Handbook,
  character-select, and model editor share one FBO + one rotation
  state + one animation time. Preserve that sharing.
- **`AudioManager`** provides both sound preview and the UI blip channel.
  Blip pitches already form a small vocabulary:
  - `1.0, 0.5` — tab change.
  - `1.1, 0.4` — clip selection.
  - `1.2, 0.4` — entry selection.
  - `0.8, 0.4` — "Edit model" button.
  - `0.6, 0.4` — Fork button.
  Replacement should map the same user actions to the same pitches;
  muscle memory for users is real.
- **`getMaterialValue(id)`** — single source for item/entity value.
- **`ImGuiMenu::handbook()`** returns the shared `HandbookUI` instance.
  Models are registered once at init; the replacement needs an
  equivalent "register once at boot" API
  (`registerModel`/`registerModelPath`).
- **WorldManager** (unused by Handbook itself) hangs off the same
  menu; any nav changes to the menu shell must not break it.
- **Emscripten path**: Save button must still trigger a download,
  not a silent failure.

---

## 4. What the replacement should do *better* (wishlist, above parity)

### Visual
- **Card layout** instead of ImGui flat rows. Each list entry is a
  card with thumbnail, name, one-line summary, material-value badge.
  Grid view as an alternative.
- **Real typography**: proportional font for body copy, variable
  weight, more vertical rhythm. ImGui's default is serviceable for
  debug; the Handbook is player-facing.
- **Cinematic preview** (see F3b "Going further").
- **Animated transitions**: entry selection fades the detail pane
  rather than snapping. Fork success blooms green then settles.
- **Icons**: category tabs with icons, property rows with type icons
  (heart for HP, sword for damage, coin for value).
- **Dark mode**: current warm-cream theme is pleasant but mandatory;
  offer a dark variant.

### Information
- **Global search + filter** across all categories (`/` to focus).
- **Crosslinks**: `drops = base:log` becomes a clickable jump.
- **Built-in vs fork diff** for edited entries.
- **Version / source**: "bundled with game v0.2.0" vs "mod: my_fork".
- **"Used by" back-references**: the Log entry lists every creature/
  recipe that drops or consumes it.

### Modding workflow
- **In-place code editor** using the existing `code_editor.h`.
- **Hot reload** after save: rescan artifacts, snapshot in-use
  entities, swap `m_models`. Today's restart requirement exists only
  because of the working-copy safety invariant in M6.1 — a well-
  defined hot path lifts it.
- **Fork naming modal**.
- **"Open in external editor"** launching `$EDITOR`.
- **Behavior editor surfaced** via "Edit behavior" button on creatures.
- **Share mod archive** button.

### Performance
- **Lazy preview rendering**: only tick when the entry is both
  selected AND its pane is visible.
- **Icon cache for list thumbnails**: reuse `model_icon_cache.h`;
  don't run fresh previews for every visible row.
- **Virtualized list** when entry counts grow (mods could easily push
  past 200 creatures).

---

## 5. Replacement tech notes

- **Parallel binary**: per `vulkan_migration.md`, the new Handbook
  ships inside `solarium-ui-vk` alongside the GL `solarium-ui` until
  parity is proven. Both exist.
- **Toolkit decision**: the Handbook is the biggest UI surface in the
  game. Staying on ImGui gives us reuse across both binaries and keeps
  the replacement purely additive. Switching to a real retained-mode
  UI (Slint, RmlUi, WebView) locks the new Handbook to the Vulkan
  binary from day one.
  - **Recommendation: ImGui + heavy `ImDrawList` custom drawing** for
    cards, thumbnails, and icons. Defer the toolkit swap — it's a
    separate project.
- **Feature flag**: `--new-handbook` while it grows.
- **A/B**: the `make compare` harness from the Vulkan plan can also
  screenshot the Handbook side-by-side for visual parity checks.

---

## 6. Parity checklist

A replacement Handbook is feature-complete when it has:

**Menu shell**
- [ ] Four nav buttons with active-accent treatment.
- [ ] Embedded-in-page layout, not a floating window.
- [ ] Web build starts on Multiplayer, native on Singleplayer.
- [ ] `setPage(int)` deep-link API.
- [ ] `Custom entries marked with *` actually marks them.

**Category tabs (F1)**
- [ ] Registry-derived, counts per tab, audio on change.
- [ ] Empty categories skipped, first-letter capitalization.

**Entry list (F2)**
- [ ] Id-keyed rows (name collisions safe).
- [ ] Persistent selection across tab switches.
- [ ] `selectEntry(id)` deep-link API.
- [ ] Audio on select.

**Detail pane (F3)**
- [ ] Title + id block, material-value row always first.
- [ ] Shared `ModelPreview` with auto-rotate + drag-orbit + pitch
      clamp.
- [ ] Per-entry clip selector (idle + all named clips, walk-speed-0
      during clip playback).
- [ ] Description with word wrap, skipped when empty.
- [ ] Edit-model button, conditional on model+path.
- [ ] Properties table with filter list, label humanization,
      collapsible.
- [ ] Resource sound preview: group play-random + per-file preview
      + counts + file basenames.
- [ ] Source code viewer with line numbers + highlight + collapsed
      default.
- [ ] File path footer.
- [ ] Fork button gated to built-ins, auto-switch, timed status,
      no-registry fallback, debug logging.

**Model editor (M)**
- [ ] In-place swap (not a new window).
- [ ] Shared preview (same FBO).
- [ ] Parts list + `+Cube` / `Delete`.
- [ ] Identity / Geometry / Walk-swing editor per part, full range
      + step matches.
- [ ] Model-level editor (height, scale, head pivot, hand/pivot).
- [ ] Save: native file write OR WASM download, via
      `model_writer::emitModelPy`.
- [ ] Working-copy safety: never mutate live `m_models`.

**Behavior editor (B) — new integration**
- [ ] 13 conditions, 15 actions preserved.
- [ ] 5 node types with identical compile semantics.
- [ ] Live-compile Python preview.
- [ ] Save to `player/behaviors/<name>.py`.
- [ ] Reachable from Handbook creature entries via "Edit behavior".

**Audio blip vocabulary**
- [ ] Tab change `1.0, 0.5`.
- [ ] Clip selection `1.1, 0.4`.
- [ ] Entry selection `1.2, 0.4`.
- [ ] Edit-model button `0.8, 0.4`.
- [ ] Fork button `0.6, 0.4`.

**Integration points**
- [ ] ArtifactRegistry, ModelPreview, AudioManager, getMaterialValue,
      shared ImGuiMenu instance.
- [ ] WASM build: save → download.
- [ ] Boot-time `registerModel` + `registerModelPath` API.

Above parity, the wishlist in section 4 is where the new Handbook
earns its keep.
