# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

AiCraft is a voxel game where the world is code. Players write Python to define new objects and actions, then upload them into a shared world. C++ server + C++ client, with Python hot-loading planned.

## Multiplayer Architecture

AiCraft uses a **server-authoritative** model:
- **One server** owns world state (chunks, entities, physics, game rules)
- **Multiple clients** connect to the server (real players or AI agents)
- **Singleplayer** runs server + client in the same process
- **Dedicated server** runs headless for multiplayer hosting

### Client Types
- **Real player client** -- keyboard/mouse input, OpenGL rendering, connects locally or over internet
- **AI agent client** -- headless, Python-driven, connects via same network protocol as real players. Uses `observe()` to read world state and `decide()` to send actions

### Server-Authoritative World Ownership

**The server is the sole owner and modifier of world state.**

- Objects NEVER directly set their own position. They submit a MovementIntent
  (where they want to go), and the server validates it through physics.
- All world mutations (block break/place, entity spawn/damage) go through an
  ActionProposal queue. The server validates and executes each proposal.
- Direction/yaw is local-only (client-side, for animation). It is NOT
  server-authoritative.
- The player is an Entity like any other. There is no special Player class.
  Player input produces the same ActionProposals as AI behavior code.
- In singleplayer, server and client run in the same process. The action
  queue is filled and drained within a single frame.
- In multiplayer, ActionProposals are serialized as TOSERVER_* packets.
  The server broadcasts results as TOCLIENT_* packets.

### Three Modes of Control

Any character can be controlled in three interchangeable ways:

1. **First-Person (FPS)** -- You ARE the character. WASD/mouse input produces
   ActionProposals directly. Camera shows their eyes.
2. **RTS (Warcraft-like)** -- You COMMAND characters. Click to assign orders
   (move here, attack that, build this). They execute over time.
3. **Python Auto-Pilot** -- Behavior code decides. The decide() function
   reads the world and returns ActionProposals autonomously.

All three produce identical ActionProposals. The server doesn't know or care
which mode generated them. You can switch modes at any time. In multiplayer,
one player can control some characters in FPS, others in RTS, others on auto-pilot.

"Possessing" a character = switching it to first-person mode.
An NPC with no player controlling it = auto-pilot by default.

This design ensures:
1. Anti-cheat by design (server validates everything)
2. All entities use identical code paths (player = pig = NPC)
3. Python behavior code cannot cheat (it only proposes, never mutates)
4. Multiplayer is a natural extension, not a rewrite
5. Any character can be controlled any way, at any time

### Client-Server Boundary Rules (anti-cheat)

**Client code (src/game/, src/client/) MUST NEVER directly modify entity state.**

Specifically, client code must not:
- Write to `entity.position` or `entity.velocity`
- Call `entity.setProp()` for physics-relevant properties (hp, fly_mode, cooldowns)
- Call `chunk->set()` to modify blocks
- Call `entity.onGround = ...`

Client code CAN:
- Read any entity property (for rendering, HUD, raycast)
- Write to `camera.player.*` (client-side mirror, animation only)
- Write to `entity.yaw` (local animation only, not server-authoritative)
- Push `ActionProposal` to `world.actions` (the ONLY way to request changes)
- Write to `entity.setProp(Prop::SelectedSlot, ...)` (UI-only, does not affect physics)

All state changes go through `resolveActions()` which runs server-side.
In singleplayer, "server-side" means the same process but a different phase
of the game loop. In multiplayer, it means a different machine entirely.

### Python-Everything Architecture

**No game content should be C++ source code.** Everything is Python data that C++ loads.

- **Items** (jetpack, tools, food) — Python defines visual pieces, particle emitters,
  active effects. C++ renders generically from the definitions.
- **Behaviors** (pig wander, chicken peck) — Python `decide()` runs on server, returns
  ActionProposals. C++ executes them through physics.
- **Actions** (mine, place, attack) — Python `validate()` + `execute()` with WorldView.
  Server validates all actions before mutation.
- **Blocks** (TNT, wheat, wire) — Python ObjectMeta defines properties. Active blocks
  have Python `decide()` for ticking behavior.

**Data flow:** Server sets entity props (e.g., `"jetpack_active" = 1`). Client reads
props and looks up visual definitions. Server knows nothing about flames; client knows
nothing about thrust physics. They communicate through entity properties.

**Artifact upload:** Client creates Python code → sends to server → server validates
(AST scan, sandbox) → registers globally → other players see it live.

### Network Protocol (implemented)
- Binary TCP with 8-byte header (type + payload length)
- `TOSERVER_*` packets: `C_ACTION` (serialized ActionProposal), `C_SLOT` (hotbar)
- `TOCLIENT_*` packets: `S_WELCOME` (player ID), `S_ENTITY` (position/velocity/goal),
  `S_CHUNK` (16x16x16 block data), `S_TIME` (world time), `S_BLOCK` (single change)
- Server validates all actions (anti-cheat by design)
- Entity broadcasts throttled to 20 Hz (not every tick)

## Build Commands

