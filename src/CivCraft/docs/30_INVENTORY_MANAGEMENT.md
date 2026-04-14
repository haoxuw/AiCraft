# Inventory Management

How inventories work end-to-end: data model, entity ownership, chest
interaction, agent client access, Python behavior API, network protocol,
and persistence.

---

## Data Model

Inventories use a **counter model** — no grid, no slots. Each inventory is a
`string → int` map of item counts.

```
src/shared/inventory.h
```

| Method | Description |
|--------|-------------|
| `add(id, n)` | Increase count |
| `remove(id, n)` | Decrease count (clamp to 0) |
| `count(id)` | Current count |
| `has(id, n=1)` | True if count >= n |
| `items()` | Sorted vector of `{string, int}` pairs |
| `clear()` | Remove all items |
| `equip(slot, id)` | Put item into wear slot |

There are no stack limits, no slot management. Items are displayed sorted
by ID, skipping zero-count entries. Think `collections.Counter`.

### Hotbar

The hotbar is a **client-side concept only** — a list of 10 shortcut aliases
pointing into the counter. The server does not track or validate hotbar
assignments. Clients may sort, reorder, or auto-populate the hotbar however
they like. The hotbar is **not** part of the network protocol.

### Equipment (Wear Slots)

Equipment is part of the inventory. `equip(slot, id)` moves an item from
the counter into a wear slot; `unequip(slot)` returns it to the counter.
Equipment state is included in `S_INVENTORY` broadcasts.

---

## Owner Categories

Inventory is allocated for two entity kinds:

| Entity Kind | When inventory is allocated | Key |
|-------------|---------------------------|-----|
| **Living** (player, Creatures, animal) | Always — every Living entity gets an inventory | EntityId |
| **Structure** with `has_inventory=true` (e.g. Chest) | Only if the EntityDef sets `has_inventory` | EntityId |

**Item entities do NOT have inventories.** They represent a single dropped
item on the ground (type + count stored as entity props).

### Chests Are Structure Entities

Chests are `EntityKind::Structure` with `has_inventory = true`. Their
inventory lives on `Entity::inventory`, the same `unique_ptr<Inventory>`
that Living entities use. This means:

- Chest inventories are accessed by **entity ID**, not block position
- The `StructureBlockCacher` maps block position → entity ID, so
  right-clicking a chest block resolves to the Structure entity
- All inventory operations (read, transfer, persist) use the same code
  path for Living and Structure entities
- `S_INVENTORY` works for both — same message, same format

---

## The Four Action Primitives and Inventory

The server accepts exactly four action types (Rule 0). All inventory
changes flow through these:

### TYPE_RELOCATE (1) — Move items between containers

This is the primary inventory action. Containers are:

| Container | Meaning |
|-----------|---------|
| `Self()` | The actor entity's own inventory |
| `Ground()` | World ground (spawn / despawn item entity) |
| `Entity(id)` | Another entity's inventory (Living or Structure) |
| `Block(x,y,z)` | A block position (legacy, used for mining/placing) |

Relocate flows handled by the server (`server.cpp`):

| From | To | Effect |
|------|----|--------|
| `Entity(item_id)` | `Self()` | **Pickup**: take item entity from ground into inventory. Range check: `pickup_range` (default 1.5 blocks). |
| `Self()` | `Ground()` | **Drop**: remove from inventory, spawn item entity at feet. |
| `Self()` | `Entity(chest_id)` | **Store**: deposit items into chest entity's inventory. If `itemId` is set, transfers that item × `itemCount`; if empty, transfers all items. Range check: 2.5 blocks. |
| `Entity(chest_id)` | `Self()` | **Take**: withdraw `itemId` × `itemCount` from chest entity's inventory into actor's inventory. Range check: 2.5 blocks. |
| `Self()` | `Self()` (equip) | **Equip**: move item to a wear slot (Offhand, RightHand, etc.) |

### TYPE_CONVERT (2) — Transform items

Used for crafting, consuming, and block breaking/placing. Value must be
conserved (items cannot be created from nothing).

| Pattern | Effect |
|---------|--------|
| `Block → Ground` | **Break block**: remove block, drop result |
| `Block → Self` | **Mine into inventory**: remove block, add drop to actor |
| `Self → Block` | **Place block**: consume item, set block at position |
| `Self → Self` | **Use/consume**: e.g. eat apple → restore HP |

### TYPE_MOVE (0) — Set velocity

No inventory effect. Used for walking, stopping.

### TYPE_INTERACT (3) — Toggle block state

No inventory effect. Used for doors, buttons, TNT fuse.

---

## Python Behavior API

### Reading inventory

