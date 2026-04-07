# Material Value Conservation

## The Core Invariant

**No action can increase the total material value in the world.**

Every object in the world — blocks placed in the terrain, items in inventories,
entity hit points, dropped items on the ground — carries an intrinsic `value`
field. The server enforces that every action either conserves or decreases total
value. It never increases it.

This makes every resource meaningful. Wood logs can't be created from nothing.
HP can't be conjured without a source. Every conversion has a cost.

---

## What Has Value?

| Object | Value Source | Example |
|--------|-------------|---------|
| Block in world | `value` field in block artifact | `base:wood` block: 10 |
| Item in inventory | `value` field in item artifact | `base:log`: 10 |
| Entity HP | `hp_value` field in creature artifact | 1 HP = 0.5 |
| Dropped item entity | Same as item | — |

Values are defined in Python artifacts and loaded by the server at startup.
They are immutable at runtime — only artifact files can change them.

### Artifact Format

```python
# artifacts/items/base/log.py
item = {
    "id": "base:log",
    "name": "Log",
    "value": 10,       # material value units
    ...
}

# artifacts/blocks/base/wood.py
block = {
    "id": "base:wood",
    "value": 10,       # same value as log — breaking/placing is even
    ...
}

# artifacts/creatures/base/chicken.py
creature = {
    "id": "base:chicken",
    "max_hp": 6,
    "hp_value": 0.5,   # each HP point is worth 0.5 value units
    ...
}
```

---

## Value Sources (How New Value Enters the World)

The invariant says *actions* can't increase value. But the world would drain to
zero without value creation. Value enters via:

1. **HP regeneration** — a chicken regenerating HP from 2→3 creates 0.5 value.
   This is the primary renewable resource mechanism.

2. **Chunk generation** — when a new chunk is generated, blocks are placed into
   the world. This is the "big bang" value injection at world creation time.

3. *(Future)* **Plant growth, farming** — seeds → crops, sapling → tree.

These are world-system events, not player actions, and they are explicitly
allowed to increase value.

---

## The 4 Primitive Actions

All world mutations go through exactly 4 server-validated action types:

### 1. MoveTo
```
MoveTo(entityId, targetPos, speed)
```
Validation: path doesn't pass through solid blocks (no tunneling).
Value effect: none. Moving doesn't create or destroy value.

### 2. TakeItem
```
TakeItem(actorId, fromRef, toRef, itemId, count)
```
Moves items between any two inventory references without creating new ones.
An `InventoryRef` can be:
- Entity ID (entity's item inventory — includes chest entities, dropped items)
- Block position (block at xyz treated as a single-slot inventory)
- `"ground"` (spawn/despawn dropped-item entity)

Validation: source ref has ≥ `count` of `itemId`. Neither creates nor destroys;
total value is conserved exactly.

### 3. ConvertItem
```
ConvertItem(actorId, fromItemId, fromCount, toItemId, toCount)
```
Transforms items in the actor's inventory. Any conversion is allowed as long as:
```
value(toItemId) * toCount  ≤  value(fromItemId) * fromCount
```
Use cases:
- Break block: `ConvertItem("base:wood_block", 1, "base:log", 1)` — same value
- Cook meat: `ConvertItem("base:raw_meat", 1, "base:cooked_meat", 1)` — must define cooked_meat.value ≤ raw_meat.value
- Lay egg: `ConvertItem("hp", 4, "base:egg", 1)` — spend 4 HP to produce 1 egg
- Place block: `ConvertItem("base:log", 1, "base:wood_block", 1)` — same value

Python behavior decides *when* to send ConvertItem (e.g., only near a furnace
for cooking). The server only checks value conservation. No other preconditions.

### 4. DestroyItem
```
DestroyItem(actorId, targetRef, itemId, count)
```
Removes items from an inventory reference permanently. Always allowed — value
decreases, which satisfies the invariant.
Use cases:
- Attack: `DestroyItem(attacker, entityId, "hp", 5)` — deal 5 HP damage
- Consume item: `DestroyItem(actor, self, "base:potion", 1)` — drink potion
- Burn: `DestroyItem(actor, blockPos, "base:wood_block", 1)` — block catches fire

---

## Conservation Examples

### Woodcutter chops a tree
```python
# Python decides: actor is adjacent to wood block, has axe equipped
ConvertItem("base:wood_block", 1, "base:log", 1)   # value: 10 → 10 ✓
```

### Cook raw meat (requires Python to check furnace proximity)
```python
# Python only sends this if actor is within 2 blocks of a furnace
ConvertItem("base:raw_meat", 1, "base:cooked_meat", 1)  # value: 3 → 3 ✓
```

### Chicken lays an egg (spending HP)
```python
# hp_value=0.5, egg.value=2.0; need 4 HP to lay 1 egg
ConvertItem("hp", 4, "base:egg", 1)   # value: 2.0 → 2.0 ✓
```

### Attack (destroying HP)
```python
DestroyItem(attacker_id, target_entity, "hp", 5)   # value: 2.5 → 0 ✓ (decrease)
```

### Invalid (server rejects)
```python
ConvertItem("base:dirt", 1, "base:diamond", 1)  # value: 1 → 100 ✗ REJECTED
```

---

## What the Server Validates

| Action | Server Check |
|--------|-------------|
| MoveTo | No solid block on path within range |
| TakeItem | Source has the item; actor within reach range |
| ConvertItem | `to_value * to_count ≤ from_value * from_count` |
| DestroyItem | Source has the item |

The server does **not** check:
- Whether a furnace is nearby for cooking
- Whether the actor has the right tool for the job
- Whether a recipe "makes sense"

All prerequisite logic lives in Python behaviors. Python is trusted to only
propose valid actions given world context. The server is the last line of
anti-cheat defense — it verifies the mathematical invariant only.
