# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

**Read `docs/PYTHON_EVERYTHING.md` before making ANY gameplay changes.**

## Mandatory Design Rules

These rules are NON-NEGOTIABLE. Every code change must respect them.

### Rule 1: Python Is the Game, C++ Is the Engine

**All game content and gameplay rules are defined in Python. C++ only provides
the engine (physics, networking, rendering).** See `docs/PYTHON_EVERYTHING.md`.

- Creature definitions (stats, collision, model) → Python artifacts
- Behaviors (AI decision logic) → Python artifacts
- Block definitions (properties, drops, sounds) → Python artifacts
- Items (visual, stats, effects) → Python artifacts
- Starting inventory → Python config or creature definition
- **NEVER hardcode gameplay constants in C++.** If you add a magic number
  (distance, speed, timer, radius), it MUST be configurable from Python.

### Rule 2: The Player Is Not Special

The player character (`base:player`) is just another creature. No special
C++ classes, no separate rendering path, no hardcoded stats.

- Same EntityDef as pig/chicken/dog
- Same rendering path (model lookup from EntityDef.model)
- Same physics (moveAndCollide with entity collision box)
- Same behavior dispatch (BehaviorId → Python decide())
- The ONLY difference: input source (WASD vs Python) and camera tracking
- **NEVER add code that checks `EntityType::Player` for gameplay logic.**
  Use property-based checks (`max_hp > 0`, `BehaviorId`, `has_inventory`).

### Rule 3: Server-Authoritative World Ownership

**The server is the sole owner and modifier of world state.**

- Entities submit `ActionProposal` (intent). Server validates and executes.
- Client NEVER writes to `entity.position`, `entity.velocity`, `chunk.set()`
- Camera state is client-only. Server doesn't know about cameras.
- In multiplayer, all state goes through TCP. In singleplayer, same code
  paths but in-process.

### Rule 4: All Intelligence Runs on the Client

**The server has ZERO intelligence.** All AI, pathfinding, and decision-making
runs on the client side.

- Client runs `decide()` for entities it controls
- Client sends `ActionProposal` with coordinates (no logic)
- Server validates (in range? collision? breakable?) and executes
- Python behavior code NEVER runs on the server

## Project Overview

AgentWorld is a voxel game where the world is code. Players write Python to
define new objects and actions, then upload them into a shared world.
C++ server + C++ client, with Python hot-loading.

## Multiplayer Architecture

- **One server** owns world state (chunks, entities, physics)
- **Multiple clients** connect (real players or AI agents)
- **Singleplayer** runs server + client in one process
- **Dedicated server** runs headless for multiplayer hosting
- ESC opens game menu overlay (Minecraft/DST style, no pause)
- Only admin can pause the server (not regular clients)

### Camera Modes

1. **FPS** — First-person, cursor captured, left=break, right=inspect
2. **TPS** — Third-person orbit, Minecraft-style Y-axis
3. **RPG** — Isometric, click-to-move, right-drag=orbit
4. **RTS** — Top-down, box-select, grid formation move orders
5. **Auto-Pilot** — Python behavior decides

All modes produce identical ActionProposals. Any entity can be controlled
any way, at any time.

### Network Protocol

- Binary TCP, 8-byte header (type + payload length)
- `C_ACTION`, `C_SLOT`, `C_HELLO` (client → server)
- `S_WELCOME`, `S_ENTITY`, `S_CHUNK`, `S_TIME`, `S_BLOCK`, `S_INVENTORY` (server → client)
- Entity broadcasts at 20 Hz, physics at 60 TPS
- Client interpolates all entities (no client-side physics)

## Build Commands

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

make game                  # singleplayer
make game 7890             # LAN debug (server + client, skips menu)
make server                # dedicated server
make client                # network client → localhost:7777
make stop                  # kill all agentworld processes
```

## Running

```bash
./build/agentworld                              # singleplayer with menu
./build/agentworld --skip-menu                  # skip menu, start village world
./build/agentworld --skip-menu --demo           # auto-screenshot tour