Inside `decide(entity, local_world)`:

```python
entity.inventory.count("base:trunk")   # how many logs carried
entity.inventory.items                  # {"base:trunk": 5, "base:apple": 2}
bool(entity.inventory)                  # True if carrying anything
```

`entity.inventory` is an `InventoryView` (pydantic model, `python/local_world.py`).
It is a read-only snapshot rebuilt each tick from the C++ bridge.

### Writing inventory (via actions)

Behaviors **never modify inventory directly**. They return an action, and
the server validates and executes it. All wrappers are in `python/actions.py`:

```python
from actions import PickupItem, DropItem, StoreItem, TakeItem, BreakBlock
from civcraft_engine import Relocate, Convert, Self, Ground, Entity, Block

# Pick up item entity from ground
PickupItem(entity_id)
# → Relocate(relocate_from=Entity(entity_id))

# Drop item from inventory
DropItem("base:trunk", count=3)
# → Relocate(relocate_to=Ground(), item_id="base:trunk", count=3)

# Deposit specific items into a chest (by chest entity ID)
StoreItem(chest_entity_id, item_id="base:trunk", count=5)
# → Relocate(relocate_to=Entity(chest_entity_id), item_id="base:trunk", count=5)

# Deposit ALL items into a chest
StoreItem(chest_entity_id)
# → Relocate(relocate_to=Entity(chest_entity_id))

# Take items from a chest
TakeItem(chest_entity_id, item_id="base:trunk", count=3)
# → Relocate(relocate_from=Entity(chest_entity_id), item_id="base:trunk", count=3)

# Mine block (drop result on ground, auto-pickup by proximity)
BreakBlock(x, y, z)
# → Convert(from_item="", convert_from=Block(x,y,z), convert_into=Ground())

# Direct Relocate for fine control
Relocate(
    relocate_from=Self(),
    relocate_to=Entity(chest_entity_id),
    item_id="base:trunk",
    count=3,
)
```

### Chest entity ID resolution

Behaviors that interact with chests discover them dynamically at decide()
time via `scan_entities("base:chest", near=..., max_dist=..., max_results=1)`.
The server does NOT pre-assign a chest to any NPC — any chest in range is
valid, including chests placed by players after world generation.

```python
hits = scan_entities("base:chest", near=(entity.x, entity.y, entity.z),
                     max_dist=120, max_results=1)
if hits:
    chest_eid = int(hits[0]["id"])
    return StoreItem(chest_eid), "Depositing"
```

### Woodcutter example (`artifacts/behaviors/base/woodcutter.py`)

The woodcutter demonstrates the full inventory cycle:

```
1. WORK state    → scan_blocks("base:logs", near=entity.pos) to find trees
                 → Convert(from_item="base:logs", convert_from=Block(x,y,z))
                   Server: block removed, "base:logs" added to actor inventory

2. DEPOSIT state → scan_entities("base:chest", near=entity.pos) to find a chest
                 → Walk within STORE_RANGE (3 blocks), then StoreItem(chest_eid)
                   Server: all items moved from actor to chest entity inventory
                   Server: broadcasts S_INVENTORY for both actor and chest entity

3. Agent receives S_INVENTORY → rebuilds entity.inventory for next decide()
   → logs == 0 → state transitions back to WORK
```

Key properties set at spawn (see `server.h` initWorld):
- `collect_goal` — logs to collect before depositing (default 5)
- `work_radius` — max scan_blocks distance for trees (default 80)
- `chop_period` — seconds between chop actions (default 0.5)

---

## Network Protocol

### S_INVENTORY — Universal inventory broadcast

A single message type for all inventory owners (Living and Structure):

```
S_INVENTORY (0x1007)  S→C
  [u32 entityId]
  [u32 itemCount]
  For each item: [str itemId][i32 count]
  [u8 equipCount]
  For each equip: [str slotName][str itemId]
```

Sent whenever an entity's inventory changes (after any successful Relocate
or Convert action). The server broadcasts to all connected clients.

For Structure entities (e.g. chests), `equipCount` is 0. For Living entities,
equipment slots are included.

**Hotbar is NOT in the wire format.** Clients manage hotbar locally.

### C_GET_INVENTORY — Request inventory snapshot

```
C_GET_INVENTORY (0x000D)  C→S
  [u32 entityId]
```

Client requests the current inventory of any entity. Server validates that
the entity exists, has an inventory, and the requesting player is within
range (6 blocks), then responds with `S_INVENTORY`.

Used by the GUI client when the player right-clicks a chest: the client
resolves the chest block position to a Structure entity ID (via the entity
list or a block→entity lookup), then sends `C_GET_INVENTORY`.

