# Behavior API Reference

## Overview

Every creature in ModCraft has a behavior defined as a Python file. The behavior's
`decide()` method is called 4 times per second and returns `(action, goal_str)`.

**Behaviors run on the agent CLIENT, not the server.**
Each NPC has its own `modcraft-agent` process. The agent reads its `LocalWorld`
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
Behavior.decide(entity: SelfEntity, world: LocalWorld) → (action, goal_str)
```

`LocalWorld` is a pydantic object rebuilt each tick from the agent's C++ caches.
Stale data is tolerated — the server rejects invalid actions and behaviors recover.

---

## Function Signature

```python
from modcraft_engine import Idle, Wander, MoveTo, Follow, Flee
from behavior_base import Behavior

class MyBehavior(Behavior):
    def decide(self, entity, world):
        # entity : SelfEntity  — this creature's full state
        # world  : LocalWorld  — what this creature can currently perceive
        # Returns: (action, goal_str)
        return Idle(), "Standing guard"
```

---

## `entity` — SelfEntity

Typed pydantic object. All fields are read-only.

| Field | Type | Description |
|-------|------|-------------|
| `entity.id` | int | Entity ID (unique per creature) |
| `entity.type_id` | str | e.g. `"base:pig"`, `"base:dog"` |
| `entity.x / y / z` | float | World position |
| `entity.yaw` | float | Facing direction (degrees) |
| `entity.hp` | int | Current health points |
| `entity.walk_speed` | float | Base movement speed |
| `entity.on_ground` | bool | Whether standing on solid ground |
| `entity.inventory` | InventoryView | Read-only inventory snapshot |

**Custom server-assigned props** (home_x, work_radius, collect_goal, …):
```python
entity.get("work_radius", 80.0)   # float, with default
entity.get("home_x")              # None if not set
```

**Inventory:**
```python
entity.inventory.count("base:trunk")   # → int
bool(entity.inventory)                  # → True if non-empty
```

---

## `world` — LocalWorld

Pydantic object representing what this agent can currently perceive.
Cached subset of GlobalWorld. May be stale — server validates all actions.

### Spatial queries

```python
# Nearest block or entity of a type — O(1) index lookup
world.get("base:trunk")               # → BlockView | None
world.get("base:trunk", max_dist=40)  # → within 40 units

# Nearest entity — includes dropped items, NPCs, players
world.get("base:spider")              # → EntityView | None
world.get("base:dog", max_dist=6)

# All of a type, nearest-first
world.all("base:trunk")               # → [BlockView, …]
world.all("base:trunk", max_dist=80)

# Nearest entity by category
world.nearest("player")               # → EntityView | None
world.nearest("player", max_dist=20)
world.nearest("chest")
world.nearest("hostile", max_dist=12)
```

### Time and delta

```python
world.time   # float, 0.0–1.0 day fraction (0.0 = midnight, 0.5 = noon)
world.dt     # float, seconds since last decide() (~0.25 s)
```

### Raw lists (for complex queries)

```python
world.blocks    # list[BlockView]  — pre-sorted nearest-first
world.entities  # list[EntityView] — within 64-unit radius
```

### BlockView fields

| Field | Type | Description |
|-------|------|-------------|
| `block.x / y / z` | int | Block position |
| `block.type_id` | str | e.g. `"base:trunk"`, `"base:wood"` |
| `block.distance` | float | Distance from this creature |

### EntityView fields

| Field | Type | Description |
|-------|------|-------------|
| `entity.id` | int | Entity ID |
| `entity.type_id` | str | e.g. `"base:chicken"`, `"base:villager"` |
| `entity.category` | str | `"player"`, `"animal"`, `"npc"`, `"hostile"`, `"item"`, `"chest"` |
| `entity.x / y / z` | float | World position |
| `entity.distance` | float | Distance from this creature |
| `entity.hp` | int | Current health |

---

## Behavior base class helpers

```python
# Time of day
self.is_night(world)      # 0–25% of day
self.is_morning(world)    # 25–50%
self.is_afternoon(world)  # 50–75%
self.is_evening(world)    # 75–100%

# Distance
self.dist2d(ax, az, bx, bz)              # XZ horizontal distance

# Proximity check (works with BlockView, EntityView, tuple, or dict)
self.is_near(entity, pos, threshold=2.5)

# Home management
self._home = self.init_home(entity, self._home)   # reads home_x/home_z props
self._chest = self.get_chest(entity, self._home)  # reads chest_x/y/z props

# Stuck detection (call each tick while navigating)
if self.check_stuck(entity, world.dt):
    self.reset_stuck()
    return Wander(speed=spd), "Stuck — wandering"
```

---

## Actions — What to Return

Always return a 2-tuple: `(action, goal_str)`. The goal string is shown above
the entity's head and in the inspect panel.

Import from the engine:
```python
from modcraft_engine import Idle, Wander, MoveTo, Follow, Flee, ConvertObject, StoreItem
```

| Action | Description |
|--------|-------------|
| `Idle()` | Stand still |
| `Wander(speed=2.0)` | Random walk |
| `MoveTo(x, y, z, speed=2.0)` | Walk to position |
| `Follow(entity_id, speed=2.0, min_distance=1.5)` | Track an entity |
| `Flee(entity_id, speed=4.0)` | Run away from entity |
| `ConvertObject(from_item, to_item, block_pos, …)` | Block-based crafting/harvesting |
| `StoreItem(chest_entity_id)` | Deposit inventory into chest |

---

## Full Example

```python
"""Guard dog — follows nearest player, chases away cats."""
import random
from modcraft_engine import Idle, Wander, Follow, Flee
from behavior_base import Behavior

class GuardDogBehavior(Behavior):
    def __init__(self):
        self._home = None

    def decide(self, entity, world):
        self._home = self.init_home(entity, self._home)
        spd = entity.walk_speed

        # Go home at night
        if self.is_night(world) or self.is_evening(world):
            if not self.is_near(entity, self._home, threshold=3):
                return MoveTo(*self._home, speed=spd), "Going home"
            return Idle(), "Sleeping zzz"

        # Chase cats away
        cat = world.get("base:cat", max_dist=8)
        if cat:
            return Follow(cat.id, speed=spd * 1.5, min_distance=1), "Chasing cat!"

        # Follow nearest player
        player = world.nearest("player")
        if player:
            if player.distance > 3:
                return Follow(player.id, speed=4.0, min_distance=3), \
                       "Following player (%dm)" % int(player.distance)
            return Idle(), "Sitting by player"

        return Wander(speed=spd * 0.5), "Sniffing around"
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
