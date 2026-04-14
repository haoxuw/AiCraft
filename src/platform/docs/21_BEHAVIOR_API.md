# Behavior API Reference

## Overview

Every creature in CivCraft has a behavior defined as a Python file. The behavior's
`decide()` method is called 4 times per second and returns `(action, goal_str)`.

**Behaviors run on the agent CLIENT, not the server.**
Each Creatures has its own `civcraft-agent` process. The agent reads its `LocalWorld`
cache (nearby entities, nearby blocks from loaded chunks), runs `decide()`, and
sends the resulting `ActionProposal` over TCP. The server validates and executes.

This means:
- Pathfinding and block scanning are client-side (no server CPU cost)
- Adding NPCs = adding AI clients, not loading the server
- A single server can host hundreds of players and NPCs

Behaviors are stored in `artifacts/behaviors/` and hot-reloadable without rebuilding.

---

## World Architecture

```
Server  GlobalWorld (C++, authoritative)
  │  TCP: S_BLOCK, S_ENTITY, S_CHUNK
  ▼
Agent   LocalWorld (Python, cached subset — may be stale, that's fine)
  │
  ▼
Behavior.decide(entity: SelfEntity, local_world: LocalWorld) → (action, goal_str)
```

`LocalWorld` is a pydantic object rebuilt each tick from the agent's C++ caches.
Stale data is tolerated — the server rejects invalid actions and behaviors recover.

---

## Function Signature

```python
from civcraft_engine import Idle, MoveTo, Convert, StoreItem, Interact
from behavior_base import Behavior

class MyBehavior(Behavior):
    def decide(self, entity, local_world):
        # entity : SelfEntity  — this creature's full state
        # local_world  : LocalWorld  — what this creature can currently perceive
        # Returns: (action, goal_str)
        return Idle(), "Standing guard"
```

---

## `entity` — SelfEntity

Typed pydantic object. All fields are read-only.

| Field | Type | Description |
|-------|------|-------------|
| `entity.id` | int | Entity ID (unique per creature) |
| `entity.type` | str | e.g. `"base:pig"`, `"base:dog"` |
| `entity.x / y / z` | float | World position |
| `entity.yaw` | float | Facing direction (degrees) |
| `entity.hp` | int | Current health points |
| `entity.walk_speed` | float | Base movement speed |
| `entity.on_ground` | bool | Whether standing on solid ground |
| `entity.inventory` | InventoryView | Read-only inventory snapshot |

**Custom server-assigned props** (work_radius, collect_goal, …):
```python
entity.get("work_radius", 80.0)   # float, with default
entity.get("collect_goal", 5)     # int, with default
```

**Inventory:**
```python
entity.inventory.count("base:trunk")   # → int
bool(entity.inventory)                  # → True if non-empty
```

---

## `local_world` — LocalWorld

Pydantic object representing what this agent can currently perceive.
Cached subset of GlobalWorld. May be stale — server validates all actions.

### Spatial queries

```python
# Nearest block or entity of a type — O(1) index lookup
local_world.get("base:trunk")               # → BlockView | None
local_world.get("base:trunk", max_dist=40)  # → within 40 units

# Nearest entity — includes dropped items, NPCs, players
local_world.get("base:spider")              # → EntityView | None
local_world.get("base:dog", max_dist=6)

# All of a type, nearest-first
local_world.all("base:trunk")               # → [BlockView, …]
local_world.all("base:trunk", max_dist=80)

# Nearest entity by category
local_world.nearest("player")               # → EntityView | None
local_world.nearest("player", max_dist=20)
local_world.nearest("chest")
local_world.nearest("hostile", max_dist=12)
```

### Time and delta

```python
local_world.time   # float, 0.0–1.0 day fraction (0.0 = midnight, 0.5 = noon)
local_world.dt     # float, seconds since last decide() (~0.25 s)
```

### Raw lists (for complex queries)

```python
local_world.blocks    # list[BlockView]  — pre-sorted nearest-first
local_world.entities  # list[EntityView] — within 64-unit radius
```

### BlockView fields

| Field | Type | Description |
|-------|------|-------------|
| `block.x / y / z` | int | Block position |
| `block.type` | str | e.g. `"base:trunk"`, `"base:wood"` |
| `block.distance` | float | Distance from this creature |

### EntityView fields

| Field | Type | Description |
|-------|------|-------------|
| `entity.id` | int | Entity ID |
| `entity.type` | str | e.g. `"base:chicken"`, `"base:villager"` |
| `entity.category` | str | `"player"`, `"animal"`, `"npc"`, `"hostile"`, `"item"`, `"chest"` |
| `entity.x / y / z` | float | World position |
| `entity.distance` | float | Distance from this creature |
| `entity.hp` | int | Current health |

---

## Behavior base class helpers

