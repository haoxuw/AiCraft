# ModCraft — Actions

Actions are the **only way** to mutate world state. Objects propose actions;
the server validates and executes them. This gives us validation, atomicity,
networking, and moddability.

See `docs/28_MATERIAL_VALUE.md` for the value conservation principle that
underpins action validation.

---

## 1. Four Primitive Action Types

The server validates exactly four kinds of actions. All gameplay, no matter
how complex, compiles down to these primitives. Python behaviors compose
them — the server only enforces invariants.

```
MoveTo      — move entity to position (no tunneling through blocks)
TakeItem    — move item between inventories (no creation from nothing)
ConvertItem — transform items in inventory (no value increase)
DestroyItem — remove items permanently (value decreases — always allowed)
```

High-level semantic actions (Wander, Follow, Flee, Chop, Cook, Attack) are
*Python-level strategies* that emit one or more of these primitives. The server
has no knowledge of "chopping" or "cooking" — it only sees ConvertItem proposals
and checks that value doesn't increase.

---

## 2. Action Lifecycle

```
Python behavior: decide() → PyAction
       |
       ↓
Agent client: BehaviorAction → ActionProposal (one of 4 types)
       |
       ↓  (TCP C_ACTION)
Server validates:
  MoveTo      → path clear?
  TakeItem    → source has item?
  ConvertItem → to_value * count ≤ from_value * count?
  DestroyItem → source has item?
       |
       ↓ (pass)
Apply mutation to world state
       |
       ↓
Broadcast delta to all clients (S_ENTITY, S_BLOCK, S_INVENTORY)
```

---

## 3. Inventory References

`TakeItem`, `ConvertItem`, and `DestroyItem` all operate on **inventory
references**. An inventory ref identifies where items live:

| Ref Type | Format | What It Points To |
|----------|--------|------------------|
| Entity inventory | `EntityId` | Items held by an entity (player, villager, chest) |
| Block position | `{x, y, z}` | A placed block treated as a 1-slot inventory |
| HP pool | `{entity: id, slot: "hp"}` | Entity hit points (value = hp * hp_value) |
| Ground | implicit | Dropped item entities at actor's feet |

This unified model means "break a block", "pick up an item", "deal damage", and
"store items in a chest" are all variants of the same three action types.

---

## 4. Python API

Python behaviors import from `modcraft_engine`:

```python
from modcraft_engine import MoveTo, TakeItem, ConvertItem, DestroyItem
```

### MoveTo
```python
# Move entity toward a position at a given speed
MoveTo(x, y, z, speed=2.0)
```

### TakeItem
```python
# Take item from another entity (chest, dropped item, creature)
TakeItem(from_entity_id, item_id, count=1)

# Take item from a world block position (block → inventory)
TakeItem(from_pos=(x, y, z), item_id="base:wood_block", count=1)

# Put item into another entity's inventory (store in chest)
TakeItem(from_entity_id=self_id, to_entity_id=chest_id, item_id="base:log", count=all)
```

### ConvertItem
```python
# Convert items in actor's inventory (server checks: to_value ≤ from_value)
ConvertItem(from_item="base:wood_block", from_count=1,
            to_item="base:log", to_count=1)

# Spend HP to produce an item (chicken laying an egg)
ConvertItem(from_item="hp", from_count=4,
            to_item="base:egg", to_count=1)

# Place a block (item → world position)
ConvertItem(from_item="base:log", from_count=1,
            to_block_pos=(x, y, z), to_block="base:wood_block")
```

### DestroyItem
```python
# Attack: deal damage to entity (destroy HP value)
DestroyItem(target_entity=enemy_id, item="hp", count=5)

# Consume an item (eat, drink, burn)
DestroyItem(target_entity=self_id, item="base:potion", count=1)
```

---

## 5. Behavior Examples

### Wander (Python implements target selection)
```python
# behavior_base.py provides wander_target() helper
from behavior_base import Behavior
from modcraft_engine import MoveTo

class WanderBehavior(Behavior):
    def decide(self, entity, world):
        target = self.wander_target(entity, radius=8)
        return MoveTo(*target, speed=entity["walk_speed"]), "Wandering"
```

### Follow / Flee (Python does the math)
```python
def decide(self, entity, world):
    for e in world["nearby"]:
        if e["category"] == "player" and e["distance"] < 8:
            # Flee: move in opposite direction
            dx = entity["x"] - e["x"]
            dz = entity["z"] - e["z"]
            dist = (dx*dx + dz*dz) ** 0.5 or 1
            tx = entity["x"] + dx/dist * 12
            tz = entity["z"] + dz/dist * 12
            return MoveTo(tx, entity["y"], tz, speed=4.0), "Fleeing!"
    return MoveTo(*self.wander_target(entity, 8)), "Wandering"
```

### Woodcutter (ConvertItem for chopping)
```python
def decide(self, entity, world):
    blocks = [b for b in world["blocks"] if b["type"] == "base:wood"]
    if blocks:
        b = min(blocks, key=lambda x: x["distance"])
        if b["distance"] < 2.5:
            return (ConvertItem("base:wood_block", 1, "base:log", 1,
                                block_pos=(b["x"], b["y"], b["z"])),
                    "Chopping!")
        return MoveTo(b["x"], b["y"], b["z"]), "Walking to tree"
    return MoveTo(*self.wander_target(entity, 20)), "Searching..."
```

### Attack
```python
def decide(self, entity, world):
    enemies = [e for e in world["nearby"]
               if e["category"] == "hostile" and e["distance"] < 3]
    if enemies:
        target = min(enemies, key=lambda e: e["distance"])
        damage = entity.get("damage", 3)
        return (DestroyItem(target_entity=target["id"], item="hp", count=damage),
                "Attacking!")
    return MoveTo(*self.wander_target(entity, 10)), "Patrolling"
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
MoveTo:       value unchanged  (position change only)
TakeItem:     value conserved  (item relocated, not created)
ConvertItem:  value ≤ before   (transformation; server rejects if value would increase)
DestroyItem:  value decreases  (always allowed)
```

New value enters the world only via: HP regeneration, chunk generation,
and (future) plant growth. These are world-system events, not player actions.
