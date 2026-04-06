# ModCraft — Architecture Overview

A voxel game where the world is code. Players write Python to define new
objects and actions, then upload them into a shared world.

## Core Principle

**Python is the game. C++ is the engine.**

All game content — creatures, behaviors, items, blocks, effects, models,
worlds — is defined in Python artifacts. C++ provides physics, rendering,
networking, and collision. Nothing gameplay-specific lives in C++.

```
Python (the game)                    C++ (the engine)
  Creatures, Behaviors, Items    -->   Physics (moveAndCollide, 60 Hz)
  Blocks, Effects, Models            Network (TCP, binary protocol)
  World templates, Actions            Rendering (OpenGL, chunk meshing)
  Starting inventory, Sounds          Collision (AABB, raycast)
                                      Audio (OpenAL), Input (GLFW)
```

## Server + Player Client + Agent Client

Three separate process types. Same architecture in singleplayer and multiplayer.

### Server (`modcraft-server`)

Headless. **NO Python, NO OpenGL.** Owns all world state.

- Validates `ActionProposal`s from all clients
- Runs physics at 50 Hz, broadcasts entity state at 20 Hz
- Tracks entity ownership (which client controls which entity)
- **Auto-spawns agent client processes** for NPC entities (via `ClientManager`)
- Perception-scoped: each client only receives state within 64 blocks

### Player Client (`modcraft`)

GUI. **OpenGL, NO Python.**

- Renders world, handles WASD/mouse/camera input
- Sends ActionProposals for the player entity
- Connects to server via TCP (localhost in singleplayer, remote in MP)
- In singleplayer: `AgentManager` launches the server process first

### Agent Client (`modcraft-agent`)

Headless. **Python + pybind11, NO OpenGL.**

- One process per NPC entity (avoids Python GIL, full parallelism)
- Runs `decide(self, world)` at 4 Hz, sends ActionProposals
- Receives perception-scoped entity/chunk state from server
- **Spawned and managed by the server** — not by the player client

### Singleplayer Flow

```
1. Player clicks "Play"
2. AgentManager (GUI) fork+execs modcraft-server on localhost
3. GUI connects to server as a regular player client
4. Server's ClientManager detects NPC entities with BehaviorId
5. Server fork+execs one modcraft-agent per NPC entity
6. Each agent connects back to server, receives S_ASSIGN_ENTITY
7. Agents run Python AI, entities start moving
```

Multiplayer is identical — server is just on a different machine.

## Entity Ownership

Every living entity is controlled by exactly one client:

| Client Type | Controls | Identified By |
|-------------|----------|---------------|
| Player client | Its player entity | `C_HELLO` on connect |
| Agent client | One NPC entity | `C_AGENT_HELLO` + entity ID |

Server validates: only the owning client can act on its entity.
If an agent crashes, server restarts it — entities recover in seconds.

## The Player Is Not Special

`base:player` is just another creature with the same EntityDef, rendering,
physics, and behavior dispatch as pig/chicken/dog. The ONLY differences:

1. Input source: WASD/mouse instead of Python `decide()`
2. Camera follows it (client-side only)
3. Spawned when a player client connects

Any entity can be "possessed" (human-controlled) or run on auto-pilot.

## Artifact System

Artifacts are Python files that define **all game content**. They live in
`artifacts/` with `base/` (built-in) and `player/` (user-created) subdirs.

### Categories

| Category | What It Defines | Example |
|----------|-----------------|---------|
| `creatures/` | Stats, collision, model ref, behavior | pig.py, villager.py |
| `behaviors/` | AI logic: `decide(self, world)` → action | wander.py, prowl.py |
| `items/` | Weapons, tools, consumables | sword.py, potion.py |
| `blocks/` | Hardness, color, drops, transparency | terrain.py |
| `models/` | Box model geometry + animations | pig.py, sword.py |
| `effects/` | Combat/status effects | damage.py, poison.py |
| `characters/` | Player skins + appearance | knight.py, mage.py |
| `worlds/` | World gen: terrain, villages, mobs | village.py |
| `actions/` | Custom action types | *(planned)* |
| `resources/` | Audio/visual metadata | creature_sounds.py |