```python
# Time of day
self.is_night(local_world)      # 0–25% of day
self.is_morning(local_world)    # 25–50%
self.is_afternoon(local_world)  # 50–75%
self.is_evening(local_world)    # 75–100%

# Distance
self.dist2d(ax, az, bx, bz)              # XZ horizontal distance

# Proximity check (works with BlockView, EntityView, tuple, or dict)
self.is_near(entity, pos, threshold=2.5)

# Home anchor (= first-observed position; no server-assigned props)
self._home = self.init_home(entity, self._home)

# Chest lookup (discover chests dynamically — no pre-assigned chest IDs)
hits = scan_entities("base:chest", near=(entity.x, entity.y, entity.z),
                     max_dist=120, max_results=1)
if hits:
    chest_eid = int(hits[0]["id"])
    return StoreItem(chest_eid), "Depositing"

# Stuck detection (call each tick while navigating)
if self.check_stuck(entity, local_world.dt):
    self.reset_stuck()
    tx, ty, tz = self.wander_target(entity, radius=6)
    return MoveTo(tx, ty, tz, speed=spd), "Stuck — wandering"
```

---

## Actions — What to Return

Always return a 2-tuple: `(action, goal_str)`. The goal string is shown above
the entity's head and in the inspect panel.

### Read query — `get_block` (agent-side only, never sent to server)

```python
from civcraft_engine import get_block
block_id = get_block(x, y, z)   # e.g. "base:trunk", "base:air"
```

`get_block` probes the agent's local chunk cache. It is **not** one of the four action
types and cannot change local_world state.  Call it freely inside `decide()` for pathfinding
or environment sensing.

### Write actions — the four server primitives

```python
from civcraft_engine import MoveTo, Relocate, Convert, Interact
from actions import StoreItem, PickupItem, DropItem, BreakBlock  # Python wrappers
```

| Action | Source | Description |
|--------|--------|-------------|
| `MoveTo(x, y, z, speed=2.0)` | C++ bridge | Walk to position |
| `Relocate(…)` | C++ bridge | Move items between inventories |
| `Convert(from_item, to_item, …)` | C++ bridge | Crafting/harvesting/attack |
| `Interact(x, y, z)` | C++ bridge | Open door, press button |
| `StoreItem(chest_entity_id)` | `actions.py` | Deposit inventory → Relocate(to_entity=…) |
| `PickupItem(entity_id)` | `actions.py` | Pick up item → Relocate(from_entity=…) |
| `DropItem(item_type, count=1)` | `actions.py` | Drop at feet → Relocate(to_ground=True, …) |
| `BreakBlock(x, y, z)` | `actions.py` | Mine block → Convert(convert_from_block=True, …) |

**To stand still**: `MoveTo(entity.x, entity.y, entity.z)` — move to current position.

### Behavior helper methods (on `Behavior` base class)

High-level helpers that compute a target and return a `MoveTo`. These are pure
Python — no server involvement — and live in `behavior_base.py`:

```python
# Random walk: pick a point within radius and return MoveTo
tx, ty, tz = self.wander_target(entity, radius=8)
return MoveTo(tx, ty, tz, speed=spd), "Wandering"

# Follow an EntityView: move toward it (or Idle if already close enough)
if entity_view.distance > min_dist:
    return MoveTo(entity_view.x, entity_view.y, entity_view.z, speed=spd), "Following"

# Flee from an EntityView: move in the opposite direction
dx = entity.x - entity_view.x
dz = entity.z - entity_view.z
d = (dx*dx + dz*dz) ** 0.5 or 1
return MoveTo(entity.x + dx/d * 12, entity.y, entity.z + dz/d * 12, speed=spd), "Fleeing!"
```

---

## Full Example

```python
"""Guard dog — follows nearest player, chases away cats."""
from civcraft_engine import Idle, MoveTo
from behavior_base import Behavior

class GuardDogBehavior(Behavior):
    def __init__(self):
        self._home = None

    def decide(self, entity, local_world):
        self._home = self.init_home(entity, self._home)
        spd = entity.walk_speed

        # Go home at night
        if self.is_night(local_world) or self.is_evening(local_world):
            if not self.is_near(entity, self._home, threshold=3):
                return MoveTo(*self._home, speed=spd), "Going home"
            return Idle(), "Sleeping zzz"

        # Chase cats away — move toward cat position
        cat = local_world.get("base:cat", max_dist=8)
        if cat:
            return MoveTo(cat.x, cat.y, cat.z, speed=spd * 1.5), "Chasing cat!"

        # Follow nearest player
        player = local_world.nearest("player")
        if player:
            if player.distance > 3:
                return MoveTo(player.x, player.y, player.z, speed=4.0), \
                       "Following player (%dm)" % int(player.distance)
            return Idle(), "Sitting by player"

        # Wander — pick a random nearby point and walk to it
        tx, ty, tz = self.wander_target(entity, radius=8)
        return MoveTo(tx, ty, tz, speed=spd * 0.5), "Sniffing around"
```

---

## Entity Categories

| Category | Description |
|----------|-------------|
| `"player"` | Human-controlled character |
| `"animal"` | Passive AI creature (pig, chicken, cat, dog) |
| `"npc"` | AI worker (villager) |
| `"hostile"` | Aggressive mob |
| `"item"` | Dropped item on the ground |
| `"chest"` | Storage container |

---

## Block Type IDs (common)

| ID | Description |
|----|-------------|
| `base:grass` | Grass block |
| `base:dirt` | Dirt |
| `base:stone` | Stone |
| `base:cobblestone` | Cobblestone |
| `base:trunk` | Tree trunk (choppable) |
| `base:wood` | Wood planks / processed wood |
| `base:leaves` | Tree leaves |
| `base:sand` | Sand |
| `base:water` | Water |
| `base:planks` | Wooden planks |
| `base:fence` | Fence post |
