# VK Migration — Remaining Plan (Self-contained)

**Audience:** A fresh Claude Code session with no prior conversation context.
This document is the single hand-off artifact. Read it top-to-bottom and you
will know what's done, what's left, and exactly where to make every change.

**Repo:** `/home/haoxuw/workspace/AiCraft` (branch `master`).
**Project doc:** `CLAUDE.md` at repo root has the architecture invariants
(Rules 0–6) — read it before touching any code.

---

## 1. Where we are

### 1.1 Two clients, one engine

The repo ships **two parallel rendering backends** built on a shared abstraction:

| Binary | Backend | Where it runs | Source roots |
|---|---|---|---|
| `civcraft-ui` | OpenGL | Native + Web (Emscripten/WASM) | `src/platform/client/game.cpp` + `src/CivCraft/client/*` |
| `civcraft-ui-vk` | Vulkan | Native only | `src/platform/client/game_vk*.cpp` |

Both call into `civcraft::rhi::IRhi` — a backend-agnostic drawer interface
defined in `src/platform/client/rhi/rhi.h`. Implementations:

- `src/platform/client/rhi/rhi_gl.cpp` — OpenGL backend
- `src/platform/client/rhi/rhi_vk.cpp` (+ `rhi_vk.h`) — Vulkan backend
- `src/platform/client/rhi/rhi_ui.cpp` — backend-agnostic 2D text/rect
  layered on top (uses `ui_font_8x8.h`)

**Strategic decision (commit `d18d42f`):** keep both backends. GL is the only
path that works in the browser (WebGL2). VK is the native target where we want
modern features. Do NOT delete GL.

**Rendering surface that VK exposes** (from `rhi.h`):

```
MeshHandle createVoxelMesh / updateVoxelMesh / destroyMesh
MeshHandle createChunkMesh / updateChunkMesh
drawChunkMeshOpaque / drawChunkMeshTransparent / renderShadowsChunkMesh
drawBoxModel(SceneParams, instances*, count)        // 9 floats/box
drawParticles(SceneParams, particles*, count)       // 8 floats/particle
drawRibbon(SceneParams, points*, count)             // ≥2 points
drawText2D(text, x, y, scale, rgba[4])              // helper on top
drawRect2D(x, y, w, h, rgba[4])                     // helper on top
```

Anything that draws in either client must reduce to these calls.

### 1.2 Architecture invariants (NON-NEGOTIABLE — from CLAUDE.md)

| Rule | Summary |
|---|---|
| **R0** | Server validates exactly four `ActionProposal` types: `Move(0)`, `Relocate(1)`, `Convert(2)`, `Interact(3)`. Everything else is a Python helper that lowers to one of these four. |
| **R1** | Python is the game; C++ is the engine. No hardcoded gameplay constants in C++. Constants live in `src/CivCraft/shared/...Tuning` or in artifacts. |
| **R2** | Player isn't special. Entity = Living + Item. Player is just a Living with `playable=true`. Blocks aren't entities. |
| **R3** | Server-authoritative world ownership. Client never writes to `chunk.set()`. Client predicts via `moveAndCollide()` and reports `clientPos` in Move actions; server snaps if drift > 8 blocks. |
| **R4** | All intelligence runs on agent clients (`civcraft-agent` processes — though VK runs them in-process as `AgentClient` instances). Server has zero AI. Player click-to-move uses server-side greedy steering in `src/CivCraft/server/pathfind.h`. |
| **R5** | Server has no display logic. NEVER add to `ServerCallbacks` for visual/audio. Each client derives display from the TCP state stream (e.g. damage text from HP delta in successive `S_ENTITY`). |
| **R6** | Unified physics — one tick loop on the client for ALL entities, one `moveAndCollide()` shared with server. No `tickPlayer` vs `tickNPC`. No dual state — entity position lives in `entity.position` only. See `src/CivCraft/docs/10_CLIENT_SERVER_PHYSICS.md`. |

These rules apply to every change in this plan.

