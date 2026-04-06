# Behavior API Reference

## Overview

Every creature in ModCraft has a behavior defined as a Python file. The behavior's `decide()` function is called 4 times per second and returns an action (what the creature should do next).

**Behaviors run on the CLIENT, not the server.** The client reads its local world cache (nearby entities, nearby blocks), runs `decide()`, and sends the resulting action as an ActionProposal to the server. The server only validates ("can you move there?", "can you break that?") and executes.

This means:
- Pathfinding and block scanning are client-side (no server CPU cost)
- Adding NPCs = adding AI clients, not loading the server
- A single server can host hundreds of players and NPCs

Behaviors are stored in `artifacts/behaviors/` and can be viewed and edited from the in-game code editor (right-click an entity → inspect → press E).

---

## Function Signature

```python
def decide(self, world):
    """Called 4 times per second by the server.

    Args:
        self: dict — the creature's own state (read/write)
        world: dict — what the creature can see (read-only)

    Returns:
        An action object (Idle, Wander, MoveTo, Follow, Flee, Attack)
    """
    self["goal"] = "Doing something"  # shown above creature's head
    return Idle()
```

---

## `self` — The Creature

A dict with the creature's current state. You can read all fields and write `goal`.

| Field | Type | Description |
|-------|------|-------------|
| `self["id"]` | int | Entity ID (unique per creature) |
| `self["type_id"]` | str | Type ID (e.g., `"base:pig"`, `"base:dog"`) |
| `self["x"]` | float | World X position |
| `self["y"]` | float | World Y position |
| `self["z"]` | float | World Z position |
| `self["yaw"]` | float | Facing direction (degrees) |
| `self["hp"]` | int | Current health |
| `self["walk_speed"]` | float | Base movement speed |
| `self["on_ground"]` | bool | Whether standing on solid ground |
| `self["goal"]` | str | **Writable.** Text shown above the creature's head. |

---

## `world` — What the Creature Can See

A dict with nearby entities and blocks. Read-only.

### `world["nearby"]` — Nearby Entities

A list of `EntityInfo` objects for all entities within 16 blocks.

| Field | Type | Description |
|-------|------|-------------|
| `entity.id` | int | Entity ID |
| `entity.type_id` | str | Type ID (e.g., `"base:player"`, `"base:pig"`) |
| `entity.category` | str | `"player"`, `"animal"`, `"item"` |
| `entity.x` | float | World X position |
| `entity.y` | float | World Y position |
| `entity.z` | float | World Z position |
| `entity.distance` | float | Distance from this creature |
| `entity.hp` | int | Current health |

**Example: find nearest player**
```python
players = [e for e in world["nearby"] if e.category == "player"]
if players:
    closest = min(players, key=lambda e: e.distance)
```

### `world["blocks"]` — Nearby Blocks

A list of dicts for non-air blocks within 10 blocks. Sorted by distance.

| Field | Type | Description |
|-------|------|-------------|
| `block["x"]` | int | Block X position |
| `block["y"]` | int | Block Y position |
| `block["z"]` | int | Block Z position |
| `block["type"]` | str | Block type ID (e.g., `"base:wood"`, `"base:stone"`) |
| `block["distance"]` | float | Distance from this creature |

**Example: find nearest tree**
```python
wood = [b for b in world["blocks"] if b["type"] == "base:wood"]
if wood:
    nearest = min(wood, key=lambda b: b["distance"])
    tree_x, tree_y, tree_z = nearest["x"], nearest["y"], nearest["z"]
```

### `world["dt"]` — Time Delta

Float, seconds since last decide() call (~0.25 seconds).

---

## Actions — What to Return

Import actions from the engine:
```python
from modcraft_engine import Idle, Wander, MoveTo, Follow, Flee
```

### `Idle()`

Stand still, do nothing.

```python
return Idle()
```

### `Wander(speed=2.0)`

Walk in a random direction. The creature picks a new direction periodically.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `speed` | float | 2.0 | Walking speed |

```python
return Wander(speed=self["walk_speed"])
```

### `MoveTo(x, y, z, speed=2.0)`

Walk toward a specific position. Stops when within 0.5 blocks.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `x` | float | — | Target X |
| `y` | float | — | Target Y |
| `z` | float | — | Target Z |
| `speed` | float | 2.0 | Walking speed |

```python
return MoveTo(tree_x + 0.5, tree_y, tree_z + 0.5, speed=3.0)
```

### `Follow(target, speed=2.0, min_distance=1.5)`

