# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

**Read `docs/00_OVERVIEW.md` before making ANY gameplay changes.**

## Mandatory Design Rules

These rules are NON-NEGOTIABLE. Every code change must respect them.

### Rule 1: Python Is the Game, C++ Is the Engine

**All game content and gameplay rules are defined in Python. C++ only provides
the engine (physics, networking, rendering).** See `docs/00_OVERVIEW.md`.

- Creature definitions, behaviors, items, blocks, effects, models → Python artifacts
- **NEVER hardcode gameplay constants in C++.** If you add a magic number
  (distance, speed, timer, radius), it MUST be configurable from Python.

### Rule 2: The Player Is Not Special

The player character (`base:player`) is just another creature. No special
C++ classes, no separate rendering path, no hardcoded stats.

- Same EntityDef, rendering, physics, behavior dispatch as pig/chicken/dog
- The ONLY difference: input source (WASD vs Python) and camera tracking
- **NEVER add code that checks `EntityType::Player` for gameplay logic.**

### Rule 3: Server-Authoritative World Ownership

**The server is the sole owner and modifier of world state.**

- Entities submit `ActionProposal` (intent). Server validates and executes.
- Client NEVER writes to `entity.position`, `entity.velocity`, `chunk.set()`
- Singleplayer uses the same TCP code paths as multiplayer.

### Rule 4: All Intelligence Runs on Agent Clients

**The server has ZERO intelligence.** All AI runs on agent client processes.

- Each NPC entity has its own `agentworld-bot` process running Python `decide()`
- Server spawns/manages agent clients via `ClientManager`
- Python behavior code NEVER runs on the server

## Architecture Summary

Three process types (same in singleplayer and multiplayer):

- **Server** (`agentworld-server`) — headless, owns world, NO Python/OpenGL
- **Player Client** (`agentworld`) — GUI, renders world, NO Python
- **Agent Client** (`agentworld-bot`) — headless, runs Python AI, NO OpenGL

Singleplayer: GUI launches server → server auto-spawns agent clients for NPCs.
See `docs/00_OVERVIEW.md` for full architecture, protocol, and artifact system.

## Project Overview

AgentWorld is a voxel game where the world is code. Players write Python to
define new objects and actions, then upload them into a shared world.

## Build Commands

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

make game                  # singleplayer (server + agents auto-launched)
make play                  # singleplayer on fixed port (friends can join)
make server                # dedicated server (auto-spawns agent clients)
make client                # player client → localhost:7777
make stop                  # kill all agentworld processes
```

## Running

```bash
./build/agentworld                              # singleplayer with menu
./build/agentworld --skip-menu                  # skip menu, start village world

./build/agentworld-server --port 7777           # dedicated server
./build/agentworld-client --host 127.0.0.1 --port 7777  # player client
./build/agentworld-bot --host 127.0.0.1 --port 7777 --entity 5  # agent client
```

## Code Style

- Tabs for indentation in C++ and GLSL
- `namespace agentworld { }` wraps all code
- String IDs use `"base:name"` format
- Header-only for small classes, .h+.cpp split for larger ones

## Source Structure

```
src/
  main.cpp                  Player client (GUI, spawns server via AgentManager)
  main_server.cpp           Dedicated server (spawns agents via ClientManager)
  main_client.cpp           Network player client

  shared/                   Linked by ALL (no OpenGL, no Python)
  server/                   Authoritative simulation (no OpenGL)
    server.h                GameServer: tick, actions, entity ownership
    client_manager.h        ClientManager: TCP, agent spawning, broadcast
    entity_manager.h        EntityManager: spawn, physics (no AI)
  bot/                      Agent client (Python, no OpenGL)
    bot_client.h            BotClient: TCP, Python decide(), send actions
    behavior_executor.h     BehaviorAction → ActionProposal
  client/                   Rendering + input (OpenGL, no Python)
  game/                     Player client game loop + UI
    process_manager.h       AgentManager: spawn server for singleplayer
  content/                  C++ fallback definitions

artifacts/                  Python game content (hot-loadable)
  creatures/ behaviors/ items/ blocks/ models/ effects/
  characters/ worlds/ actions/ resources/
```

### Dependency Rules
- `shared/` depends on nothing (pure data types)
- `server/` depends on `shared/` + `content/` (no OpenGL, no Python)
- `bot/` depends on `shared/` + `server/behavior.h` + Python
- `client/` depends on `shared/` + `content/` (no Python, no server/)
- `game/` depends on `shared/` + `client/`

### Key Docs
- `docs/00_OVERVIEW.md` — **Full architecture, artifact system, protocol**
- `DEBUGGING.md` — **Iterative dev loop, --skip-menu, auto-screenshot**

## Commit Guidelines

- Present tense, capital first letter
- First line: compact summary under 70 characters
- Prefix with area: `server:`, `client:`, `shared:`, `bot:`, `content:`, etc.
