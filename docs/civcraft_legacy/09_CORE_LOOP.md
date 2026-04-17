# CivCraft - Core Loop & Conceptual Separation

> **DESIGN SKETCH — uses non-existent class names** (`WanderTo`, `FleeFrom`,
> `Place`, etc.) that do not match the actual Python API. See
> `docs/21_BEHAVIOR_API.md` for the real action types.

Two orthogonal axes. **Server/Client** is about WHERE code runs. **World/Action/Object** is about WHAT the game simulates. They must not be conflated.

---

## 1. The Two Axes

```
Axis 1: DEPLOYMENT (where does code run?)

  +------------------+     network     +------------------+
  |  C++ SERVER      | <============> |  Python CLIENT    |
  |                  |                |                   |
  |  Single source   |                |  Renders what     |
  |  of truth.       |                |  server tells it. |
  |  Runs the sim.   |                |  Sends inputs.    |
  +------------------+                +-------------------+

Axis 2: GAME MODEL (what does the simulation compute?)

  +------------------+
  |      WORLD       |   The container. Holds everything.
  +------------------+
          |
    +-----+-----+
    |           |
  +-v----+   +--v-----+
  |Object|   | Action |   Objects exist. Actions happen.
  +------+   +--------+
    |
  +-+----------+
  |            |
  Passive    Active      Passive waits. Active decides.
```

**These two axes are independent.** The World/Action/Object model is a pure simulation concept. It doesn't know about servers, clients, or networks. The Server/Client split is a deployment concern -- it decides where the simulation runs and how state is synchronized.

---

## 2. The Game Model (Pure Simulation)

### World

The World is a data structure. It contains:
- A voxel grid of blocks (each block is an Object)
- A set of entities (each entity is an Object)
- Global state: time, weather, light