### 1.3 Three-process architecture (always TCP)

| Process | Binary | Concern |
|---|---|---|
| Server | `civcraft-server` | Owns world, validates actions, broadcasts S_*; NO Python, NO OpenGL |
| Player Client | `civcraft-ui` (GL) or `civcraft-ui-vk` (VK) | Renders, takes input, predicts physics; NO Python |
| Agent Client | `civcraft-agent` (and in-process `AgentClient` in VK) | Runs Python `decide()` for one Living entity; NO OpenGL |

Singleplayer = `civcraft-ui` spawns `civcraft-server` as a child via
`ProcessManager` (`src/platform/client/process_manager.h`) and connects over
localhost TCP. There is no in-process server. `TestServer` exists only for
headless E2E tests.

---

## 2. VK file map (where things live in the new client)

```
src/platform/tools/civcraft_ui_vk/main.cpp     # 60-line shell — GLFW + RHI + Game
src/platform/client/game_vk.h                  # Game class header — all member state
src/platform/client/game_vk.cpp                # State machine (Menu↔Playing↔Paused↔Dead)
src/platform/client/game_vk_playing.cpp        # Player tick, input, combat, block ops
src/platform/client/game_vk_render.cpp         # All rendering (world/entities/HUD/menus)
src/platform/client/local_world.h              # Client's chunk store (ChunkSource)
src/platform/client/network_server.h           # TCP connection (no chunk storage)
src/platform/client/process_manager.h          # AgentManager — spawns civcraft-server
src/platform/client/game_logger.h              # Derives DECIDE/COMBAT/INV from TCP
src/platform/client/debug_triggers.h           # File-based /tmp triggers (NDEBUG only)
src/platform/client/audio.h                    # AudioManager (miniaudio) — exists, NOT wired in VK yet
src/platform/client/attack_anim.h              # AttackAnimPlayer — exists, NOT wired in VK yet
```

Player state convention: ALL reads via `Game::playerEntity()` which returns
`Entity*` from the server-broadcast entity map. There is **no `Player` struct**
in VK (per R6). Camera reads `playerEntity()->position`; HP reads
`playerEntity()->hp()`.

---

## 3. Build / run / verify

### 3.1 Build

The root `Makefile` is the source of truth. Parallelism is capped at half the
core count by default (`PAR := nproc/2`). Use `-j$(PAR)` not `-j$(nproc)` —
full parallelism can OOM/crash the machine.

```bash
make build                                 # builds all targets
make build PAR=8                           # override parallelism
cmake --build build -j$(PAR) --target civcraft-ui-vk    # VK only
```

Targets that exist: `civcraft-ui`, `civcraft-ui-vk`, `civcraft-server`,
`civcraft-agent`, `civcraft-test`, `civcraft-test-pathfinding`,
`model-editor`, `imgui_lib`, `imgui_lib_vk`.

### 3.2 Run

```bash
./build/civcraft-ui-vk --skip-menu                       # SP, drop into world
./build/civcraft-ui-vk --skip-menu --log-only            # headless behavior log
./build/civcraft-ui-vk --host 127.0.0.1 --port 7777      # join remote server
./build/civcraft-server --port 7777                      # dedicated server
```

**DO NOT** use `pkill -f civcraft` — it matches the shell's own command line
and SIGTERMs your bash. Use:

```bash
for p in $(pgrep -x civcraft-ui-vk); do kill $p; done
for p in $(pgrep -x civcraft-server); do kill $p; done
```

### 3.3 Visual verification (when the change is visual)

```bash
make build && for p in $(pgrep -x civcraft-ui-vk); do kill $p; done; sleep 0.5
DISPLAY=:1 nohup ./build/civcraft-ui-vk --skip-menu > /tmp/vk.log 2>&1 &
# Auto-screenshot at /tmp/civcraft_auto_screenshot.ppm after ~3s
# OR: touch /tmp/civcraft_screenshot_request → /tmp/civcraft_vk_screenshot_N.ppm
# In-game: F2 = manual screenshot
```

