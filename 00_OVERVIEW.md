# AiCraft - Overview

A voxel game where the world is code. Players don't just play -- they write Python to define new objects and actions, then upload them into a shared world.

---

## Core Idea

```
+------------------------------------------------------------------+
|                                                                    |
|   "What if every block, creature, spell, and interaction          |
|    in the game was a Python script that players wrote?"           |
|                                                                    |
+------------------------------------------------------------------+
```

The game ships with a base set of objects (dirt, stone, water, trees, pigs, etc.) and actions (mine, place, attack, eat). But players can open an in-game code editor, write new `Object` and `Action` definitions in Python, test them locally, then upload them to the server. Once uploaded, they become real -- other players can interact with them.

---

## Architecture at a Glance

```
+================================================================+
|                         AiCraft                                 |
+================================================================+
|                                                                  |
|   C++ Server (single source of truth)                           |
|   +---------------------------------------------------------+   |
|   |  World State    |  Physics/Tick  |  Python Hot-Loader    |   |
|   |  - All blocks   |  - Step loop   |  - Load .py scripts   |   |
|   |  - All entities |  - Collision    |  - Validate & sandbox |   |
|   |  - All players  |  - Actions     |  - Object registry    |   |
|   |  - Day/weather  |  - ABMs        |  - Action registry    |   |
|   +---------------------------------------------------------+   |
|          ^                    |                                   |
|          |  UDP/TCP           |  State updates                   |
|          |  (protobuf)        v                                   |
|   +---------------------------------------------------------+   |
|   |  Python Client (per player)                              |   |
|   |  - Rendering (OpenGL/Vulkan via moderngl or similar)     |   |
|   |  - Input handling                                        |   |
|   |  - In-game code editor                                   |   |
|   |  - Local preview / testing sandbox                       |   |
|   |  - Object/Action authoring                               |   |
|   +---------------------------------------------------------+   |
|                                                                  |
+================================================================+
```

---

## The Three Abstractions

Everything in AiCraft is built from three primitives:

### 1. Object -- "What exists"

Everything in the world is an Object. A dirt block, a pig, a player, rain, a magic sword.

```
Object
  |
  +-- PassiveObject    (doesn't act on its own)
  |     dirt, stone, wood, leaves, crafting table, chest, ...
  |
  +-- ActiveObject     (has behavior, acts each tick)
        water (flows), fire (spreads), pig (wanders),
        player (controlled), NPC (AI), potion (ticking effect), ...
```

Objects are Pydantic models -- their attributes (stats, weight, texture, etc.) are typed, validated, serializable.

### 2. Action -- "What happens"

An Action is a discrete event that modifies the World. Mining a block, a sheep eating grass, TNT exploding, casting a fireball.

Actions are Python functions decorated with metadata: who/what can trigger them, what they affect, cooldowns, costs, etc.

### 3. World -- "Where everything lives"

The World is the container: a voxel grid of blocks, plus all entities (players, NPCs, mobs, items, particles, weather, time-of-day). The server owns the canonical World state. Clients receive synchronized snapshots.

---

## What Makes This Different

| Traditional modding (Luanti/Minecraft) | AiCraft |
|---------------------------------------|---------|
| Mods written outside the game | Code written inside the game |
| Restart server to load mods | Hot-load while playing |
| Mod authors are separate from players | Every player is a potential creator |
| Static mod packs | Living, evolving content |
| Lua/Java with engine-specific APIs | Standard Python with Pydantic |
| Content reviewed externally | Sandboxed, validated on upload |

---

## Document Index

### Core Concepts (read these first)

| Document | Contents |
|----------|---------|
| [14_ARCHITECTURE_DIAGRAM.md](14_ARCHITECTURE_DIAGRAM.md) | **START HERE.** The 3-layer separation: Game Model / Server / Renderer. |
| [09_CORE_LOOP.md](09_CORE_LOOP.md) | Server/Client vs World/Action/Object separation. The 3-phase step loop. |
| [01_WORLD.md](01_WORLD.md) | World structure, voxel grid, chunks, coordinate system |
| [02_OBJECTS.md](02_OBJECTS.md) | Object model, Passive/Active split, Pydantic schemas, registry |
| [03_ACTIONS.md](03_ACTIONS.md) | Action system, triggers, effects, validation |

### Infrastructure

| Document | Contents |
|----------|---------|
| [04_SERVER.md](04_SERVER.md) | C++ server architecture, Python embedding, hot-loading |
| [05_CLIENT.md](05_CLIENT.md) | Python client, rendering, code editor, preview sandbox |
| [06_NETWORKING.md](06_NETWORKING.md) | Protocol, state sync, delta compression |

### Player Experience

| Document | Contents |
|----------|---------|
| [07_PLAYER_CODING.md](07_PLAYER_CODING.md) | In-game code editor, upload flow, sandboxing, artifact system |
| [08_BUILTIN_CONTENT.md](08_BUILTIN_CONTENT.md) | Base game objects and actions that ship with AiCraft |

### Technical Validation & Milestones

| Document | Contents |
|----------|---------|
| [12_FEASIBILITY.md](12_FEASIBILITY.md) | Proof that C++ + embedded Python + hot-reload works. Precedents, perf, sandbox. |
| [13_MILESTONE_1.md](13_MILESTONE_1.md) | Milestone 1: Playable flat world sandbox. Tech stack, build instructions. |
| [15_BLOCK_AND_ENTITY_MODEL.md](15_BLOCK_AND_ENTITY_MODEL.md) | Block storage (compact grid + sparse state) vs entity storage. |
| [16_ARTIFACT_REGISTRY_TODO.md](16_ARTIFACT_REGISTRY_TODO.md) | TODO: User artifact upload, sandbox, hot-reload pipeline. |

### Reference (NOT our design -- just for learning)

These describe the existing Luanti/Minetest systems. We are **NOT** replicating their approach: no Lua, no hard-coded C++ game functions, no engine-baked node/tool/craft systems. These exist only as prior art to learn from. See [reference/README.md](reference/README.md) for what we take and what we reject.

| Document | Contents |
|----------|---------|
| [reference/README.md](reference/README.md) | What we learn vs what we reject |
| [reference/LUANTI_ENGINE.md](reference/LUANTI_ENGINE.md) | Luanti C++ engine internals |
| [reference/MINETEST_GAME.md](reference/MINETEST_GAME.md) | Minetest Game content spec |
