# Material Value Conservation

## The Core Invariant

**No action can increase the total material value in the world.**

Every object in the world — blocks placed in the terrain, items in inventories,
entity hit points, dropped items on the ground — carries an intrinsic material
value. The server enforces that every action either conserves or decreases total
value. It never increases it.

This makes every resource meaningful. Wood logs can't be created from nothing.
HP can't be conjured without a source. Every conversion has a cost.

---

## What Has Value?

| Object | Value Source | Example |
|--------|-------------|---------|
| Block in world | `src/shared/material_values.h` | `base:wood`: 4.0 |
| Item in inventory | `src/shared/material_values.h` | `base:sword`: 15.0 |
| Entity HP | fixed at 1.0 (same scale as dirt) | 1 HP = 1.0 |
| Dropped item entity | Same as item type | — |

Material values are the **single source of truth** in `src/shared/material_values.h`.
Any type not listed defaults to 1.0. Artifact files do not carry a `value` field —
they describe appearance, behavior, and stats. Values are a separate concern.

### Value Table (excerpt from `src/shared/material_values.h`)

```cpp
{"base:dirt",    1.0f},   // reference unit
{"base:trunk",   4.0f},   // tree trunk block
{"base:wood",    4.0f},   // placed wood block
{"base:chest",   6.0f},
{"base:sword",  15.0f},
{"base:apple",   2.0f},
{"hp",           1.0f},   // 1 HP = 1.0 (enables item↔HP conversions)
// anything not listed → 1.0 (default)
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

### 1. Move
```
Move(x, y, z, speed)
```
Validation: path doesn't pass through solid blocks (no tunneling).
Value effect: none. Moving doesn't create or destroy value.

### 2. Relocate
```
Relocate(relocate_from=Container, relocate_to=Container, item_id, count)
```
Moves items between any two containers without creating new ones.
A `Container` is one of: `Self()`, `Ground()`, `Entity(id)`, `Block(x,y,z)`.

Validation: source container has ≥ `count` of `item_id`. Neither creates nor
destroys; total value is conserved exactly.

### 3. Convert
```
Convert(from_item, from_count, to_item, to_count,
        convert_from=Container, convert_into=Container)
```
Transforms items. Any conversion is allowed as long as:
```
value(to_item) * to_count  ≤  value(from_item) * from_count
```
Use cases:
- Break block: `Convert(convert_from=Block(x,y,z), convert_into=Ground())` — block → dropped item
- Lay egg: `Convert("hp", 2, "base:egg", 1)` — spend 2 HP (value 2.0) → 1 egg (value 1.0) ✓
- Eat apple: `Convert("base:apple", 1, "hp", 2)` — 1 apple (value 2.0) → 2 HP ✓

Python behavior decides *when* to send Convert. The server only checks value
conservation. No other preconditions.

### 4. Interact
```
Interact(block_x, block_y, block_z)
```
Toggles interactive block state (door open/close, TNT fuse, button press).
Value effect: none.

---

## Conservation Examples

### Woodcutter chops a tree
```python
from civcraft_engine import Convert, Block, Ground
# value: 4.0 (trunk) → 4.0 (dropped trunk item) ✓
Convert(convert_from=Block(tx, ty, tz), convert_into=Ground())
```

### Chicken lays an egg (spending HP)
```python
from civcraft_engine import Convert
# value: 2.0 (2 HP × 1.0) → 1.0 (1 egg × 1.0) ✓ (decrease allowed)
Convert("hp", 2, "base:egg", 1)
```

### Eat apple (healing HP)
```python
from civcraft_engine import Convert
# value: 2.0 (1 apple × 2.0) → 2.0 (2 HP × 1.0) ✓
Convert("base:apple", 1, "hp", 2)
```

### Attack (destroying HP — value decreases)
```python
from civcraft_engine import Convert, Entity
# value: 5.0 (5 HP) → 0 (destroy) ✓ (decrease allowed)
Convert("hp", 5, "", 0, convert_from=Entity(target_id))
```

### Invalid (server rejects)
```python
Convert("base:dirt", 1, "base:sword", 1)  # value: 1.0 → 15.0 ✗ REJECTED
```

---

## What the Server Validates

| Action | Server Check |
|--------|-------------|
| Move | No solid block on path within range |
| Relocate | Source container has the item; actor within reach range |
| Convert | `value(to_item) * to_count ≤ value(from_item) * from_count` |
| Interact | Block at position is interactive |

The server does **not** check:
- Whether a furnace is nearby for cooking
- Whether the actor has the right tool for the job
- Whether a recipe "makes sense"

All prerequisite logic lives in Python behaviors. Python is trusted to only
propose valid actions given world context. The server is the last line of
anti-cheat defense — it verifies the mathematical invariant only.