**Auto-pause gotcha:** when launched in background (`nohup`/`&`) the window
never gets focus, and `onWindowFocus(false)` triggers an auto-pause. To
unblock for headless screenshots: `touch /tmp/civcraft_vk_pause_request` to
toggle pause off. Triggers are defined in
`src/platform/client/debug_triggers.h` and listed in
`game_vk_playing.cpp::processInput`.

### 3.4 Behavioral verification (preferred when the change isn't visual)

The log path is preferred over screenshots for AI/behavior/pathfinding work
because it's diff-able. Log format:

```
[HH:MM:SS] [CATEGORY] <actor> <event>
```

Categories: `DECIDE` / `MOVE` / `ACTION` / `COMBAT` / `DEATH` / `INV`. All
derived client-side from the TCP state stream — Rule 5 compliant.

```bash
./build/civcraft-ui-vk --skip-menu --log-only &
sleep 10 && for p in $(pgrep -x civcraft-ui-vk); do kill $p; done
# Read /tmp/civcraft_game.log — grep for DECIDE/ACTION/COMBAT
```

Prior session log preserved as `/tmp/civcraft_game.log.prev`.

### 3.5 E2E baseline

```bash
./build/civcraft-test
```

Baseline is **6 known-failing tests** (P53 village-spawn-in-block, B2/B5
behavior-file paths, W3/W4 template gaps, R3 reconciliation drift). These are
pre-existing template/behavior issues, NOT VK-touched code. Any **new**
failure is a regression.

---

## 4. What just landed (recent VK work — context for what's next)

These items are done in `master` (uncommitted to working tree, all builds clean):

- **LMB/RMB input parity with GL** (just landed in this session).
  `game_vk_playing.cpp` lines ~571–704. LMB held in FPS/TPS = continuous
  mining; LMB edge with entity in cone (closer than block) = attack via
  `tryServerAttack()`. RMB edge = inspect entity (closer) else `placeBlock()`.
  RPG/RTS unchanged: LMB = `clickToMove`, RMB = quick-click action.
  The previous mapping (RMB-to-break, shift+RMB-to-place) was Minecraft-wrong
  and surprised users.
- **Survival 3-hit block break** with crack overlay particles, per-hit burst,
  `n/3` floater. `game_vk.h::BreakState`, drawn in `game_vk_render.cpp` via
  `facePoint()` UV→3D mapping on the targeted face.
- **Block highlight wireframe** via 12 thin `drawBoxModel` boxes for cube
  edges (`renderWorld()`).
- **Move-target spinner** — particle ring at `m_moveOrderTarget` for RPG/RTS
  click-to-move (`renderEffects()`).
- **Hit-event particles** — 8-particle burst per mining swing, expanding
  outward with block color, fades over 0.4s (`m_hitEvents`).
- **Crosshair hitmarker** — orange flash on damage, red on kill, 0.18s.
  Triggered in `tryServerAttack()`; rendered in `renderHUD()`.
- **Crosshair scoping** — only in FPS/TPS, screen-center (over-the-shoulder
  in TPS, NOT character-anchored).
- **NPC walk animation fixed** — was always cycling on `m_wallTime * 3.5f`.
  Now `phase = (speed > 0.3) ? wallTime * speed * 2.5 + hash : 0`. Idle NPCs
  no longer swing limbs.
- **F3 PosErr2 restored** — `m_server->getServerPosition()` vs
  `playerEntity()->position`; green when small, red when > 4 m².
- **`Player` struct eliminated** — all reads through `playerEntity()`.
  Aligns with R6 ("no dual state").
- **`--log-only` headless mode + GameLogger** — DECIDE/COMBAT/INV derived
  from TCP, written to `/tmp/civcraft_game.log` and stdout.
- **F12 admin / F11 fly toggles**, V-key camera cycle, Tab inventory,
  H handbook, F2 screenshot, F3 debug overlay all wired.

---

