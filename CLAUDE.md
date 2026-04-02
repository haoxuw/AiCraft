# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

AgentWorld is a voxel game where the world is code. Players write Python to define new objects and actions, then upload them into a shared world. C++ server + C++ client, with Python hot-loading planned.

## Multiplayer Architecture

AgentWorld uses a **server-authoritative** model:
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

### Key Design Choice: ALL Intelligence Runs on the Client

**The server has ZERO intelligence. All AI, pathfinding, and decision-making
runs on the client side.**

Why: A single server may host hundreds of players and NPCs. If the server ran
AI behaviors for every NPC, it would become the bottleneck. Instead:

- **Client runs behaviors** — each client executes `decide()` for the entities
  it controls (its player character + any NPCs it owns).
- **Client reads the world** — pathfinding, block scanning, entity targeting
  all use the client's local world cache (received from server).
- **Client sends intents** — the result is an ActionProposal: "move to (x,y,z)"
  or "break block at (x,y,z)". Just coordinates, no logic.
- **Server validates** — "can you move there?" (collision check), "can you break
  that?" (in range? block exists? breakable?). Simple yes/no checks.
- **Server executes** — applies physics, modifies chunks, broadcasts results.

This means:
- Server CPU scales with world size, not with NPC count.
- Adding 100 NPCs = adding 100 headless AI clients, NOT loading the server.
- Python behavior code NEVER runs on the server.
- The server never imports pybind11 for behavior execution (only for artifact validation).

```
Client (runs Python behaviors)          Server (dumb validator)
┌─────────────────────────┐            ┌──────────────────────────┐
│ behavior.decide()       │            │                          │
│   → reads world cache   │            │ Receives ActionProposal  │
│   → finds nearest tree  │  ───────►  │   "break block (5,3,7)" │
│   → returns BreakBlock  │            │ Validates: in range? yes │
│                         │            │ Executes: remove block   │
│ behavior.decide()       │            │ Broadcasts: chunk update │
│   → finds player nearby │  ───────►  │   "move to (10,0,15)"   │
│   → returns Follow      │            │ Validates: path clear?   │
│                         │            │ Executes: apply physics  │
└─────────────────────────┘            └──────────────────────────┘
```

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
All built-in content ships as Python files in the artifact store. Players can view,
fork, and modify ANY built-in definition from the in-game editor.

- **Creatures** (pig, chicken, dog, villager) — Python defines stats, collision,
  walk speed, default behavior, model reference. `python/agentworld/creatures/*.py`
- **Behaviors** (wander, peck, follow, prowl, woodcutter) — Python `decide(self, world)`
  runs on server, returns actions. `artifacts/behaviors/base/*.py`
- **Items** (jetpack, tools, food) — Python defines visual pieces, particle emitters,
  active effects. C++ renders generically from definitions.
- **Actions** (mine, place, attack) — Python `validate()` + `execute()` with WorldView.
- **Blocks** (TNT, wheat, wire) — Python ObjectMeta defines properties.

**Artifact store structure:**
```
artifacts/
  behaviors/
    base/          ← built-in (wander.py, peck.py, follow.py, prowl.py, woodcutter.py)
    player/        ← player-modified (forkable, saveable)
  creatures/
    base/          ← built-in (pig.py, chicken.py, dog.py, villager.py)
    player/        ← player-created creatures
```

**Data flow:** Server sets entity props (e.g., `"jetpack_active" = 1`). Client reads
props and looks up visual definitions. Server knows nothing about rendering; client
knows nothing about physics. They communicate through entity properties.

**Artifact upload:** Client creates/edits Python code → sends to server → server
validates (AST scan, sandbox) → registers globally → other players see it live.

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
make game                  # singleplayer (server + client in one process)
make game 7890             # LAN debug: server + client on port 7890, skips menu
make play                  # LAN debug on default port (7777)
make server                # dedicated server (interactive world select)
make server 7890           # dedicated server on port 7890
make client                # network client → localhost:7777
make client 7890           # network client → localhost:7890
make stop                  # kill all agentworld processes
make build                 # configure + build (all three executables)
make clean                 # remove build directory
```

Dependencies (GLFW, GLM, GLAD, pybind11) fetched automatically via CMake FetchContent.

### Web build (Emscripten → WASM + WebGL)
```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -B build-web -DAGENTWORLD_TARGET=web
cmake --build build-web -j$(nproc)
# Outputs: agentworld.html, agentworld.js, agentworld.wasm, agentworld.data
```
See `18_WEB_CLIENT.md` for full design.

## Running

```bash
# Singleplayer
./build/agentworld                          # full menu experience

# LAN debug (quick test — server + client in one command)
make game 7890                              # starts server bg, client joins, skips menu

# Multiplayer (separate terminals)
./build/agentworld-server --port 7777       # Terminal 1: dedicated server
./build/agentworld-client --host 127.0.0.1 --port 7777  # Terminal 2: player 1
./build/agentworld-client --host 127.0.0.1 --port 7777  # Terminal 3: player 2

# Server world management
./build/agentworld-server                   # interactive: pick saved world or create new
./build/agentworld-server --world saves/my_village  # load specific world
./build/agentworld-server --template 1 --seed 42    # create new village world
```

## Code Style

- Tabs for indentation in C++ and GLSL
- `namespace agentworld { }` wraps all code
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
- `19_OBJECT_MODEL.md` -- **Object hierarchy, Python-everything architecture, all game concepts**
- `21_BEHAVIOR_API.md` -- **Behavior API reference: self, world, actions, examples**
- `00_OVERVIEW.md` -- Project vision, the three abstractions (Object/Action/World)
- `09_CORE_LOOP.md` -- 3-phase step loop (Resolve/Render/Gather)
- `14_ARCHITECTURE_DIAGRAM.md` -- Layer separation rules
- `15_BLOCK_AND_ENTITY_MODEL.md` -- Block storage (compact grid + sparse state)
- `13_MILESTONE_1.md` -- Current milestone deliverables
- `17_RESOURCE_GUIDE.md` -- Open-source textures, sounds, models, rendering references
- `18_WEB_CLIENT.md` -- **Web browser client: dual-target build (native + WASM/WebGL)**

## Commit Guidelines

- Present tense, capital first letter
- First line: compact summary under 70 characters
- Prefix with area: `server:`, `client:`, `shared:`, `content:`, etc.