Walk toward an entity, maintaining minimum distance. Idles when close enough.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `target` | int | — | Entity ID to follow |
| `speed` | float | 2.0 | Walking speed |
| `min_distance` | float | 1.5 | Stop when this close |

```python
return Follow(player.id, speed=4.0, min_distance=3.0)
```

### `Flee(target, speed=4.0)`

Run away from an entity.

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `target` | int | — | Entity ID to flee from |
| `speed` | float | 4.0 | Run speed |

```python
return Flee(threat.id, speed=self["walk_speed"] * 2.0)
```

### `BreakBlock(x, y, z)`

Break a block at the given world position. The server validates that the block exists and is within reach (~8 blocks). The block is removed and drops as an item.

| Param | Type | Description |
|-------|------|-------------|
| `x` | int | Block X position |
| `y` | int | Block Y position |
| `z` | int | Block Z position |

```python
# Villager chops a tree
wood = [b for b in world["blocks"] if b["type"] == "base:wood"]
if wood:
    nearest = min(wood, key=lambda b: b["distance"])
    return BreakBlock(nearest["x"], nearest["y"], nearest["z"])
```

---

## Block Type IDs

| ID | Description |
|----|-------------|
| `base:dirt` | Dirt (can grow grass) |
| `base:grass` | Grass block |
| `base:stone` | Stone |
| `base:cobblestone` | Cobblestone |
| `base:sand` | Sand (falls) |
| `base:snow` | Snow |
| `base:water` | Water (non-solid) |
| `base:wood` | Wood (tree trunk) |
| `base:leaves` | Leaves |
| `base:glass` | Glass (transparent) |

---

## Entity Categories

| Category | Description |
|----------|-------------|
| `"player"` | Human-controlled character |
| `"animal"` | AI creature (pig, chicken, dog, villager) |
| `"item"` | Dropped item on the ground |

---

## Full Examples

### Pig (wander + flee from players)
```python
from modcraft_engine import Idle, Wander, Flee

def decide(self, world):
    players = [e for e in world["nearby"] if e.category == "player"]
    if players:
        closest = min(players, key=lambda e: e.distance)
        if closest.distance < 5.0:
            self["goal"] = "Fleeing!"
            return Flee(closest.id, speed=self["walk_speed"] * 1.8)

    self["goal"] = "Wandering"
    return Wander(speed=self["walk_speed"])
```

### Dog (follow nearest player or villager)
```python
from modcraft_engine import Idle, Wander, Follow

def decide(self, world):
    players = [e for e in world["nearby"] if e.category == "player"]
    villagers = [e for e in world["nearby"] if e.type_id == "base:villager"]

    target = None
    if players:
        target = min(players, key=lambda e: e.distance)
    elif villagers:
        target = min(villagers, key=lambda e: e.distance)

    if not target:
        self["goal"] = "Looking for someone"
        return Wander(speed=self["walk_speed"] * 0.5)

    if target.distance < 3.0:
        self["goal"] = "Sitting"
        return Idle()

    self["goal"] = "Following"
    return Follow(target.id, speed=4.0, min_distance=3.0)
```

### Villager (find trees, walk to them, chop)
```python
from modcraft_engine import Idle, Wander, MoveTo

_state = "searching"
_timer = 0
_tree = None

def decide(self, world):
    global _state, _timer, _tree
    _timer -= world["dt"]

    if _state == "searching":
        wood = [b for b in world["blocks"] if b["type"] == "base:wood"]
        if wood:
            _tree = min(wood, key=lambda b: b["distance"])
            _state = "walking"
            _timer = 8.0
            self["goal"] = "Found a tree!"
        else:
            self["goal"] = "Searching for trees"
        return Wander(speed=self["walk_speed"] * 0.7)

    if _state == "walking":
        dx = self["x"] - _tree["x"]
        dz = self["z"] - _tree["z"]
        dist = (dx*dx + dz*dz) ** 0.5
        if dist < 2.5:
            _state = "chopping"
            _timer = 2.0
        elif _timer <= 0:
            _state = "searching"
        self["goal"] = "Walking to tree"
        return MoveTo(_tree["x"]+0.5, _tree["y"], _tree["z"]+0.5,
                       speed=self["walk_speed"])

    if _state == "chopping":
        self["goal"] = "Chopping!"
        if _timer <= 0:
            _state = "resting"
            _timer = 3.0
        return Idle()

    if _state == "resting":
        self["goal"] = "Taking a break"
        if _timer <= 0:
            _state = "searching"
        return Idle()

    return Idle()
```