## 5. Remaining work (ordered)

### Tier 1 — Ship gate (#15: VK at full GL parity)

These three items are the gate for declaring "VK ships."

#### 5.1 Door swing animation overlay (#57)

**Problem:** RMB on a door already sends `Interact` and the server toggles
`door` ↔ `door_open` (both blocks visible in `src/CivCraft/content/builtin.cpp`).
The chunk re-meshes, so the door visually pops between states. GL fades a
hinged quad over 0.25s in between; VK currently has nothing.

**Reference (GL):**
- `src/CivCraft/client/renderer.h` lines 17–24 — `DoorAnim {basePos, height,
  timer, opening, hingeRight, color}` struct.
- `src/CivCraft/client/renderer.cpp` lines 750–839 —
  `Renderer::renderDoorAnims()`. Builds a per-frame mesh of 4-corner panels
  rotated by `theta = opening ? t*PI/2 : (1-t)*PI/2` with smoothstep ease,
  uploads via `updateChunkMesh` to a single persistent handle, draws via
  `drawChunkMeshOpaque`. Pivot picked from `hingeRight` (left = pivot at
  `(fx, fz)`, right = pivot at `(fx+1, fz)`).
- `src/platform/client/game_playing.cpp` lines 470–481 — push/expire logic.
  Push happens when `m_gameplay.doorToggled()` fires; expire when
  `timer >= 0.25f`.

**VK change:**
1. Add `struct DoorAnim` and `std::vector<DoorAnim> m_doorAnims` to
   `game_vk.h` (mirror the GL struct).
2. In `game_vk_playing.cpp` after the RMB handler, when `placeBlock()` would
   have hit a door (peek the block before sending Interact), push a `DoorAnim`.
   Currently `placeBlock()` just sends Convert/Interact and exits — split it
   so we know whether the target was a door. Or: detect via the next-frame
   `S_BLOCK` swap (door ↔ door_open) in the network handler and push the anim
   from there. The latter is more R5-pure (derive from TCP stream).
3. In `game_vk_render.cpp::renderEffects()`, port the GL `renderDoorAnims`
   verbatim — both backends already share the same `drawChunkMeshOpaque` /
   `createChunkMesh` / `updateChunkMesh` RHI calls, so the meshing code is
   copy-paste.
4. Tick `timer += dt` and erase expired anims in `tickFloaters` (or a new
   `tickDoors`).

**Verify:** Stand next to a door in admin mode. Right-click. The door panel
should sweep 90° instead of jump.

#### 5.2 First-person attack hand animation (#58)

**Problem:** In FPS the held item should sweep diagonally on swing. VK draws
no hand model at all in FPS — the screen is empty below the crosshair. GL
uses `AttackAnimPlayer` driving a held-item box-model with arm angles.

**Reference (GL):**
- `src/platform/client/attack_anim.h` — `AttackAnimPlayer`, with built-in
  clips: `swing_left`, `swing_right` (0.32s), `cleave`, `jab`. Combo
  registered via `registerBuiltins()`.
- `src/platform/client/box_model.h` line 130 — clip-specific right-arm angles.
- `src/platform/client/game_playing.cpp` lines 119–124:
  ```cpp
  if (m_gameplay.swingTriggered()) {
      m_attackAnim.triggerOnce("swing_right");
      m_gameplay.clearSwing();
  }
  m_attackAnim.update(dt * m_combatFx.attackDtScale());
  ```
- `src/platform/client/model.h` line 23 — `Model::drawWithRoot()` for FP held
  item.

**VK change (incremental — pick the cheaper option first):**

Option A (cheap, no animation system): single `drawBoxModel` bound to camera
basis, offset to lower-right of the view frustum. On swing trigger
(`m_slashes.push_back(sw)` already fires in our LMB handler), interpolate a
punch arc over `kTune.attackCD = 0.45s` — translate forward + rotate ~60°.
Reuse the existing box pipeline; no new shader.

