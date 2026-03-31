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

### Network Protocol (future)
- Binary packet-based over TCP/UDP
- `TOSERVER_*` packets: player input, block interaction, chat
- `TOCLIENT_*` packets: world state updates, entity sync, chunk data
- Server validates all actions (anti-cheat by design)
- Client-side prediction for smooth movement

## Build Commands

```bash
# Configure (only needed once, or after CMakeLists.txt changes)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j$(nproc)

# Or use the Makefile shortcuts:
make build    # configure + build
make game     # build + run
make clean    # remove build directory
```

Dependencies (GLFW, GLM, GLAD) are fetched automatically via CMake FetchContent.

## Running

```bash
./build/aicraft              # singleplayer (menu screen)
./build/aicraft --demo       # automated screenshot mode (cycles camera views)
# Future:
# ./build/aicraft-server     # dedicated multiplayer server
# ./build/aicraft-agent      # headless AI agent client
```

## Code Style

- Tabs for indentation in C++ and GLSL
- `namespace aicraft { }` wraps all code
- String IDs use `"base:name"` format (defined in `src/shared/constants.h`)
- Header-only for small classes, .h+.cpp split for larger ones

## Source Structure

```
src/
  main.cpp                  Singleplayer entry point (server + client in-process)
  main_server.cpp           Dedicated server entry point (future)
  main_agent.cpp            AI agent client entry point (future)

  shared/                   Linked by server, client, AND agent
    types.h                 ChunkPos, CHUNK_SIZE, face directions
    constants.h             All string IDs (blocks, entities, props, groups)
    block_def.h             BlockDef, BlockBehavior, BlockRegistry
    entity_def.h            EntityDef
    inventory.h             ItemStack, Inventory
    protocol.h              Network packet types (future)

  server/                   Authoritative world simulation
    server.h/.cpp           Server class: tick loop, client management (future)
    world.h                 World: chunk map, block access, mutex
    chunk.h                 Chunk: 16x16x16 block storage
    entity.h                Entity: property-bag pattern
    entity_manager.h/.cpp   Spawn, despawn, step, wander AI
    physics.h               collideAndSlide, AABB collision, unstuck
    active_blocks.h         Block ticking (TNT fuse, wheat growth, wire/NAND)
    terrain/
      noise.h               Hash-based value noise, terrain height
      world_template.h      WorldTemplate base class
      flat_world.h          FlatWorldTemplate
      village_world.h       VillageWorldTemplate

  content/                  Data-driven content definitions
    blocks_terrain.h        Stone, Dirt, Grass, Sand, Water, Snow, Cobblestone
    blocks_plants.h         Wood, Leaves
    blocks_active.h         TNT, Wheat, Wire, NAND Gate
    entities_animals.h      Pig, Chicken
    entities_player.h       Player entity definition
    entities_items.h        Item entity definition
    models.h                Box models (player, pig, chicken)
    characters.h            Character skins and body parts
    register.h/.cpp         registerAllContent() entry point

  client/                   Rendering + input + game UI
    render/
      window.h/.cpp         GLFW window, OpenGL context
      shader.h/.cpp         GLSL compile/link, uniform setters
      camera.h/.cpp         4 camera modes, view/projection matrices
      renderer.h/.cpp       Render pipeline: sky, terrain, frustum cull
      chunk_mesher.h/.cpp   Greedy meshing, ambient occlusion
      model_renderer.h/.cpp Box model animation and rendering
      particles.h/.cpp      GPU particle system
      text.h/.cpp           Bitmap font renderer
      raycast.h             DDA voxel raycast
    input/
      controls.h/.cpp       Keybindings, YAML config, action mapping
    game/
      types.h               GameState, MenuAction, HOTBAR_SIZE
      game.h/.cpp           Client game loop, subsystem ownership
      player.h              Client-side player state and prediction
      gameplay.h/.cpp       Input processing, block interaction
      menu.h/.cpp           Menu screens, template selection
      hud.h/.cpp            Hotbar, health bars, debug overlay
```

### Dependency Rules
- `shared/` depends on nothing (pure data types)
- `server/` depends on `shared/` + `content/`
- `content/` depends on `shared/` only
- `client/` depends on `shared/` + `content/` (never imports `server/`)
- In singleplayer, `main.cpp` wires server + client together via direct calls
- In multiplayer, server and client communicate only through `shared/protocol.h`

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
