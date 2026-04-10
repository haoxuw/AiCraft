# ModCraft - Architecture Diagram

Two process types: **WorldServer** and **Client** (PlayerClient + AgentClient in one binary).

---

## Process Architecture

```
+===========================================================+
|                     WORLD SERVER                           |
|                     (C++ Engine)                           |
|                                                            |
|  Source of truth: blocks, entities, HP, inventory, items   |
|  Validates action proposals from all clients               |
|  Broadcasts state via UDP (positions, blocks, time)        |
|  Handles proposals via TCP (move, harvest, attack, etc.)   |
|  1 process per world (separate processes, not batched)     |
+==================+========================================+
                   |  UDP state sync + TCP proposals
                   |
      +------------+-------------+------------+
      |                          |             |
      v                          v             v
+============+            +============+   +============+
|  Client A  |            |  Client B  |   |  Client C  |
| (Player 1) |            | (Player 2) |   | (Player 3) |
+============+            +============+   +============+

Each Client is ONE process containing:
  - PlayerClient: GUI, input, rendering
  - AgentClient:  AI for all NPCs owned by this player
  - Shared chunk cache (up to 200MB, always connected, no gaps)
```

---

## Inside a Client Process

```
+=================================================================+
|                        CLIENT PROCESS                            |
|                                                                  |
|  +---------------------------+   +----------------------------+  |
|  |      PLAYER CLIENT        |   |       AGENT CLIENT         |  |
|  |                           |   |                            |  |
|  |  GUI / Input / Rendering  |   |  Python AI for owned NPCs  |  |
|  |  Camera, HUD, Controls    |   |  decide() -> Plan          |  |
|  |  Sends human actions      |   |  execute Plan each tick    |  |
|  +-------------+-------------+   +-------------+--------------+  |
|                |                               |                 |
|                +---------- SHARED -------------+                 |
|                |                               |                 |
|  +-------------v-------------------------------v--------------+  |
|  |              SHARED CHUNK + ENTITY CACHE                   |  |
|  |                                                            |  |
|  |  Chunks: eagerly fetched from server, up to 200MB          |  |
|  |  Entities: positions, HP, inventory (from server broadcast) |  |
|  |  Always connected — no isolated chunks                     |  |
|  +------------------------------------------------------------+  |
|                                                                  |
|  +------------------------------------------------------------+  |
|  |              NETWORK LAYER                                 |  |
|  |  UDP: receive state sync (entity pos, block changes, time) |  |
|  |  TCP: send action proposals, receive approval/rejection    |  |
|  +------------------------------------------------------------+  |
+=================================================================+
```

---

## Agent AI Loop (per tick)

```
Phase 1 — DECIDE (for agents needing a new plan):
  +------------------+
  | Python decide()  |  -> Returns a Plan (list of steps)
  | sees: entity,    |
  | local_world      |  Plan = [Move(A), Move(B), Harvest(tree), Move(chest), Relocate(logs)]
  +------------------+
  If chunks not loaded for planning -> defer (put back in queue)

Phase 2 — EXECUTE (for all agents with active plans):
  +------------------+
  | Tick current step|  Move: pathfind waypoints, send Move proposals
  |                  |  Harvest: Convert proposal if in range, else pathfind
  |                  |  Attack: Move toward + Convert when in range
  |                  |  Relocate: Relocate proposal if in range, else pathfind
  +------------------+
  Step complete -> advance to next
  Plan complete or failed -> mark for re-decide
```

---

## Plan Step Types (4 total)

All map to the server's 4 action types: Move, Convert, Relocate, Interact.

| Plan Step | What it does | Server Action |
|-----------|-------------|---------------|
| **Move(pos)** | Walk to position (client-side pathfinding) | TYPE_MOVE |
| **Harvest(block_pos)** | Break block (tree, ore) | TYPE_CONVERT |
| **Attack(entity_id)** | Melee hit | TYPE_CONVERT |
| **Relocate(from, to, item)** | Move item between containers | TYPE_RELOCATE |

