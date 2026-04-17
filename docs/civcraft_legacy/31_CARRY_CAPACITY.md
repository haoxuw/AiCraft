# Carry Capacity & Auto-Pickup

This doc describes how inventory size is bounded, how auto-pickup works, and
how client and server stay consistent with a single source of truth.

---

## Single Source of Truth: Material Values

Every item and block has a numeric **material value**, defined in:

```
src/shared/material_values.h
```

```cpp
{"base:dirt", 1.0f}, {"base:stone", 2.0f}, {"base:logs", 4.0f},
{"base:leaves", 0.5f}, {"base:sword", 15.0f}, ...
```

These values already drive the server's `Convert` value-conservation check
(`outVal * outCount ≤ inVal * inCount`). They are now the same unit used for
carry capacity.

**Never hardcode values in Python.** Behaviors that need a value call
`civcraft_engine.material_value(item_id)`, which binds directly to
`getMaterialValue()` in C++.

---

## Entity Inventory Capacity & Max HP

**Everything is value.** An entity's `inventory_capacity` and `max_hp` are
both its own material value — looked up from the same table in
`src/shared/material_values.h`:

```cpp
{"base:player",   128.0f},
{"base:villager",  10.0f},
{"base:dog",        5.0f},
{"base:pig",        3.0f},
{"base:cat",        3.0f},
{"base:chicken",    2.0f},
```

`src/content/builtin.cpp` calls `getMaterialValue(def.string_id)` at
registration time and stores the result on both
`EntityDef::inventory_capacity` (as a float) and `EntityDef::max_hp` (as
`(int)value`). There are no separate capacity or HP tables, and
`artifacts/living/*.py` must not declare `max_hp` — the value lives in
`material_values.h` alone.

An entity's inventory is valid iff

    sum(count[i] * material_value(id[i])) + worn_items ≤ inventory_capacity

`inventory_capacity == 0` means the entity cannot carry anything.

---

## The `canAccept` Predicate

Shared between client and server — defined **once** in
`src/shared/inventory.h`:

```cpp
bool Inventory::canAccept(const std::string& itemId, int count,
                          float capacity) const;
```

Returns true iff adding `(item × count)` would keep `totalValue()` ≤ capacity.
The Python side has a mirror method on `InventoryView`:

```python
entity.inventory.can_accept("base:logs", 1, entity.inventory_capacity)
```

`InventoryView.can_accept` internally calls
`civcraft_engine.material_value()` — no value table in Python.

---

## Auto-Pickup Protocol (Client-Predicted, Server-Authoritative)

The flow is the same for every carrier (player, NPC):

```
┌──────────────┐                                  ┌──────────────┐
│  Client/Agent│                                  │    Server    │
├──────────────┤                                  ├──────────────┤
│ Each tick:   │                                  │              │
│  scan items  │                                  │              │
│  within      │                                  │              │
│  pickup_range│                                  │              │
│      ↓       │                                  │              │
│ canAccept()? │ ← shared logic (inventory.h)  → │              │
│      ↓ yes   │                                  │              │
│ send         │ ─── Relocate proposal ─────────► │ canAccept()? │
│ Relocate     │                                  │  (re-check)  │
│ (relocate_   │                                  │      ↓ yes   │
│  from=Entity)│                                  │ add to inv,  │
│      │       │                                  │ remove item  │
│      │       │ ◄── S_INVENTORY + S_REMOVE ───── │              │
└──────────────┘                                  └──────────────┘
```

Key properties:

1. **Shared prediction.** Both sides run the same `canAccept`. The client
   skips sending a proposal that would be rejected — no network spam.
2. **Server is authoritative.** The server re-runs `canAccept` before
   mutating state. A client whose local view is stale can be denied.
3. **No new action types.** Auto-pickup is a `Relocate` with
   `relocate_from=Entity(itemEntityId)` and `relocate_to=Self()`. No
   `AutoPickup` or `PickupItem` primitive — it's just the existing Relocate.
4. **Behaviors don't need to implement pickup.** The engine scans and emits
   the proposal at the platform layer, not in each Python behavior.

### Where the client-side scan lives

| Carrier | File | Function |
|---------|------|----------|
| Player  | `src/client/game_playing.cpp` | `Game::updateItemPickupAnimations` |
| NPC     | `src/agent/agent_client.h`    | `AgentClient::phaseAutoPickup`    |

Both iterate `forEachEntity`, filter by `typeId == ItemName::ItemEntity`,
check distance against the actor's `pickup_range`, gate by `canAccept`, and
send a `Relocate` proposal.

### Where the server-side validation lives

```
src/server/server.cpp  — case ActionProposal::Relocate
```

Two paths call `canAccept` before mutating the inventory:
- `ItemEntity` pickup (dropped items)
- Entity inventory take (chest → actor)

If the check fails, the proposal is rejected with
`ActionRejectCode::ItemNotInInventory` (repurposed — consider adding a
dedicated `InventoryFull` code in a follow-up).

---

## Why One-Way Mirrored Logic Is Safe

Both sides consume the same `src/shared/inventory.h` (client links it,
server links it, Python imports the binding). The Python `InventoryView` is
a *method* wrapper that calls the C++ lookup — never a value mirror.

If you add a new item, you add it in exactly one place: the map in
`material_values.h`. The bridge re-exposes it automatically on the next
build; behaviors pick it up without any artifact edits.

---

## Tuning

Capacity is a gameplay knob. To let villagers carry bigger loads, edit the
villager's entry in `material_values.h`:

```cpp
{"base:villager", 30.0f},   // was 10.0f
```

Or to make an item "heavy":

```cpp
{"base:gold_bar", 16.0f},
```

A player holding one gold bar consumes 16/128 of their capacity.
Since the handbook reads `material_values.h` directly, the new number shows
up in the "Material value" row for every affected entry on the next build.
