# CivCraft — Architecture Overview

## Game Design

A voxel sandbox game, where players write Python to MOD new objects and actions, then upload them into a shared world.

At core, the gameplay is for

Kids, new gamers:
> casual fun sandbox builder
> gather resources, refine
> fun mechanics, MODded creatures

Veteran gamers:
> invent creative ways to mass harvesting, while maintaining sustainability
> take the risk gathering resources from the depth
> balance the damanges you delt to the ecosystem and its recovery

Advanced gameplays:
> MOD Everything and Anything
> terraform with thousands of scripted Agents
> build your kindom with your AI Agents -- integrate with LLM
> map real AWS resources / gmail files INTO THE GAME

### No Difficulty Selection

Think of Lost, it's a survival TV show, but weird smoke mounster show up in the first episode. We take the same game start -- a casual sandbox, no presure, no death. Threats and presure comes only when you choose to unlock.

Villagers would kept destroy a (recurring) beacon (inspires curiosity) if you choose to protected it, then threat will come from sky

Like Silent Hill games, To fend off a known thread off, you will need to venture take other risks. Inspires excitment. The loop goes like:

Want to craft new stuff -> needs resources -> repetitive slow gathering -> you want buffs given by the beacon -> threads from sky if you keep the beacon up -> needs defence items -> needs more resources -> needs to destroy the environment -> unleash new threat in the depth -> balance sustainable resource harvesting VS dealing with gameplay threads

We offer gameplay mixing FPS shooter, action RPG and mass army RTS, all in 1 game

Use villager behavior and special strucures to natrually introduce you with new game element, should you choose to accept


### MOD everything

Anything and everything can be MODed, from 3D models, to stats, item effects, even AI behaviors

### Conservation of Objects

Playing game is fun. Playing, by definition needs a challenge.

While clients can use MOD to create any OP items or behavior, we have 1 constrain:

The player can't create items out of nothing -- Conservation of Mass/Energy

Everything the player creates, has to consume material value of a higher sum.

Meanwhile the world can slowly generate/recover energy, by 2 kinds of regeneration: 1. ecosystems regnerating (trees/plants growing); 2. Creatures/NPCs regenerating HP (naturally healing).

### Terraforming

Trees and Living entities are essential to the game. While most type of crafting loss material value, only regeneration of trees and Livng HP can create value. While the core game journey require harvesting the world's resources. (todo) When we regenerate each tree from anchor, we will check for nearby vegitations, if none, we will turn the soil from dirt to desert. Desert would remove any tree anchors and spreads the desert. A desert(sand) can only be reverted back to dirt by placing new tree anchors. Villagers may do that, but it cost X more times the resources from what a tree generates.

You may also ask villagers to convert dirt to material values, and find underground gemstones with high value, but it distroys landscape and unleash monsters, and expose infertile desert(rocky) land.

With thousands of villagers, this creates a dynamic strategy of epic-scale resource harvesting, while balancing sustainable terraforming.


## System Design

Server holds the source of truth of the world. Many clients (human plaer or AI agents) connect to server, submit action proposals.

**Python is the game. C++ is the engine.**

All game content — creatures, behaviors, items, blocks, effects, models,
worlds — is defined in Python artifacts. C++ provides physics, rendering,
networking, and collision. Nothing gameplay-specific lives in C++.

**Material value is conserved.** Every object — blocks, items, entity HP,
dropped items — carries an intrinsic `value`. No action can increase the
world's total value. The server enforces the value invariant.
See `docs/28_MATERIAL_VALUE.md` and `docs/03_ACTIONS.md`.

## Action Types ← Most Important Design Invariant

**The server validates exactly four `ActionProposal` types. This is the
highest-priority architectural rule and overrides any other design decision.**

| # | Type | Semantics |
|---|------|-----------|
| 0 | `TYPE_MOVE` | Set entity velocity / direction. Server checks path is clear (no tunnelling). |
| 1 | `TYPE_RELOCATE` | Move an Object between containers (inventory ↔ ground ↔ entity). Value is conserved — nothing created from nothing. |
| 2 | `TYPE_CONVERT` | Transform an Object from one type to another. Value must not increase. `to_item=""` = destroy (value decreases, always allowed). Used for: breaking blocks, chopping trees, attacking (destroy HP), laying eggs (HP → item), consuming potions (item → HP effect). |
| 3 | `TYPE_INTERACT` | Toggle interactive block state: open/close door, press button, light TNT. |