The World does NOT:
- Know about networking
- Know about rendering
- Know about player input devices
- Have a step() method itself (it's stepped FROM OUTSIDE)

```python
class World:
    grid: ChunkManager          # blocks
    entities: EntityManager     # non-block objects
    time: WorldTime
    weather: WeatherState
    registry: Registry          # all known types

    # World is DATA, not LOGIC.
    # It provides read/write access but doesn't drive itself.
```

### Object (Passive vs Active)

```
Object = something that EXISTS in the world.

PassiveObject:
  - HAS attributes (texture, hardness, groups, etc.)
  - DOES NOT act on its own
  - Only changes when an Action targets it
  - Examples: dirt, stone, planks, chest, crafting table

ActiveObject:
  - HAS attributes (same as passive)
  - ALSO HAS a decide() method
  - Called every step to produce Actions
  - decide() READS world state, RETURNS a list of Actions
  - decide() DOES NOT directly mutate anything
  - Examples: water, fire, pig, player, Creatures, ticking bomb

Key insight:
  ActiveObject.decide() is a PURE FUNCTION of (self_state, world_view) -> [Action]
  It does not mutate the world. It only PROPOSES changes via Actions.
```

```python
class PassiveObject:
    meta: ObjectMeta
    # ... attributes ...
    # No decide(). Just data.

class ActiveObject:
    meta: ObjectMeta
    # ... attributes ...

    def decide(self, world_view: WorldView) -> list[Action]:
        """Observe the world, return actions to take.
        MUST NOT mutate world or self. Read-only."""
        return []
```

### Action

```
Action = something that HAPPENS to the world.

An Action is a complete description of a state change:
  - WHO/WHAT initiated it
  - WHAT it does (mutations)
  - WHERE it applies

Actions are the ONLY way the world changes.
Nothing else writes to the World. Ever.
```

```python
class Action:
    meta: ActionMeta

    def validate(self, world_view: WorldView) -> bool:
        """Can this action happen right now? Read-only check."""
        ...

    def execute(self, world_view: WorldView) -> list[Mutation]:
        """Apply the action. Returns list of mutations.
        May also return chained Actions."""
        ...
```

---

## 3. The Step Loop

Each game step follows a strict 3-phase order:

```
+=================================================================+
|                    ONE GAME STEP                                 |
|                                                                   |
|  Phase 1: RESOLVE ACTIONS (World changes)                        |
|  Phase 2: RENDER / BROADCAST (Observe new state)                 |
|  Phase 3: GATHER INPUTS (ActiveObjects decide next actions)      |
|                                                                   |
+=================================================================+

     Phase 1              Phase 2              Phase 3
  +-----------+       +-------------+       +-------------+
  | RESOLVE   |  -->  |   RENDER    |  -->  |   GATHER    |
  | ACTIONS   |       |   (output)  |       |   INPUTS    |
  +-----------+       +-------------+       +-------------+
  |                   |                     |
  | Process all       | Server: broadcast  | For each ActiveObject:
  | pending actions   |   state deltas     |   call decide(world_view)
  | from last step.   |   to clients       |   collect proposed Actions
  |                   |                     |
  | For each action:  | Client: render     | For each Player:
  |   validate()      |   the current      |   read input device
  |   execute()       |   world state      |   map to Actions
  |   apply mutations |   (the frame)      |
  |   collect chains  |                     | For each Creatures:
  |                   |                     |   AI decision -> Actions
  | World state is    |                     |
  | now updated.      |                     | Queue all Actions for
  |                   |                     | NEXT step's Phase 1.
  +---------+---------+---------+-----------+-----------+-----------+
            |                               |
            +-------------------------------+
            Actions flow from Phase 3 to Phase 1 of the NEXT step.
```

### Why This Order?

```
1. RESOLVE first:
   - World is in a CONSISTENT state after all actions execute.
   - No partial updates. No race conditions.
   - All observers (render, decide) see the same snapshot.

2. RENDER/BROADCAST second:
   - Players see the world AFTER actions resolved.
   - What you see = what is true. No desync.

3. GATHER last:
   - Decisions are based on the LATEST world state.
   - A pig sees that grass is there AFTER it was potentially eaten.
   - A player sees the block is gone AFTER it was mined.
   - Actions go into a queue for the NEXT step.
```

---

## 4. Detailed Step Breakdown

```
step(dt):
  |
  |===== PHASE 1: RESOLVE ACTIONS ============================
  |
  |  action_queue = get_pending_actions()      # from last step
  |  sort by priority (world > chain > player > entity > deferred)
  |
  |  for action in action_queue:
  |      view = WorldView(world, action.origin, action.radius)
  |
  |      if not action.validate(view):
  |          discard(action)
  |          continue
  |
  |      mutations = action.execute(view)
  |
  |      for m in mutations:
  |          apply_to_world(m)                 # actually change state
  |
  |      for chained_action in view.emitted_actions:
  |          action_queue.push(chained_action)  # process this step too
  |
  |  # World state is now fully updated for this step.
  |  world.time.tick += 1
  |  world.time.advance(dt)
  |
  |===== PHASE 2: RENDER / BROADCAST =========================
  |
  |  # Server side:
  |  deltas = diff(world_before, world_after)
  |  for client in connected_clients:
  |      send(client, relevant_deltas)
  |
  |  # Client side (on receiving deltas):
  |  apply_deltas(local_state)
  |  render_frame(local_state)                 # draw what we see
  |
  |===== PHASE 3: GATHER INPUTS ==============================
  |
  |  next_actions = []
  |
  |  # --- Active Object decisions ---
  |  for obj in world.get_all_active_objects():
  |      view = WorldView(world, obj.pos, obj.view_radius)
  |      proposed = obj.decide(view)           # READ-ONLY
  |      next_actions.extend(proposed)
  |
  |  # --- Player inputs ---
  |  for player in world.players.online():
  |      inputs = get_player_inputs(player)    # from network/keyboard
  |      player_actions = map_inputs_to_actions(player, inputs)
  |      next_actions.extend(player_actions)
  |
  |  # --- World system actions (ABMs, timers) ---
  |  system_actions = world.check_periodic_systems(dt)
  |  next_actions.extend(system_actions)
  |
  |  # Queue for next step's Phase 1
  |  action_queue.push_all(next_actions)
  |
  |  return  # step complete
```

---

## 5. decide() — pure read, returns ActionProposals

`decide()` is the single entry point for behavior code. It reads the world through
a `view` and returns a list of `ActionProposal`s. It never writes.

```
  def decide(self, view) -> [ActionProposal]:
      actions = []
      if self.hunger < 0.3:
          grass = view.find_nearest(
              self.pos, "base:dirt",
              filter=lambda b: b.grass_level > 0.3)
          if grass:
              # "Eat grass" = CONVERT the grass block to dirt;
              # hunger restoration is a separate CONVERT on the pig.
              actions.append(Convert(
                  from_pos=grass.pos, to_block="base:dirt"))
      return actions

  Properties:
  - Pure read: never mutates world or self directly
  - Returns ActionProposals (intents only)
  - Server validates & executes in Phase 1 of NEXT step
  - Fully auditable; order-independent
```

### The four action types — the ONLY thing the server accepts

Every behavior decision compiles down to one of these four proposals
(see `03_ACTIONS.md` and Rule 0 in CLAUDE.md):

| Type | Purpose |
|------|---------|
| `TYPE_MOVE`     | Set entity velocity/direction. Wander, follow, flee — all Python helpers that return a MoveTo target. |
| `TYPE_RELOCATE` | Move an Object between containers (inventory ↔ ground ↔ entity). Value is conserved. |
| `TYPE_CONVERT`  | Transform an Object from one type to another (value must not increase). Consuming, casting, block breaking, damage — all CONVERT. |
| `TYPE_INTERACT` | Toggle interactive block state (door, button, TNT fuse). |

There is no `WanderTo`, no `EatGrass`, no `Place`, no `Attack` at the protocol
level. Those are Python-side names for helpers that build one of the four
proposals above.

### Self-state changes (hunger, timers)

Attributes that change on every tick (hunger decay, age) don't need to go
through ActionProposals — the agent client updates them locally between
`decide()` calls. Only state that affects the shared world requires a
proposal (which the server validates).

```python
class Pig:
    hunger: float = 1.0          # local state, ticked by the agent client
    age: float = 0.0

    def decide(self, view) -> list[ActionProposal]:
        # Hunger decay happens outside decide() — just READ it here.
        if self.hunger < 0.3:
            grass = view.find_nearest_block(
                self.pos, radius=8,
                filter=lambda b: hasattr(b, 'grass_level') and b.grass_level > 0.3)
            if grass:
                # CONVERT the grass block; agent client restores hunger locally
                # once the server broadcasts the block change.
                return [Convert(from_pos=grass.pos, to_block="base:dirt")]
            # No grass found → MOVE toward a random nearby spot.
            return [MoveTo(target=wander_target(self.pos, radius=8))]
        return [MoveTo(target=wander_target(self.pos, radius=8))]
```

---

## 6. Player as ActiveObject

A Player is just an ActiveObject whose `decide()` is driven by human input instead of AI:

```
                Living entity
                    |
        +-----------+-----------+
        |                       |
    AI-driven               Human-driven
    (pig, wolf, villager)   (player = Living with playable=true)
        |                       |
   agent client            GUI client sends
   runs decide()           raw input; server's
   every tick              player-controller
        |                   maps it to proposals
        v                       v
     ActionProposals         ActionProposals
     (MOVE/CONVERT/…)        (MOVE/CONVERT/…)
```

```python
# Player has NO decide() in Python — the GUI client sends input directly.
# The server's player controller translates WASD/clicks/right-click into
# the same four ActionProposals any creature would produce.

class Pig:
    hunger: float = 1.0

    def decide(self, view) -> list[ActionProposal]:
        if self.is_threatened:
            # "Flee" = MOVE away from threat; flee_target is a Python helper.
            return [MoveTo(target=flee_target(self.pos, self.threat_pos))]
        if self.hunger < 0.3 and (grass := view.find_grass(self.pos)):
            return [Convert(from_pos=grass.pos, to_block="base:dirt")]
        return [MoveTo(target=wander_target(self.pos, radius=8))]

class Villager:
    def decide(self, view) -> list[ActionProposal]:
        if customer := view.nearest_customer():
            # "Trade" = RELOCATE items between inventories.
            return [Relocate(
                from_inv=self.id, to_inv=customer.id,
                item="base:bread", count=1)]
        return [MoveTo(target=wander_target(self.pos, radius=4))]
```

---

## 7. Server/Client Responsibility Split

```
+================================================================+
|                     SERVER (C++)                                |
|================================================================|
|                                                                  |
|  OWNS:                                                          |
|    - The World (authoritative state)                            |
|    - The step loop (Phase 1, 2-broadcast, 3)                   |
|    - Action validation & execution                              |
|    - Python runtime (hot-loading objects/actions)               |
|    - Physics (collision, gravity) as C++ fast-path              |
|    - Terrain generation                                         |
|    - Chunk storage (DB)                                         |
|                                                                  |
|  DOES NOT:                                                      |
|    - Render anything                                            |
|    - Handle keyboard/mouse directly                             |
|    - Know about OpenGL, textures, or meshes                     |
|    - Make sound                                                 |
|                                                                  |
+================================================================+

+================================================================+
|                     CLIENT (Python)                             |
|================================================================|
|                                                                  |
|  OWNS:                                                          |
|    - Rendering (voxel meshes, entities, sky, particles, UI)     |
|    - Input (keyboard, mouse -> Actions -> server)               |
|    - Audio (positional sound, ambient, music)                   |
|    - Code editor (writing, testing, uploading artifacts)        |
|    - Client-side prediction (optimistic local state)            |
|    - Asset management (textures, models, sounds)                |
|    - Camera control                                             |
|                                                                  |
|  DOES NOT:                                                      |
|    - Run the simulation                                         |
|    - Validate actions                                           |
|    - Own any authoritative state                                |
|    - Execute Python object/action code (server does this)       |
|    - Decide what NPCs/mobs do                                   |
|                                                                  |
+================================================================+
```

### Data Flow

```
Client                          Server
  |                               |
  |  [Input: W key held]          |
  |  Map to: MoveForward          |
  |  TOSERVER_PLAYER_ACTION  ---> |
  |                               |  Phase 3: queue MoveForward action
  |                               |  Phase 1: validate & execute
  |                               |    -> player.pos += velocity * dt
  |                               |  Phase 2: broadcast delta
  |  <--- TOCLIENT_ENTITY_MOVE    |
  |  Apply to local state         |
  |  Render frame                 |
  |                               |
  |  [Input: left-click block]    |
  |  Map to: Mine(pos)            |
  |  TOSERVER_PLAYER_ACTION  ---> |
  |                               |  Phase 3: queue Mine action
  |  [Predict: start dig anim]    |  Phase 1: validate & execute
  |                               |    -> block removed, item dropped
  |  <--- TOCLIENT_BLOCK_CHANGE   |  Phase 2: broadcast
  |  <--- TOCLIENT_ENTITY_ADD     |    (item entity)
  |  Confirm prediction           |
  |  Render updated chunk         |
```

---

## 8. Separation Summary

```
+--------------------+-------------------------------------------+
| Concept            | Responsibility                            |
+--------------------+-------------------------------------------+
| World              | DATA container. No logic. No step().      |
|                    | Read/write via WorldView.                  |
+--------------------+-------------------------------------------+
| PassiveObject      | DATA. Attributes only. No decide().       |
|                    | Changed only by Actions.                   |
+--------------------+-------------------------------------------+
| ActiveObject       | DATA + decide(). Proposes Actions.        |
|                    | Never directly mutates World.              |
|                    | Sub-types: Player, Mob, Creatures, Fluid, etc.  |
+--------------------+-------------------------------------------+
| Action             | LOGIC. validate() + execute().            |
|                    | The ONLY way to mutate World state.        |
|                    | Produces Mutations and chained Actions.    |
+--------------------+-------------------------------------------+
| Server             | INFRASTRUCTURE. Runs the 3-phase loop.    |
|                    | Owns World. Embeds Python. Syncs clients.  |
+--------------------+-------------------------------------------+
| Client             | PRESENTATION. Renders. Takes input.       |
|                    | Sends Actions to server. Mirrors state.    |
+--------------------+-------------------------------------------+
```
