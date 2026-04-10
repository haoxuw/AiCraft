# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Read `docs/00_OVERVIEW.md` before making ANY gameplay changes.**

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

See `docs/00_OVERVIEW.md` § Action Types and `docs/03_ACTIONS.md`.

### Rule 1: Python Is the Game, C++ Is the Engine

**All game content and gameplay rules are defined in Python. C++ only provides
the engine (physics, networking, rendering).** See `docs/00_OVERVIEW.md`.

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

- Each Creatures entity has its own `modcraft-agent` process running Python `decide()`
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

## Architecture

Three process types — **always TCP**, same architecture for singleplayer and multiplayer:

| Process | Binary | What it does |
|---------|--------|-------------|
| Server | `modcraft-server` | Headless, owns world, NO Python/OpenGL |
| Player Client | `modcraft` | GUI, renders world, NO Python |
| Agent Client | `modcraft-agent` | Headless, runs Python AI, NO OpenGL |

Singleplayer: `modcraft` spawns `modcraft-server` as a child process then connects
via localhost TCP. There is no `LocalServer` in-process shortcut. `TestServer`
(`server/test_server.h`) exists only for headless unit tests.

## Build & Run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

make game                  # singleplayer: skip menu, auto-launch server + agents
make server PORT=7777      # dedicated server
make client HOST=X PORT=N  # GUI client (pre-fills join tab)
make stop                  # kill all modcraft processes
make test_e2e              # headless gameplay tests (no OpenGL/network)
make web                   # WASM build + serve on :8080 (needs emsdk at ~/emsdk)
```

Direct invocation:
```bash
./build/modcraft --skip-menu                              # singleplayer
./build/modcraft-server --port 7777                       # dedicated server
./build/modcraft-client --host 127.0.0.1 --port 7777      # network client
./build/modcraft-agent --host 127.0.0.1 --port 7777 --entity 5
```

## Iterative Development

**Fast rebuild + screenshot loop:**
```bash
cmake --build build -j$(nproc) && \
  pkill -f "build/modcraft"; sleep 0.5 && \
  DISPLAY=:1 ./build/modcraft --skip-menu &
# Game auto-writes /tmp/modcraft_auto_screenshot.ppm after ~3s
```

**Screenshot triggers:**
- `/tmp/modcraft_auto_screenshot.ppm` — written ~3s after entering a world
- `touch /tmp/modcraft_screenshot_request` — immediate screenshot → `/tmp/modcraft_screenshot_N.ppm`
- **F2** in-game: manual screenshot

**Visual QA without an interactive window:**
```bash
./build/modcraft --skip-menu --debug-scenario item_views --debug-item base:sword
# writes /tmp/debug_N_<suffix>.ppm per camera angle (FPS/TPS/RPG/RTS/ground)
```

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
  main.cpp            Player client entry: spawns modcraft-server, connects via TCP
  main_server.cpp     Dedicated server entry: spawns agent clients via ClientManager
  main_client.cpp     Network-only player client entry

  shared/             Pure data types — linked by ALL, no OpenGL, no Python
  server/             Authoritative simulation — no OpenGL
    server.h            GameServer: tick, action validation, entity ownership
    client_manager.h    TCP connections, agent process spawning, state broadcast
    entity_manager.h    Spawn, physics — no AI
    test_server.h       GameServer wrapper for headless tests only
  agent/              Agent client — Python + pybind11, no OpenGL
    agent_client.h      TCP, receives world state, runs Python decide(), sends actions
    behavior_executor.h BehaviorAction → ActionProposal translation
  client/             Rendering + input — OpenGL, no Python, no server/
    game.cpp            Main game loop, state machine, UI orchestration
    gameplay.cpp        Player input → ActionProposals
    process_manager.h   AgentManager: spawns modcraft-server for singleplayer
  server/python_bridge.cpp  pybind11 module `modcraft_engine` exposed to Python
  content/            C++ fallback entity/block definitions

artifacts/            Python game content (hot-loadable, no rebuild needed)
  behaviors/          Creatures AI: decide() functions
  creatures/          EntityDef: stats, model, behavior assignment
  items/ blocks/      Item and block definitions
  worlds/             World templates (flat, village)
```

### Dependency Rules
- `shared/` → nothing
- `server/` → `shared/` + `content/` (no OpenGL, no Python)
- `agent/` → `shared/` + `server/behavior.h` + Python
- `client/` → `shared/` + `content/` (no Python, no `server/`)

## Code Style

- Tabs for indentation in C++ and GLSL
- `namespace modcraft { }` wraps all C++ code; `namespace modcraft::net { }` for protocol types
- String IDs: `"base:name"` format (namespace:identifier)
- Header-only for small classes; `.h` + `.cpp` split for larger ones
- When adding a new `BehaviorAction` type: decide if one-shot or continuous.
  One-shot (creates entities, modifies blocks, deals damage) → add to `extractOneShots()`
  in `agent/behavior_executor.h`. See `docs/24_COMMON_PITFALLS.md` #1.
- **Header-only changes don't trigger rebuild**: after editing a header-only content file
  (e.g. `content/entities_animals.h`), `touch` its including `.cpp` to force recompilation:
  `touch src/content/builtin.cpp`

## Key Docs

- `docs/00_OVERVIEW.md` — Full architecture, artifact system, TCP protocol
- `docs/22_BEHAVIORS.md` — Entity AI: wander, peck, prowl, follow, woodcutter
- `docs/24_COMMON_PITFALLS.md` — Known bugs to avoid (one-shot actions, ImGui frames, camera jumps)
- `DEBUGGING.md` — Dev iteration loop, screenshot pipeline

## Game Identity

**This game is called ModCraft.** The name reflects its core design goal:
players can mod ANYTHING and EVERYTHING. Every creature, behavior, item,
block, world, and effect is defined in Python artifacts — fully replaceable,
extendable, and shareable without touching C++. The C++ engine is a pure
platform; all game identity lives in `artifacts/`.

Every feature decision must ask: *"Can a modder override this from Python?"*
If not, it needs to move to an artifact.

## Commit Guidelines

- Present tense, capital first letter, under 70 chars
- Area prefix: `server:`, `client:`, `shared:`, `agent:`, `content:`, `artifacts:`, etc.