### Forking & Sharing

- Fork: `ArtifactRegistry::forkEntry()` copies `base/pig.py` → `player/my_pig.py`
- Namespace rewritten: `"base:pig"` → `"player_abc:pig"`
- Upload custom creatures, behaviors, items, models to share with others
- Each artifact is self-contained — one Python file, standard dict format

### Behavior Hot-Swap

1. Inspect entity → [E] opens code editor
2. Edit Python `decide()` function → Apply
3. `C_RELOAD_BEHAVIOR` → server → agent client reloads Python instantly
4. Entity uses new behavior on next tick — no restart needed

### Artifact Format

**Creature** (`artifacts/creatures/base/pig.py`):
```python
creature = {
    "id": "base:pig", "name": "Pig", "behavior": "wander",
    "model": "pig", "walk_speed": 2.0, "max_hp": 10,
    "collision": {"min": [-0.4, 0, -0.4], "max": [0.4, 0.9, 0.4]},
}
```

**Behavior** (`artifacts/behaviors/base/wander.py`):
```python
from modcraft_engine import Idle, Wander, Flee
def decide(self, world):
    for e in world["nearby"]:
        if e["category"] == "player" and e["distance"] < 5:
            return Flee(e["id"], speed=self["walk_speed"] * 1.8)
    return Wander(speed=self["walk_speed"])
```

**Item** (`artifacts/items/base/sword.py`):
```python
item = {
    "id": "base:sword", "name": "Sword", "category": "weapon",
    "damage": 5, "range": 3.0, "on_use": "attack",
    "model": "sword", "color": [0.7, 0.7, 0.75],
}
```

## Network Protocol

Binary TCP, 8-byte header `[type:u32][length:u32]`.

| Dir | Type | Purpose |
|-----|------|---------|
| C→S | `C_ACTION` | ActionProposal (move, break, place, attack) |
| C→S | `C_HELLO` | Player client ID |
| C→S | `C_AGENT_HELLO` | Agent client ID + target entity |
| C→S | `C_RELOAD_BEHAVIOR` | Hot-swap behavior source code |
| S→C | `S_WELCOME` | Assigned entity + spawn pos |
| S→C | `S_ENTITY` | Entity state (perception-scoped) |
| S→C | `S_CHUNK` | 16x16x16 blocks |
| S→C | `S_ASSIGN_ENTITY` | Assign entity to agent |
| S→C | `S_REVOKE_ENTITY` | Revoke control (death) |
| S→C | `S_RELOAD_BEHAVIOR` | Forward new Python to agent |

## What Stays in C++

| System | Why |
|--------|-----|
| Physics (`moveAndCollide`) | 60 Hz, all entities |
| Network protocol | Binary TCP serialization |
| Rendering (OpenGL) | Chunks, models, fog of war |
| Collision (AABB, raycast) | Performance-critical |
| Audio (OpenAL) | Spatial audio |
| Chunk storage | 16x16x16 arrays |

## Source Layout

```
src/
  shared/           Linked by ALL (no OpenGL, no Python)
  server/           Authoritative simulation (server.h, client_manager.h, entity_manager.h)
  agent/            Agent client (agent_client.h, behavior_executor.h, python_bridge)
  client/           Rendering + input (renderer, camera, fog_of_war, audio)
  game/             Player client game loop (gameplay, hud, menu, code_editor)
  content/          C++ fallback definitions
artifacts/          Python game content (hot-loadable)
```

| Binary | Links | Purpose |
|--------|-------|---------|
| `modcraft` | shared + client + game + OpenGL | Player client (GUI) |
| `modcraft-server` | shared + server + content + Python | Server + agent spawner |
| `modcraft-agent` | shared + agent + content + Python | Agent client (headless) |

## Build & Run

```bash
make game        # singleplayer (server + agents auto-launched)
make server      # dedicated server (agents auto-spawned)
make client      # player client → localhost:7777
make stop        # kill all processes
```