```bash
# Configure (only needed once, or after CMakeLists.txt changes)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j$(nproc)

# Or use the Makefile shortcuts:
make build    # configure + build (all three executables)
make game     # singleplayer (server + client in one process)
make server   # dedicated server (headless, TCP port 7777)
make client   # network client (connects to localhost:7777)
make clean    # remove build directory
```

Dependencies (GLFW, GLM, GLAD) are fetched automatically via CMake FetchContent.

## Running

```bash
./build/aicraft                          # singleplayer (server + client in one process)
./build/aicraft --demo                   # automated screenshot mode
./build/aicraft-server --port 7777       # dedicated server (headless, TCP)
./build/aicraft-client --host 127.0.0.1 --port 7777  # network client
```

## Code Style

- Tabs for indentation in C++ and GLSL
- `namespace aicraft { }` wraps all code
- String IDs use `"base:name"` format (defined in `src/shared/constants.h`)
- Header-only for small classes, .h+.cpp split for larger ones

## Source Structure

```
src/
  main.cpp                  Singleplayer (server + client in one process)
  main_server.cpp           Dedicated headless server (TCP, port 7777)
  main_client.cpp           Network client (connects to server)

  shared/                   Linked by BOTH server and client
    types.h                 ChunkPos, CHUNK_SIZE, BlockId
    constants.h             All string IDs (blocks, entities, items, props)
    block_registry.h        BlockDef, BlockRegistry
    entity.h                EntityDef + Entity class (property-bag pattern)
    inventory.h             Counter-based inventory
    action.h                ActionProposal, ActionQueue
    physics.h               moveAndCollide (shared collision)
    chunk.h                 Chunk: 16x16x16 block storage
    box_model.h             BodyPart, BoxModel, AnimState (pure data, no GL)
    character.h             CharacterDef, CharacterManager
    net_protocol.h          Binary message serialization
    net_socket.h            TCP server/client socket wrapper

  server/                   Authoritative world simulation (no OpenGL)
    server.h/.cpp           GameServer: tick loop, client mgmt, action resolver
    world.h                 World: chunk map, block access, terrain gen
    entity_manager.h        EntityManager: spawn, physics, behavior dispatch
    behavior.h              AI behavior system (Wander, Peck, Idle)
    python_bridge.h/.cpp    pybind11 bridge (optional, needs python3-dev)
    noise.h                 Terrain noise generator
    world_template.h        FlatWorld, VillageWorld templates

  content/                  Data-driven definitions (shared)
    blocks_*.h              Block type definitions
    entities_*.h            Entity type definitions (pig, chicken, player, item)
    models.h                Box models (player, pig, chicken)
    characters.h            Character skins (Blue Knight, Skeleton, etc.)
    faces.h                 Face overlays
    builtin.h/.cpp          registerAllContent()

  client/                   Rendering + input (OpenGL, no world mutation)
    window.h/.cpp           GLFW window
    camera.h/.cpp           4 camera modes, smooth tracking
    renderer.h/.cpp         Terrain + sky + highlight rendering
    chunk_mesher.h/.cpp     Greedy meshing, ambient occlusion
    model.h/.cpp            Box model renderer (uses shared/box_model.h)
    particles.h/.cpp        GPU particle system
    text.h/.cpp             Bitmap font
    shader.h/.cpp           GLSL compile/link
    controls.h/.cpp         Keybindings, input mapping
    raycast.h               Block raycast (DDA)
    entity_raycast.h        Entity raycast (ray-AABB)
    text_input.h            Multi-line text input (code editor)

  game/                     Game loop + UI (client-side)
    types.h                 GameState, MenuAction
    game.h/.cpp             Client loop: owns GameServer + renders
    gameplay.h/.cpp         Input → ActionProposals (client-only)
    menu.h/.cpp             Menu screens
    hud.h/.cpp              Hotbar, health, entity tooltip
    code_editor.h/.cpp      In-game Python behavior editor
```

### Dependency Rules
- `shared/` depends on nothing (pure data types, no OpenGL)
- `server/` depends on `shared/` + `content/` (no OpenGL)
- `content/` depends on `shared/` only
- `client/` depends on `shared/` + `content/` (never imports `server/` directly)
- `game/` depends on `shared/` + `client/` + `server/` (only in singleplayer mode)
- In singleplayer, `main.cpp` creates GameServer + Game in the same process
- In multiplayer, server and client communicate through TCP via `shared/net_protocol.h`

### Design Docs (read these for context)
- `00_OVERVIEW.md` -- Project vision, the three abstractions (Object/Action/World)
- `09_CORE_LOOP.md` -- 3-phase step loop (Resolve/Render/Gather)
- `14_ARCHITECTURE_DIAGRAM.md` -- Layer separation rules
- `15_BLOCK_AND_ENTITY_MODEL.md` -- Block storage (compact grid + sparse state)
- `13_MILESTONE_1.md` -- Current milestone deliverables
- `17_RESOURCE_GUIDE.md` -- Open-source textures, sounds, models, rendering references

## Commit Guidelines

- Present tense, capital first letter
- First line: compact summary under 70 characters
- Prefix with area: `server:`, `client:`, `shared:`, `content:`, etc.