./build/agentworld-server --port 7777           # dedicated server
./build/agentworld-client --host 127.0.0.1 --port 7777  # network client
```

## Code Style

- Tabs for indentation in C++ and GLSL
- `namespace agentworld { }` wraps all code
- String IDs use `"base:name"` format
- Header-only for small classes, .h+.cpp split for larger ones

## Source Structure

```
src/
  main.cpp                  Singleplayer (server + client in one process)
  main_server.cpp           Dedicated headless server
  main_client.cpp           Network client

  shared/                   Linked by BOTH server and client
    types.h                 ChunkPos, CHUNK_SIZE, BlockId
    constants.h             All string IDs (blocks, entities, items, props)
    block_registry.h        BlockDef, BlockRegistry
    entity.h                EntityDef + Entity class (property-bag)
    inventory.h             Counter-based inventory
    action.h                ActionProposal, ActionQueue
    physics.h               moveAndCollide (shared collision)
    chunk.h                 Chunk: 16x16x16 block storage
    box_model.h             BodyPart, BoxModel, AnimState (pure data)
    net_protocol.h          Binary message serialization
    net_socket.h            TCP server/client socket wrapper

  server/                   Authoritative world simulation (no OpenGL)
    server.h/.cpp           GameServer: tick loop, client mgmt, action resolver
    world.h                 World: chunk map, block access, terrain gen
    entity_manager.h        EntityManager: spawn, physics, behavior dispatch
    behavior.h              Behavior interface types
    behavior_store.h        Load/save behavior .py files
    python_bridge.h/.cpp    pybind11 bridge
    noise.h                 Terrain noise generator
    world_template.h        FlatWorld, VillageWorld templates

  content/                  C++ fallback definitions (Python overrides these)
    blocks_*.h              Block type definitions
    entities_*.h            Entity type definitions (player, animals, items)
    models.h                Box models (C++ fallback when Python not loaded)
    builtin.h/.cpp          registerAllContent()

  client/                   Rendering + input (OpenGL, no world mutation)
    window.h/.cpp           GLFW window
    camera.h/.cpp           4 camera modes, smooth tracking
    renderer.h/.cpp         Terrain + sky + highlight + crack overlay
    chunk_mesher.h/.cpp     Greedy meshing, ambient occlusion
    model.h/.cpp            Box model renderer
    particles.h/.cpp        GPU particle system
    text.h/.cpp             Bitmap font + SDF + drawArc
    audio.h/.cpp            OpenAL audio engine
    ui.h/.cpp               ImGui integration (Roboto font)
    controls.h/.cpp         Keybindings, input mapping
    raycast.h               Block raycast (DDA)
    entity_raycast.h        Entity raycast (ray-AABB)

  game/                     Game loop + UI (client-side)
    types.h                 GameState, MenuAction
    game.h/.cpp             Client loop: owns ServerInterface + renders
    gameplay.h              GameplayController
    gameplay.cpp            update() + handleCameraInput()
    gameplay_movement.cpp   processMovement() (WASD, click-to-move, RTS)
    gameplay_interaction.cpp processBlockInteraction() (break/place)
    hud.h/.cpp              DST-style circular gauges, clock, debug overlay
    imgui_menu.h            Main menu (Play, Handbook, Configurables, Settings)
    code_editor.h/.cpp      In-game Python behavior editor
```

### Dependency Rules
- `shared/` depends on nothing (pure data types, no OpenGL)
- `server/` depends on `shared/` + `content/` (no OpenGL)
- `content/` depends on `shared/` only
- `client/` depends on `shared/` + `content/` (never imports `server/`)
- `game/` depends on `shared/` + `client/` + `server/` (singleplayer only)

### Key Design Docs
- `docs/PYTHON_EVERYTHING.md` — **Python-everything architecture, hardcoded audit, migration path**
- `DEBUGGING.md` — **Iterative dev loop, --skip-menu, auto-screenshot pipeline**

## Commit Guidelines

- Present tense, capital first letter
- First line: compact summary under 70 characters
- Prefix with area: `server:`, `client:`, `shared:`, `content:`, etc.
