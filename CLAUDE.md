# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repo layout

```
src/platform/      C++ engine (logic/, net/, server/, agent/, client/, debug/, shaders/, fonts/, docs/)
src/artifacts/     Python game content — hot-loadable, no rebuild needed
                   behaviors/ living/ items/ blocks/ models/ worlds/ structures/ effects/ resources/ annotations/
src/python/        Python helpers (behavior_base, conditions_lib, local_world, pathfind, action_proxy, …)
src/config/        Runtime config (controls.yaml — copied into build dir)
src/resources/     Shipped assets (textures, sounds, models)
src/tests/         test_e2e.cpp, test_pathfinding.cpp, behavior_scenario_validation/
src/model_editor/  Authoring tooling (modelcrafter, bbmodel_export/import, capture_samples.sh)
docs/civcraft_legacy/  Game design docs (overview, actions, physics, inventory, …)
```

**Read `docs/civcraft_legacy/00_OVERVIEW.md` before making ANY gameplay changes.**
For engine/protocol/behavior-API details see `src/platform/docs/` (14_ARCHITECTURE_DIAGRAM.md, 21_BEHAVIOR_API.md, 04_SERVER.md, 06_NETWORKING.md).

## Mandatory Design Rules

These rules are NON-NEGOTIABLE. Every code change must respect them.

### Rule 0: The Server Accepts Exactly Four Action Types ← HIGHEST PRIORITY

The server validates exactly four `ActionProposal` types. It overrides any other
design consideration.**

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
- high level `Follow`, `Flee`, `Wander` are **NOT action types** — they are Python helpers
  that compute a target position and return `MoveTo` (TYPE_MOVE).

### Rule 1: Python Is the Game, C++ Is the Engine

**All game content and gameplay rules are defined in Python. C++ only provides
the engine (physics, networking, rendering).**

- Creature definitions, behaviors, items, blocks, effects, models → `src/artifacts/`
- **NEVER hardcode gameplay constants in C++.**
- **Creature AI behaviors are game logic** — they live in Python (`src/artifacts/behaviors/`).

### Rule 2: The Player Is Not Special

### Rule 3: Server-Authoritative World Ownership

**The server is the sole owner and modifier of world state.**

- Entities submit `ActionProposal` (intent). Server validates and executes.
- Client NEVER writes to `chunk.set()`.
- **Client-side prediction**: the GUI client runs `moveAndCollide()` locally
  for the player entity (same physics as server) and reports `clientPos` in
  Move actions. The server accepts `clientPos` if within tolerance (8 blocks),
  otherwise snaps back.
- Singleplayer uses the same TCP code paths as multiplayer — no in-process shortcut.

### Rule 4: All Intelligence Runs on Agent Clients

**The server has ZERO intelligence.** All AI planning runs on AgentClients (along with PlayerClientGUI).

### Rule 5: Server Has No Display Logic

### Rule 6: Unified Physics, One Source of Truth

**See `docs/civcraft_legacy/10_CLIENT_SERVER_PHYSICS.md`.**

- **One tick loop, No dual state**
- **LocalWorld** (`platform/client/local_world.h`) is the client's single chunk
  store (`ChunkSource`). Shared by the player tick, agent ticks, chunk mesher,
  and raycasting.
- **Share pysics `moveAndCollide()`** Client and server both call it, over LocalWorld and World respectively

## Architecture

Singleplayer: `civcraft-ui-vk` spawns `civcraft-server` as a child process then connects
via localhost TCP. There is no in-process shortcut.

## Build & Run

Do not use -j, the compile load may crash the machine
```bash
make game                         # singleplayer via build-perf/  (spawns server, --skip-menu)
make game GAME_PORT=7890          # singleplayer on a fixed port
make server PORT=7777             # dedicated server (interactive world select)
make client HOST=X PORT=N         # GUI client → menu or pre-filled join
make stop                         # kill all civcraft-* processes (exact-name match)
make test_e2e                     # headless gameplay + pathfinding tests
make proxy                        # HTTP→TCP action proxy w/ Swagger UI on :8088

# Manual cmake (matches `make build`):
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j1
```

## Iterative Development

**Don't `pkill -f civcraft`** — it matches the shell's own command line (which
contains the word "civcraft") and SIGTERMs your own bash, silently skipping the
rest of the chained command. Use exact-name matching:
`pgrep -x civcraft-ui-vk | xargs kill` (and the same for `civcraft-server`).

