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
docs/solarium_legacy/  Game design docs (overview, actions, physics, inventory, …)
```

**Read `docs/solarium_legacy/00_OVERVIEW.md` before making ANY gameplay changes.**
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

**See `docs/solarium_legacy/10_CLIENT_SERVER_PHYSICS.md`.**

- **One tick loop, No dual state**
- **LocalWorld** (`platform/client/local_world.h`) is the client's single chunk
  store (`ChunkSource`). Shared by the player tick, agent ticks, chunk mesher,
  and raycasting.
- **Share pysics `moveAndCollide()`** Client and server both call it, over LocalWorld and World respectively

## Architecture

Singleplayer: `solarium-ui-vk` spawns `solarium-server` as a child process then connects
via localhost TCP. There is no in-process shortcut.

## Build & Run

Do not use -j, the compile load may crash the machine
```bash
make game                         # singleplayer via build-perf/  (spawns server, --skip-menu)
make game GAME_PORT=7890          # singleplayer on a fixed port
make server PORT=7777             # dedicated server (interactive world select)
make client HOST=X PORT=N         # GUI client → menu or pre-filled join
make stop                         # kill all solarium-* processes (exact-name match)
make test_e2e                     # headless gameplay + pathfinding tests
make proxy                        # HTTP→TCP action proxy w/ Swagger UI on :8088

# Manual cmake (matches `make build`):
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j1
```

## Iterative Development

**Don't `pkill -f solarium`** — it matches the shell's own command line (which
contains the word "solarium") and SIGTERMs your own bash, silently skipping the
rest of the chained command. Use exact-name matching:
`pgrep -x solarium-ui-vk | xargs kill` (and the same for `solarium-server`).

**Debug modes, CLI flags, perf knobs, log file map → `.claude/skills/testing-plan/SKILL.md`.**
That skill is the single source of truth for `--debug-behavior`, `--log-only`,
`--sim-speed`, `--villagers`, `--template`, `make debug_villager`,
`make character_views`, `make item_views`, `make animation_sweep`,
`make perf_fps`, `make perf_server`, and screenshots (F2 /
`/tmp/solarium_screenshot_request`). Use screenshots only when the bug is
visual (rendering, animation, UI); otherwise prefer log-only.

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
  test_e2e.cpp          Headless gameplay tests → solarium-test (uses TestServer)
  test_pathfinding.cpp  Pathfinding regression  → solarium-test-pathfinding
  behavior_scenario_validation/  Scenario-driven behavior checks

src/artifacts/          Python game content — hot-loadable, no rebuild needed
  behaviors/  Creature AI: decide() functions
  living/     EntityDef: stats, model, behavior
  items/  blocks/  effects/  actions/  resources/  structures/  models/  annotations/
  worlds/     World templates (village + test_*/perf_stress arenas)
```

## Game identity

**This game is called Solarium.** Players can mod ANYTHING and EVERYTHING. Every
creature, behavior, item, block, world, and effect is defined in Python artifacts MODs

Every feature decision must ask: *"Can a modder override this from Python?"*
If not, it needs to move to an artifact.

## Commit guidelines

- Keep source code comments concise
- Present tense, capital first letter, under 70 chars
- Area prefix: `server:`, `client:`, `agent:`, `logic:`, `net:`, `artifacts:`, `content:`, `repo:`, etc.