---

## Plan Visualization (owner only)

```
  PlayerClient renders plans for entities owned by local player:

  [Crewmate] ------> A ------> B ------> C ---[axe icon]---> tree
                green dashed polyline        action icon

  Only visible to the owning player, not other players.
```

---

## The Three Layers

```
+===========================+     +============================+
|    GAME MODEL LAYER       |     |    RENDERING LAYER         |
|    (Python artifacts)     |     |    (C++ Engine)            |
|                           |     |                            |
|    Creatures, Items,      |     |    PlayerClient GUI        |
|    Behaviors, Blocks,     |     |    Animations, Textures,   |
|    World templates        |     |    Sounds, Input, Views    |
+============+==============+     +============+===============+
             |                                 |
             v                                 v
+==============================================================+
|                WORLD SERVER LAYER (C++ Engine)                 |
|                                                                |
|    Owns the World. Validates actions. Broadcasts state.       |
|    Single source of truth. No rendering. No AI.               |
+==============================================================+
```

---

## Object Model

```
+-------------------------------------------------------------------------+
|                                                                          |
|   ENTITIES (everything in the world)                                    |
|                                                                          |
|   Living (moves, has HP, has inventory)                                 |
|     Player    — just a Living with playable=true                        |
|     Crewmate  — NPC, controlled by AgentClient                         |
|     Knight    — NPC, controlled by AgentClient                         |
|     Chicken   — animal, controlled by AgentClient                      |
|     Pig       — animal, controlled by AgentClient                      |
|                                                                          |
|   Item (on ground or in inventory)                                      |
|     Apple, Bread, Sword, Pickaxe, etc.                                  |
|                                                                          |
|   Blocks are NOT entities — they live in the chunk grid                 |
|                                                                          |
|   Properties (on all entities):                                         |
|     HP, Inventory, Owner, BehaviorId, Goal, etc.                        |
|     Altered by server-validated actions only                            |
|                                                                          |
+-------------------------------------------------------------------------+
```

---

## Server Action Types (4 total)

The server validates exactly four action types. All gameplay compiles down to these:

| Type | What | Constraint |
|------|------|-----------|
| **Move** (0) | Set entity velocity/direction | No tunnelling |
| **Relocate** (1) | Move item between containers | Value conserved |
| **Convert** (2) | Transform item type | Value must not increase |
| **Interact** (3) | Toggle block state (door, TNT) | — |

---

## Separation Rules

### Rule 1: Python is the Game, C++ is the Engine

All game content defined in Python artifacts (creatures, behaviors, items, blocks).
C++ provides physics, networking, rendering. No gameplay constants in C++.

### Rule 2: The Player is Not Special

Player is just a Living entity with `playable=true`. Any Living can be controlled
by an AgentClient. NPCs are Living with a BehaviorId.

### Rule 3: Server is Authoritative

Server owns and modifies all world state. Clients submit proposals (intent).
Server validates and executes. Client-side prediction for responsiveness,
server corrects if diverged.

### Rule 4: All AI Runs on Client

The server has ZERO intelligence. All AI runs in AgentClient inside each
player's Client process. Python `decide()` produces plans, execution sends
proposals to server for validation.

### Rule 5: Server Has No Display Logic

Server never decides what to show on any client's screen. Each client
observes the state stream and decides its own display (damage text, particles,
sound effects, plan visualization).

---

## Product Roadmap

```
Playable       -->   FUN   -->  Programmable  -->  Creative  -->  Agentic  -->  Meta realworld
Sandbox                                                                          assets
```

| Stage | What it means |
|-------|---------------|
| **Playable Sandbox** | Walk around, place/mine blocks, flat world |
| **FUN** | Combat, mobs, crafting, survival mechanics |
| **Programmable** | In-game Python editor, hot-load objects & actions |
| **Creative** | Players create and share content, artifact marketplace |
| **Agentic** | LLM generates objects/actions during gameplay, AI NPCs |
| **Meta realworld assets** | Bridge game creations to real-world value |
