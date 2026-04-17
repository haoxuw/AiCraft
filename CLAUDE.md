# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repo layout

```
src/platform/      C++ engine (headers + cpps)
src/CivCraft/      Voxel sandbox game built on platform/ (native C++)
```

CivCraft is the native C++ voxel sandbox — see its Mandatory Design Rules
below.

**Read `src/CivCraft/src/CivCraft/docs/00_OVERVIEW.md` before making ANY CivCraft gameplay changes.**

## Mandatory Design Rules

These rules are NON-NEGOTIABLE. Every code change must respect them.

### Rule 0: The Server Accepts Exactly Four Action Types ← HIGHEST PRIORITY

**This is the most important architectural invariant. It overrides any other
design consideration.**

The server validates exactly four `ActionProposal` types — nothing more, nothing
less. Every action any entity can ever take must compile down to one of these:

```
TYPE_MOVE     (0) — set entity velocity/direction (no tunnelling)
TYPE_RELOCATE (1) — move an Object between containers (inventory ↔ ground ↔ entity);
                    value is conserved, nothing created from nothing
TYPE_CONVERT  (2) — transform an Object from one type to another (value must not
                    increase); consuming → convert to nothing; casting effect →
                    convert item to hp/effect; empty to_item = destroy
TYPE_INTERACT (3) — toggle interactive block state (door, button, TNT fuse)
```

**Consequences:**
- `Follow`, `Flee`, `Wander` are **NOT action types** — they are Python helpers
  that compute a target position and return `MoveTo` (TYPE_MOVE).
- All AI decision logic (follow, flee, wander, pathfind) lives in Python behaviors,
  never in the C++ bridge or server.
- The C++ bridge (`python_bridge.cpp`) must never resolve or interpret high-level
  action names — it only translates the 4 primitives into `ActionProposal`.

See `src/CivCraft/docs/00_OVERVIEW.md` § Action Types and `src/CivCraft/docs/03_ACTIONS.md`.

### Rule 1: Python Is the Game, C++ Is the Engine

**All game content and gameplay rules are defined in Python. C++ only provides
the engine (physics, networking, rendering).** See `src/CivCraft/docs/00_OVERVIEW.md`.

- Creature definitions, behaviors, items, blocks, effects, models → Python artifacts
- **NEVER hardcode gameplay constants in C++.** Every magic number (distance,
  speed, timer, radius) MUST be configurable from Python.
- **Creatures AI behaviors are game logic** — they live in Python.
  **Player click-to-move** uses server-side greedy steering (`src/server/pathfind.h`)
  for simplicity and reliability. Creatures pathfinding still uses Python (`python/pathfind.py`)
  via agent clients. Navigation tuning constants are in `ServerTuning`.

### Rule 2: The Player Is Not Special

**Entity = Living + Item.** That's it.

- **Living** — moves, has HP, has inventory. Players, NPCs, animals are all Living.
  The player is just a Living entity with `playable=true`. Any Living can be
  played by hijacking its agent client. NPCs are Living with a BehaviorId.
- **Item** — on the ground or in an inventory.
- **Blocks** are NOT entities — they live in the chunk grid. Some blocks (chests)
  have inventories managed separately, keyed by block position.

### Rule 3: Server-Authoritative World Ownership

**The server is the sole owner and modifier of world state.**

- Entities submit `ActionProposal` (intent). Server validates and executes.
- Client NEVER writes to `chunk.set()`.
- **Client-side prediction**: the GUI client runs `moveAndCollide()` locally
  for the player entity (same physics as server) and reports `clientPos` in
  Move actions. The server accepts `clientPos` if within tolerance (8 blocks),
  otherwise snaps back. This makes WASD movement feel instant while the
  server remains authoritative.
- Singleplayer uses the same TCP code paths as multiplayer — no in-process shortcut.

### Rule 4: All Intelligence Runs on Agent Clients

**The server has ZERO intelligence.** All AI runs on agent client processes.

- Each Creatures entity has its own `civcraft-agent` process running Python `decide()`
- Server spawns/manages agent clients via `ClientManager`
- Python behavior code NEVER runs on the server
- **Player click-to-move navigation** is handled server-side (greedy local
  steering in `src/server/pathfind.h`). The GUI client sends `C_SET_GOAL` or
  `C_SET_GOAL_GROUP`; the server sets entity velocities each tick. Player
  entities do NOT need agent clients for navigation.