**Screenshots** — written by the Vulkan client on request:
- `touch /tmp/civcraft_screenshot_request` → immediate screenshot → `/tmp/civcraft_screenshot_N.ppm`
- **F2** in-game: manual screenshot

**Visual QA without an interactive window:**
```bash
# Items: FPS/TPS/RPG/RTS + on-ground shots
make item_views ITEM=base:sword
# or: ./build/civcraft-ui-vk --skip-menu --debug-scenario item_views --debug-item base:sword

# Characters: 6-angle orbit (front/three_q/side/back/top/rts)
make character_views CHARACTER=base:pig
# or: ./build/civcraft-ui-vk --skip-menu --debug-scenario character_views --debug-character base:pig

# Full sweep across all characters × clips:
make animation_sweep    # writes /tmp/anim_review/<char>/<clip>.png
```

Both write `/tmp/debug_N_<suffix>.ppm` and auto-exit. See `.claude/skills/refine-model-and-animation/SKILL.md` for the full iteration loop.

**Behavioral QA without a window (headless log mode):**

Use this instead of screenshots when you need to verify *what creatures are
deciding and doing*, not what the world looks like. The log is a WoW-style
combat/event stream derived entirely from the TCP state stream (Rule 5
compliant — no server-side logging).

```bash
./build/civcraft-ui-vk --skip-menu --log-only      # singleplayer, hidden window
./build/civcraft-ui-vk --log-only --host H --port P # attach to remote server
# Streams events to stdout AND /tmp/civcraft_game.log (truncated on start;
# prior session preserved as /tmp/civcraft_game.log.prev)
```

In GUI mode the same log is also written to `/tmp/civcraft_game.log` and viewable
from **Main Menu → Game Log** and the pause menu. One source of truth; Claude
reads the file.


**Verification recipe for behavior/AI/pathfinding work** (preferred over screenshots):
```bash
make build && pgrep -x civcraft-ui-vk | xargs -r kill; sleep 0.5 && \
  ./build/civcraft-ui-vk --skip-menu --log-only &
sleep 10 && pgrep -x civcraft-ui-vk | xargs -r kill
# then Read /tmp/civcraft_game.log — grep for DECIDE/ACTION/COMBAT
```

Use screenshots only when the bug is visual (rendering, animation, UI).

## Source structure (detail)

```
src/platform/
  logic/     Pure data types (entity, chunk, action, inventory, physics, constants). No deps. Linked by all.
  net/       TCP protocol (net_protocol.h, server_interface.h, net_socket.h). Depends on logic/.
  server/    GameServer (server.h/cpp), ClientManager, EntityManager, python_bridge.cpp,
             pathfind.h (player greedy steer), world gen, weather, world_save. No GL, no Python except via pybind.
  agent/     AgentClient runtime (embedded in client process): agent_client.h, behavior_executor.h,
             decide_worker.h, pathfind (Python-backed), decision_queue.h. Links Python.
  client/    Vulkan client: game_vk.cpp (main loop), game_vk_playing.cpp (player tick/input/combat),
             game_vk_render.cpp (rendering), local_world.h (client ChunkSource), chunk_mesher.cpp,
             model_loader, network_server.h, process_manager.h (spawns server for SP), rhi/, ui/.
             NO Python imports directly — AI runs via the embedded AgentClient.
  debug/     Diagnostics — crash_log.h, move_stuck_log.h, entity_log.h
  shaders/vk/  GLSL for the Vulkan pipeline (chunk_terrain.frag, sky.frag, …)
  fonts/  docs/  engine-level assets + architecture docs

src/tests/
  test_e2e.cpp          Headless gameplay tests → civcraft-test (uses TestServer)
  test_pathfinding.cpp  Pathfinding regression  → civcraft-test-pathfinding
  behavior_scenario_validation/  Scenario-driven behavior checks

src/artifacts/          Python game content — hot-loadable, no rebuild needed
  behaviors/  Creature AI: decide() functions
  living/     EntityDef: stats, model, behavior
  items/  blocks/  effects/  actions/  resources/  structures/  models/  annotations/
  worlds/     World templates (flat, village)
```

## Game identity

**This game is called CivCraft.** Players can mod ANYTHING and EVERYTHING. Every
creature, behavior, item, block, world, and effect is defined in Python artifacts MODs

Every feature decision must ask: *"Can a modder override this from Python?"*
If not, it needs to move to an artifact.

## Commit guidelines

- Present tense, capital first letter, under 70 chars
- Area prefix: `server:`, `client:`, `agent:`, `logic:`, `net:`, `artifacts:`, `content:`, `repo:`, etc.