### No C_CHEST_MOVE, no S_CHEST_OPEN, no S_CHEST_CLOSE

All item transfers use `C_ACTION` with a Relocate action proposal. The
server validates and executes the Relocate, then broadcasts `S_INVENTORY`
for all affected entities. The client opens/closes the chest UI purely
as local state — the server does not track which chests are "open".

### Inventory update broadcast

When a Relocate or Convert changes an entity's inventory, the server fires
`onInventoryChange(entityId, inventory)`. The callback serializes and
broadcasts `S_INVENTORY` to all clients.

For chest interactions, this means both the player's and the chest entity's
`S_INVENTORY` are broadcast after each transfer, keeping all nearby clients
in sync without any viewer tracking.

### Agent client (headless Creatures)

Agent clients receive `S_INVENTORY` and update their local entity cache:

```
agent_client.h
  case S_INVENTORY:
    → parse entityId, items
    → entity->inventory->clear()
    → entity->inventory->add(id, count) for each item
```

Agent clients do **not** use `C_GET_INVENTORY`. NPCs store/take items via
the Relocate action directly, and the server broadcasts updated inventories.

---

## Chest UI (Client-Side)

Opening and closing the chest UI is **purely client-side state**. The server
has no knowledge of which chests any client has "open" (Rule 5: server has
no display logic).

### Flow

```
Player right-clicks chest block
  → Client resolves block pos → Structure entity ID (via StructureBlockCacher)
  → Client sends C_GET_INVENTORY(chest_entity_id)
  → Server validates range, sends S_INVENTORY(chest_entity_id, items...)
  → Client opens chest UI panel, displaying chest + player inventory

Player clicks item in chest to take it
  → Client sends C_ACTION: Relocate(from=Entity(chest_id), to=Self(), item="base:trunk", count=5)
  → Server validates (chest has item? player in range?)
  → Server: chestInv.remove("base:trunk", 5), playerInv.add("base:trunk", 5)
  → Server broadcasts S_INVENTORY for chest entity (updated contents)
  → Server broadcasts S_INVENTORY for player entity (updated inventory)
  → Client updates both panels

Player clicks item in player inventory to store it
  → Client sends C_ACTION: Relocate(from=Self(), to=Entity(chest_id), item="base:apple", count=3)
  → Server validates, transfers, broadcasts S_INVENTORY for both entities

UI closes when:
  → Player presses Esc (client-side)
  → Player walks >6 blocks from chest (client-side distance check)
  → Player clicks X button (client-side)
```

### Interactions

| Action | Effect |
|--------|--------|
| Left-click item in chest | Move full stack to player inventory |
| Left-click item in player inv | Move full stack to chest |
| Shift + left-click | Move half the stack (round up) |
| Ctrl + left-click | Move exactly 1 |

All interactions send `C_ACTION` with a Relocate. The server validates
and responds with `S_INVENTORY` updates.

---

## Persistence

### Entity inventories (via entity serialization)

Since both Living and Structure entities use `Entity::inventory`, all
inventories are persisted through entity serialization. Player inventories
are additionally saved per character skin for cross-session persistence.

### Player inventories (`inventories.bin`)

Saved per character skin (not per session):

```
[u32 count]
For each character:
  [string skin_name]
  [u32 item_count]
  For each item: [string item_id][i32 count]
  [u8 equip_count]
  For each equip: [string slot_name][string item_id]
```

On player join: if `m_savedInventories` has an entry for the character's
skin, restore it to the new player entity. On world save: snapshot all
connected players' inventories.

**Hotbar is not persisted server-side.** Clients may persist their own
hotbar assignments locally.

### Structure inventories (via entity persistence)

Chest inventories are persisted as part of the Structure entity. When the
world saves, each Structure entity with an inventory has its items written
as part of entity data. No separate `chest_inventories.bin` — it's all
unified through entity serialization.

Breaking a chest (Structure anchor destroyed): the entity is removed,
its inventory contents are dropped as item entities.

---

## Validation Summary

All inventory operations are **server-authoritative**. The server checks:

| Check | Where |
|-------|-------|
| Actor has the item | `actor->inventory->has(id)` before remove |
| Pickup range | `dist <= pickup_range` (default 1.5) |
| Store/take range | `dist <= 2.5` blocks to chest entity |
| Target has inventory | `target->inventory != nullptr` |
| Item entity exists | `src && !src->removed` |
| Block exists | `bid != BLOCK_AIR` |
| Value conservation | Convert cannot create value from nothing |

Rejected actions are logged with `ActionRejectCode` and the actor's
inventory is re-broadcast (`S_INVENTORY`) to correct any optimistic
client-side state.