### Rule 5: Server Has No Display Logic

**The server never decides what to show on any client's screen.**

Server responsibilities: validate actions, update world state, broadcast via TCP
(`onBlockChange`, `onEntityRemove`, `onInventoryChange`). Nothing else.

**NEVER add to `ServerCallbacks` for** visual effects, floating text, HUD notifications,
or per-player display decisions. Each client observes the TCP state stream and decides
its own display:
- Block break text → client detects `S_BLOCK` bid→AIR, looks up block name
- Damage text → client compares HP snapshots from successive `S_ENTITY` messages
- Sounds, particles → client-side, triggered by client-observable events

### Rule 6: Unified Physics, One Source of Truth

**See `src/CivCraft/docs/10_CLIENT_SERVER_PHYSICS.md` for the full spec.**

- **One tick loop** on the client for ALL entities (player + NPCs). No
  `tickPlayer()` vs `tickNPC()` — there is `tickEntities()`.
- **No dual state.** Entity position lives in `entity.position`. No parallel
  `m_player.pos`. The player is just an entity whose input comes from the keyboard.
- **LocalWorld** is the client's single chunk store (`ChunkSource`). Shared by
  the player tick, agent ticks, chunk mesher, and raycasting.
- **`moveAndCollide()`** in `logic/physics.h` is the ONE physics implementation.
  Client and server both call it — the only difference is what backs the
  `BlockSolidFn`: `LocalWorld` (client, incomplete) vs `World` (server, complete).
- **Reconciliation is uniform.** The player and owned NPCs are reconciled against
  server broadcasts with the same logic. No player special-casing.

## Architecture

Three process types — **always TCP**, same architecture for singleplayer and multiplayer:

| Process | Binary | What it does |
|---------|--------|-------------|
| Server | `civcraft-server` | Headless, owns world, NO Python/OpenGL |
| Player Client | `civcraft` | GUI, renders world, NO Python |
| Agent Client | `civcraft-agent` | Headless, runs Python AI, NO OpenGL |

Singleplayer: `civcraft` spawns `civcraft-server` as a child process then connects
via localhost TCP. There is no `LocalServer` in-process shortcut. `TestServer`
(`server/test_server.h`) exists only for headless unit tests.

## Build & Run

The root `Makefile` is the one source of truth. `src/CivCraft/Makefile`
forwards every target to the root so you can `cd src/CivCraft` and use
`make <target>` directly.

**Parallelism is capped at half the core count by default** (`PAR := nproc/2`
in the root Makefile) so a build won't pin the machine or OOM. Override on the
command line: `make build PAR=8` for more, `make build PAR=1` for minimum.
All `cmake --build` invocations in this project should use `-j$(PAR)` (not
`-j$(nproc)`).

```bash
make game                     # CivCraft singleplayer
make server PORT=7777         # dedicated server
make client HOST=X PORT=N     # GUI client (pre-fills join tab)
make stop                     # kill all CivCraft processes
make test_e2e                 # headless gameplay tests
make web                      # WASM build + serve on :8080 (needs emsdk at ~/emsdk)

# Manual cmake (matches what `make build` does):
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(PAR)   # or just `make build`
```

Direct invocation:
```bash
./build/civcraft-ui --skip-menu                           # singleplayer
./build/civcraft-server --port 7777                       # dedicated server
./build/civcraft-ui --host 127.0.0.1 --port 7777          # network client
./build/civcraft-agent --host 127.0.0.1 --port 7777 --entity 5
```

## Iterative Development

**Fast rebuild + screenshot loop:**
```bash
make build && \
  for p in $(pgrep -x civcraft-ui); do kill $p; done; sleep 0.5 && \
  DISPLAY=:1 ./build/civcraft-ui --skip-menu &
# Game auto-writes /tmp/civcraft_auto_screenshot.ppm after ~3s
```

> **Don't `pkill -f civcraft`** — it matches the shell's own command line (which
> contains the word "civcraft") and SIGTERMs your own bash, silently skipping
> the rest of the chained command. Use exact-name matching via
> `pgrep -x civcraft-ui | xargs kill` instead.