Option B (full parity): wire `AttackAnimPlayer` + `box_model` into
`game_vk_render.cpp::renderEntities()`. Trigger from LMB swing; transform the
camera-bound box by the clip's per-bone matrix. This pulls in `model.h` and
`box_model.h`, neither of which currently link into VK — check
`CMakeLists.txt` `add_executable(civcraft-ui-vk …)` for the source list.

Recommend Option A first (a 30-line addition); promote to B only if visual
parity is required.

**Verify:** F2 screenshot in FPS during LMB → diagonal hand sweep visible.

#### 5.3 Audio (#60) — MUCH cheaper than originally scoped

**Surprise:** the audio system already exists at
`src/platform/client/audio.h` (`AudioManager` wrapping miniaudio). GL
already uses it (`game.h` line 263 `AudioManager m_audio`). VK just hasn't
linked it in.

**Why it's not done yet:** VK's CMake target probably doesn't include
`audio.cpp` or link miniaudio. Verify against `CMakeLists.txt`:

```bash
grep -A 50 "add_executable(civcraft-ui-vk" CMakeLists.txt | grep -i "audio\|miniaudio"
```

**VK change:**
1. Add `audio.cpp` to the VK source list in `CMakeLists.txt` (mirror what
   `civcraft-ui` does).
2. Add `AudioManager m_audio;` to `game_vk.h`.
3. Init in `Game::init` via `m_audio.init()`.
4. Hook to TCP events (R5: client-side, derived from broadcast):
   - **Footstep**: trigger when `m_walkDist` crosses an integer step
     boundary and `m_onGround`.
   - **Block break**: in network handler, on `S_BLOCK` AIR transition for an
     adjacent block, play break sound named after the previous block.
   - **Block place**: in `placeBlock()` after Convert send.
   - **Damage**: in entity reconciler / `S_ENTITY` handler, on HP delta.
   - **Death**: on `S_REMOVE` with last HP ≤ 0.
   - **Door toggle**: when door anim is pushed (5.1).
   - **Pickup**: on `S_INVENTORY` add of an item that wasn't there.
5. Sound files: live under `artifacts/resources/sounds/` and are referenced
   by `block.sound` / `item.sound` artifact fields so artists can override.

**Verify:** Headless `--log-only` doesn't help here. Manual: launch VK, walk
on grass (footstep), break a block (break sfx), place (place sfx).

### Tier 2 — RHI completeness (#14)

#### 5.4 Phase 4: offscreen render + image readback

**Why:** Our screenshot path currently reads back the swapchain image in
`rhi_vk.cpp`. That's fragile across swap modes and won't work for headless
screenshot scenarios (`make item_views`, `make character_views`) which today
launch GL through `civcraft-ui`. Goal: switch those targets to VK.

**RHI change** — add to `src/platform/client/rhi/rhi.h`:

```cpp
struct OffscreenTarget { uint32_t id; int w; int h; };
virtual OffscreenTarget beginOffscreen(int w, int h) = 0;
virtual void            endOffscreen(OffscreenTarget t) = 0;
virtual std::vector<uint8_t> readPixelsRGBA(OffscreenTarget t) = 0;
```

VK implementation in `rhi_vk.cpp`: create a transient color attachment +
depth, render into it instead of the swapchain image, then
`vkCmdCopyImageToBuffer` into a host-visible staging buffer, map and copy out.

GL implementation in `rhi_gl.cpp`: FBO + `glReadPixels`.