Everything every entity can ever do compiles to one of these four. The server
has no concept of "follow", "flee", "wander", "attack", "craft", etc. — those
are Python-level strategies that emit these primitives.

**There is also one infrastructure type** (not a gameplay action):

| 4 | `TYPE_RELOAD_BEHAVIOR` | Hot-swap Python source for an Creatures agent. Forwarded to agent process; no world mutation. |

### What this means in practice

- `Follow(target)` is **not** an action type. Python computes `MoveTo(target.x, target.y, target.z)`.
- `Flee(threat)` is **not** an action type. Python computes a flee direction and returns `MoveTo`.
- `Wander()` is **not** an action type. Python picks a random point and returns `MoveTo`.
- The C++ bridge (`python_bridge.cpp`) translates Python action objects → the 4 primitives.
  It must never contain AI logic (no position lookups, no direction computation).
- `StoreItem`, `PickupItem`, `DropItem`, `BreakBlock` are all Python convenience
  wrappers that emit `TYPE_RELOCATE` or `TYPE_CONVERT` internally.

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

### Server (`civcraft-server`)

Headless. **NO Python, NO OpenGL.** Owns all world state.

- Validates `ActionProposal`s from all clients
- Runs physics at 50 Hz, broadcasts entity state at 20 Hz
- Tracks entity ownership (which client controls which entity)
- **Auto-spawns agent client processes** for Creatures entities (via `ClientManager`)
- Perception-scoped: each client only receives state within 64 blocks

### Player Client (`civcraft`)

GUI. **OpenGL, NO Python.**

- Renders world, handles WASD/mouse/camera input
- Sends ActionProposals for the player entity
- Connects to server via TCP (localhost in singleplayer, remote in MP)
- In singleplayer: `AgentManager` launches the server process first

### Agent Client (`civcraft-agent`)

Headless. **Python + pybind11, NO OpenGL.**

- One process per Creatures entity (avoids Python GIL, full parallelism)
- Runs `decide(self, world)` at 4 Hz, sends ActionProposals
- Receives perception-scoped entity/chunk state from server
- **Spawned and managed by the server** — not by the player client

### Singleplayer Flow

```
1. Player clicks "Play"
2. AgentManager (GUI) fork+execs civcraft-server on localhost
3. GUI connects to server as a regular player client
4. Server's ClientManager detects Creatures entities with BehaviorId
5. Server fork+execs one civcraft-agent per Creatures entity
6. Each agent connects back to server, receives S_ASSIGN_ENTITY
7. Agents run Python AI, entities start moving
```

Multiplayer is identical — server is just on a different machine.

## Entity Ownership

Every living entity is controlled by exactly one client:

| Client Type | Controls | Identified By |
|-------------|----------|---------------|
| Player client | Its player entity | `C_HELLO` on connect |
| Agent client | One Creatures entity | `C_AGENT_HELLO` + entity ID |

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
`artifacts/<category>/base/`. User-created content will be served from a
DB layer (not yet implemented) — there is no on-disk writable tier.

### Categories

| Category | What It Defines | Example |
|----------|-----------------|---------|
| `creatures/` | Stats, collision, model ref, behavior | pig.py, villager.py |
| `behaviors/` | AI logic: `decide(self, world)` → action | wander.py, prowl.py |
| `items/` | Weapons, tools, consumables | sword.py, potion.py |
| `blocks/` | Hardness, color, drops, transparency | terrain.py |
| `models/` | Box model geometry + animations | pig.py, sword.py |
| `effects/` | Combat/status effects | damage.py, poison.py |
| `characters/` | Player skins + appearance | guy.py, mage.py |
| `worlds/` | World gen: terrain, villages, mobs | village.py |
| `actions/` | Custom action types | *(planned)* |
| `resources/` | Audio/visual metadata | creature_sounds.py |

### Forking & Sharing

User-authored artifacts (forked from base or written from scratch) are
served by a DB layer — not an on-disk `player/` directory. The DB layer
is not yet implemented; in the meantime only `base/` is loaded.

- Each artifact is self-contained — one Python file, standard dict format
- Upload custom creatures, behaviors, items, models to share with others

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
| `civcraft` | shared + client + game + OpenGL | Player client (GUI) |
| `civcraft-server` | shared + server + content + Python | Server + agent spawner |
| `civcraft-agent` | shared + agent + content + Python | Agent client (headless) |

## Build & Run

```bash
make game        # singleplayer (server + agents auto-launched)
make server      # dedicated server (agents auto-spawned)
make client      # player client → localhost:7777
make stop        # kill all processes
```