**Screenshot triggers:**
- `/tmp/civcraft_auto_screenshot.ppm` — written ~3s after entering a world
- `touch /tmp/civcraft_screenshot_request` — immediate screenshot → `/tmp/civcraft_screenshot_N.ppm`
- **F2** in-game: manual screenshot

**Visual QA without an interactive window:**
```bash
# Items: FPS/TPS/RPG/RTS + on-ground shots
make item_views ITEM=base:sword
# or: ./build/civcraft-ui --skip-menu --debug-scenario item_views --debug-item base:sword

# Characters: 6-angle orbit (front/three_q/side/back/top/rts)
make character_views CHARACTER=base:pig
# or: ./build/civcraft-ui --skip-menu --debug-scenario character_views --debug-character base:pig

# Both write /tmp/debug_N_<suffix>.ppm and auto-exit when done.
```

See `.claude/skills/refine-model-and-animation/SKILL.md` for the full
iteration loop (rubric, common issues, trick list).

**Behavioral QA without a window (headless log mode):**

Use this instead of screenshots when you need to verify *what creatures are deciding and doing*, not what the world looks like. The log is a WoW-style combat/event stream derived entirely from the TCP state stream (Rule 5 compliant — no server-side logging).

```bash
./build/civcraft-ui --skip-menu --log-only         # singleplayer, no GUI
./build/civcraft-ui --log-only --host H --port P   # attach to remote server
# Streams events to stdout AND /tmp/civcraft_game.log (truncated on start;
# prior session preserved as /tmp/civcraft_game.log.prev)
```

In GUI mode the same log is also written to `/tmp/civcraft_game.log` and viewable from **Main Menu → Game Log** and the pause menu. One source of truth; Claude reads the file.

**Log format** — `[HH:MM:SS] [CATEGORY] <actor> <event>`:
| Category  | Source                                   | Example |
|-----------|------------------------------------------|---------|
| `DECIDE`  | `goalText` deltas in `S_ENTITY`          | `woodcutter#7 → MoveTo tree@(45,60,30)` |
| `MOVE`    | position deltas in `S_ENTITY` (throttled)| `chicken#5 @ (32,64,25)` |
| `ACTION`  | `S_BLOCK` break/place                    | `woodcutter#7 broke base:log@(45,60,30)` |
| `COMBAT`  | HP deltas across `S_ENTITY` snapshots    | `wolf#9 → sheep#3 for -5hp (12→7)` |
| `DEATH`   | `S_REMOVE` with last HP=0                | `sheep#3` |
| `INV`     | `S_INVENTORY` diffs                      | `player#1 picked up base:log ×1` |

**Verification recipe (replaces screenshot-driven loops for AI behavior work):**
```bash
make build && for p in $(pgrep -x civcraft-ui); do kill $p; done; sleep 0.5 && \
  ./build/civcraft-ui --skip-menu --log-only &
sleep 10 && for p in $(pgrep -x civcraft-ui); do kill $p; done
# then Read /tmp/civcraft_game.log — grep for DECIDE/ACTION/COMBAT
```

This is the preferred verification path for behavior/AI/pathfinding changes. Use screenshots only when the bug is visual (rendering, animation, UI).

**In-game shortcuts:**

| Key | Action |
|-----|--------|
| F2  | Screenshot |
| F3  | Debug overlay (FPS, XYZ, chunk, entity count) |
| F12 | Toggle admin/survival mode |
| V   | Cycle camera: FPS → TPS → RPG → RTS |
| Tab | Inventory |
| Esc | Pause / back to menu |

## Source Structure