**Wire-in:** `src/platform/client/game_vk_render.cpp` — wrap the
item_views / character_views render loops in `beginOffscreen`/`endOffscreen`
and write the readback as PPM. Mirror `civcraft-ui`'s `--debug-scenario
item_views --debug-item base:sword` flag, which today writes
`/tmp/debug_N_<suffix>.ppm`.

**Verify:** `./build/civcraft-ui-vk --skip-menu --debug-scenario item_views
--debug-item base:sword` produces 5 PPMs (FPS, TPS, RPG, RTS, ground) and
exits.

### Tier 3 — GL features that may need porting (verify before declaring parity)

These are not in the task list because we don't know which still matter. Each
is a one-grep check + go/no-go decision:

#### 5.5 Inventory drag-and-drop (Tab panel)

GL: `src/CivCraft/client/inventory_visuals.h` + ImGui drag handlers.
VK: `game_vk.h` line 296 `m_invOpen` — currently a read-only listing.
Decide: ship as read-only (acceptable) vs port full drag-to-hotbar.

#### 5.6 Per-equip weapon model on TPS character

GL renders the held item attached to the character's right hand. VK's
`renderEntities()` draws a generic silhouette only.
Check: `grep -n "equippedItem\|heldItem\|hotbar.*get" src/CivCraft/client/renderer.cpp`.
Decide cost vs visual parity gain.

#### 5.7 Settings menu (sensitivity, FOV, render distance)

GL: pause menu has sliders. VK: values baked into `kTune` (game_vk.h:60).
Decide: ship with hardcoded `kTune` (acceptable for SP) vs port ImGui sliders
that mutate `kTune` at runtime.

#### 5.8 Death overlay polish

VK has respawn button. Verify cause-of-death text formatting matches GL via
side-by-side screenshots.

#### 5.9 Crafting / recipe UI

Confirm whether GL has one. If yes, port. If no (the codebase doesn't seem to
have crafting yet — Convert handles all transformations), skip.

---

## 6. Explicit non-goals

- **Hunger** — user explicitly killed this. Don't add.
- **Replacing GL** — keep both backends per `d18d42f`. GL serves the web
  build via Emscripten/WebGL2.
- **Per-frame VK validation in Release** — debug-only.
- **Voice chat / multiplayer chat** — not planned for either client.
- **Server-emitted audio/visual** — Rule 5 violation. All effects derive
  client-side from the TCP state stream.

---

## 7. Tactical guidance for the next session

### 7.1 Pitfalls (from `src/CivCraft/docs/24_COMMON_PITFALLS.md` and lived experience)

- **Header-only changes don't trigger rebuild.** After editing
  `game_vk.h`, `touch src/platform/client/game_vk.cpp` (or `_playing` /
  `_render`) before `make build`.
- **Don't `pkill -f civcraft`** — kills your own shell. Use `pgrep -x`.
- **Don't sleep > 270s.** Anthropic prompt cache TTL is 5 minutes.
- **One-shot vs continuous behaviors.** If you add a `BehaviorAction` that
  creates entities/modifies blocks/deals damage, it MUST go in
  `extractOneShots()` in `src/platform/agent/behavior_executor.h`. Otherwise
  it fires every tick.
- **Visual-only races are fine.** Don't add locks for one-frame glitches.
- **Red lightbulb** above an entity = error. Green/white = healthy GOAL.
  Gray "…" = pre-decide.

### 7.2 Order of attack

If you have one session to spend, do **5.3 (audio)** first — biggest
felt-improvement-per-line ratio because the system already exists. Then 5.1
(doors) — small isolated change with a clear GL reference. Then 5.2
(FPS hand) Option A. Then 5.4 (offscreen). Tier 3 only if explicitly asked.

### 7.3 Commit hygiene (from CLAUDE.md)

- Present tense, capital first letter, < 70 chars.
- Area prefix: `client:`, `vulkan:`, `vk:`, `audio:`, etc.
- Don't auto-commit. Wait for user to say "commit."
- Create new commits — don't `--amend` after a hook fails.
- Don't pass `--no-verify` unless explicitly asked.

### 7.4 Verification you must do before claiming "done"

1. `make build` — clean build, no warnings introduced.
2. `./build/civcraft-test` — baseline 6 failures, no new ones.
3. Visual change → screenshot (`/tmp/civcraft_*_screenshot_N.ppm`).
   Behavior change → `/tmp/civcraft_game.log` grep.
4. Do not claim "it works" without one of the above receipts. The user has
   pushed back hard on unsubstantiated success claims.
