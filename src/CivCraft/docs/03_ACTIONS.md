# CivCraft — Actions

Actions are the **only way** to mutate world state. Objects propose actions;
the server validates and executes them. This gives us validation, atomicity,
networking, and moddability.

See `docs/28_MATERIAL_VALUE.md` for the value conservation principle that
underpins action validation.

---

## 1. Action Types

The server validates exactly these action kinds. All gameplay compiles down to
them. Python behaviors compose them — the server only enforces invariants.

```
MoveTo        — move entity to position (no tunneling through blocks)
Relocate      — move items between inventories (no creation from nothing)
Convert — transform a block or entity in the world (value conservation enforced)
Interact — interact with a block (open door, button, etc.)
```

High-level strategies (wandering, following, fleeing, chopping, cooking) are
*Python-level* logic that emits these primitives. The server has no knowledge
of "following" or "fleeing" — it only sees `MoveTo` proposals and checks that
the path is clear.

---

## 2. Action Lifecycle

```
Python behavior: decide() → action object
       |
       ↓
Agent client: BehaviorAction → ActionProposal (TCP C_ACTION)
       |
       ↓
Server validates:
  MoveTo        → path clear?
  Relocate      → source has item?
  Convert → to_value * count ≤ from_value * count?
  Interact → block interactable?
       |
       ↓ (pass)
Apply mutation to world state
       |
       ↓
Broadcast delta to all clients (S_ENTITY, S_BLOCK, S_INVENTORY)
```

---

## 3. Inventory References

`Relocate` and `Convert` operate on **inventory references** that
identify where items live:

| Ref Type | Format | What It Points To |
|----------|--------|------------------|
| Entity inventory | `EntityId` | Items held by an entity (player, villager, chest) |
| Block position | `{x, y, z}` | A placed block treated as a 1-slot inventory |
| HP pool | `{entity: id, slot: "hp"}` | Entity hit points (value = hp * hp_value) |
| Ground | implicit | Dropped item entities at actor's feet |

This unified model means "break a block", "pick up an item", "deal damage", and
"store items in a chest" are all variants of the same action types.

---

## 4. Python API

Python behaviors have two APIs: **write actions** (sent to server) and **read queries**
(local only, never touch the server).

### Read query — `get_block`

```python
from civcraft_engine import get_block

# Query block type at world position — read-only, agent-side only, never sent to server
block_id = get_block(x, y, z)   # returns e.g. "base:trunk", "base:air"
```

`get_block` probes the agent's local chunk cache. It is NOT one of the four action types
and cannot change world state.

### Write actions (the four server primitives)

```python
from civcraft_engine import MoveTo, Relocate, Convert, Interact
from actions import StoreItem, PickupItem, DropItem, BreakBlock  # thin Python wrappers
```

### MoveTo
```python
# Move entity toward a position at a given speed
MoveTo(x, y, z, speed=2.0)
```

### Convert
```python
# Chop / mine a block (block → inventory item)
Convert(from_item="base:trunk", to_item="base:trunk",
              block_pos=(bx, by, bz), convert_from_block=True, direct=True)

# Attack: deal damage to another entity
Convert(from_item="base:sword", to_item="base:sword",
              convert_from_entity=target_id, direct=True)
```

### Inventory actions (Python wrappers in `actions.py`)
```python
# Store all inventory into a chest (must be within 2 blocks of chest)
StoreItem(chest_entity_id)  # = Relocate(to_entity=chest_entity_id)

# Pick up a dropped item entity
PickupItem(item_entity_id)

# Drop an item from inventory at feet
DropItem(item_type="base:trunk", count=1)

# Mine a block (shortcut for Convert with convert_from_block=True)
BreakBlock(x, y, z)
```

### Interact
```python
# Open a door, press a button, etc.
Interact(x, y, z)
```

---

## 5. Behavior Examples

### Wander (Behavior base class provides helper)
```python
from behavior_base import Behavior
from civcraft_engine import MoveTo

class WanderBehavior(Behavior):
    def decide(self, entity, world):
        tx, ty, tz = self.wander_target(entity, radius=8)
        return MoveTo(tx, ty, tz, speed=entity.walk_speed), "Wandering"
```

### Flee (Python computes direction, returns MoveTo)
```python
from civcraft_engine import MoveTo

def decide(self, entity, world):
    player = world.nearest("player", max_dist=8)
    if player:
        dx = entity.x - player.x
        dz = entity.z - player.z
        dist = (dx*dx + dz*dz) ** 0.5 or 1
        return MoveTo(entity.x + dx/dist * 12, entity.y,
                      entity.z + dz/dist * 12, speed=4.0), "Fleeing!"
    tx, ty, tz = self.wander_target(entity, radius=8)
    return MoveTo(tx, ty, tz, speed=entity.walk_speed), "Wandering"
```

### Woodcutter (Convert for chopping)
```python
from civcraft_engine import MoveTo, Convert

def decide(self, entity, world):
    trunk = world.get("base:trunk", max_dist=80)
    if trunk:
        if trunk.distance < 2.5:
            return (Convert(from_item="base:trunk", to_item="base:trunk",
                                  block_pos=(trunk.x, trunk.y, trunk.z),
                                  convert_from_block=True, direct=True),
                    "Chopping!")
        return MoveTo(trunk.x, trunk.y, trunk.z), "Walking to tree"
    tx, ty, tz = self.wander_target(entity, radius=20)
    return MoveTo(tx, ty, tz), "Searching..."
```

---

## 6. Infrastructure Actions (Not Gameplay)

One additional action exists outside the 4 primitives — it is infrastructure,
not gameplay, and is not subject to value conservation:

```
ReloadBehavior — hot-swap Python behavior source code for an entity
```

This is sent by the GUI code editor and is handled server-side by forwarding
the new source to the entity's agent client process.

---

## 7. Value Conservation Summary

See `docs/28_MATERIAL_VALUE.md` for full details.

```
MoveTo:        value unchanged  (position change only)
Relocate:      value conserved  (item relocated, not created)
Convert: value ≤ before   (transformation; server rejects if value would increase)
Interact: value unchanged  (state toggle only)
```

New value enters the world only via: HP regeneration, chunk generation,
and (future) plant growth. These are world-system events, not player actions.