```
src/
  platform/                 ← C++ engine, game-agnostic
    shared/                 Pure data types — linked by ALL, no OpenGL, no Python
                            action.h, entity.h, inventory.h, physics.h, net_protocol.h, …
    server/                 Authoritative simulation — no OpenGL
      server.h                GameServer: tick, action validation, entity ownership
      client_manager.h        TCP connections, agent process spawning, state broadcast
      entity_manager.h        Spawn, physics — no AI
      python_bridge.cpp       pybind11 module exposed to Python
      test_server.h           GameServer wrapper for headless tests only
    agent/                  Agent client — Python + pybind11, no OpenGL
      agent_client.h          TCP, receives world state, runs Python decide(), sends actions
      behavior_executor.h     BehaviorAction → ActionProposal translation
    client/                 Rendering + input — OpenGL/Vulkan, no Python, no server-ownership
      game.cpp                GL client: main game loop, state machine, UI
      game_vk.cpp             VK client: main loop, state transitions, playerEntity()
      game_vk_playing.cpp     VK client: player tick, input, combat, block ops
      game_vk_render.cpp      VK client: all rendering (world, entities, HUD, menus)
      local_world.h           Client's single source of truth for terrain (ChunkSource)
      gameplay.cpp            Player input → ActionProposals
      process_manager.h       AgentManager: spawns the game server for singleplayer
    logic/                  Simulation types (was shared/) — linked by ALL
      entity.h, physics.h, action.h, inventory.h, constants.h, …
    net/                    Networking — net_protocol.h, server_interface.h, net_socket.h
    debug/                  Diagnostics — crash_log.h, move_stuck_log.h, entity_log.h
    shaders/  fonts/  docs/ Platform-level assets + architecture docs

  tests/                    ← E2E and regression tests
    test_e2e.cpp              Headless gameplay tests (uses TestServer)
    test_pathfinding.cpp      Pathfinding regression tests

  artifacts/                ← Python game content (top-level — most-modded surface)
                              hot-loadable, no rebuild needed
    behaviors/              Creature AI: decide() functions
    living/                 EntityDef: stats, model, behavior
    items/  blocks/         Item + block definitions
    worlds/                 World templates (flat, village)
    structures/ models/ effects/ actions/ resources/

  model_editor/             ← Model-authoring tooling (Python, not shipped w/ game)
                              modelcrafter.py       3D clip debugger (matplotlib)
                              bbmodel_export.py     .py model → Blockbench .bbmodel
                              bbmodel_import.py     Blockbench .bbmodel → .py model
                              attack_clips.py       combo-swing keyframes (used by modelcrafter)
                              capture_samples.sh    drive civcraft-ui-vk through game states, snap PPM/PNG
```

### Dependency Rules
- `platform/logic/` → nothing (pure types, linked by all)
- `platform/net/` → `logic/` (networking protocol types)
- `platform/server/` → `logic/` (no OpenGL, no Python except via pybind)
- `platform/agent/` → `logic/` + `server/behavior.h` + Python
- `platform/client/` → `logic/` (no Python, no server ownership)
- `CivCraft/` → `platform/` + its own files

## Code Style

- Tabs for indentation in C++ and GLSL
- `namespace civcraft { }` wraps all C++ code; `namespace civcraft::net { }` for protocol types
- String IDs: `"base:name"` format (namespace:identifier)
- Header-only for small classes; `.h` + `.cpp` split for larger ones
- When adding a new `BehaviorAction` type: decide if one-shot or continuous.
  One-shot (creates entities, modifies blocks, deals damage) → add to `extractOneShots()`
  in `agent/behavior_executor.h`. See `src/CivCraft/docs/24_COMMON_PITFALLS.md` #1.
- **Header-only changes don't trigger rebuild**: after editing a header-only content file
  (e.g. `content/entities_animals.h`), `touch` its including `.cpp` to force recompilation:
  `touch src/CivCraft/content/builtin.cpp`

## Key Docs

- `src/CivCraft/docs/00_OVERVIEW.md` — Full architecture, artifact system, TCP protocol
- `src/CivCraft/docs/22_BEHAVIORS.md` — Entity AI: wander, peck, prowl, follow, woodcutter
- `src/CivCraft/docs/24_COMMON_PITFALLS.md` — Known bugs to avoid (one-shot actions, ImGui frames, camera jumps)
- `src/CivCraft/docs/DEBUGGING.md` — Dev iteration loop, screenshot pipeline

## Game Identity

**This game is called CivCraft.** The name reflects its core design goal:
players can mod ANYTHING and EVERYTHING. Every creature, behavior, item,
block, world, and effect is defined in Python artifacts — fully replaceable,
extendable, and shareable without touching C++. The C++ engine is a pure
platform; all game identity lives in `artifacts/`.

Every feature decision must ask: *"Can a modder override this from Python?"*
If not, it needs to move to an artifact.

## Commit Guidelines

- Present tense, capital first letter, under 70 chars
- Area prefix: `server:`, `client:`, `shared:`, `agent:`, `content:`, `artifacts:`, etc.
