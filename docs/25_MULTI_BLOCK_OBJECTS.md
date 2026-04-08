# Multi-Block Objects

How two-block and N-block objects work: beds, chests, doors, and the future tree
pattern-matching system. Read this before adding any block that spans more than
one voxel cell.

---

## Core Principle

A multi-block object is two or more adjacent blocks that form a single logical
item. The blocks exist independently in the chunk — there is no separate
"multi-block entity" table. Connection info is encoded in `param2` so that:

- The chunk format never changes (param2 already exists in every block cell)
- Saving/loading is free (param2 is part of the chunk, no extra file needed)
- The renderer reads param2 from the padded neighbor volume to decide shape

The server validates multi-block integrity at placement and cascades breaks to
all companion blocks.

---

## param2 Bit Layout

Each block cell has a `uint8_t param2` field. Current and planned allocations:

| Bits | Name | Used by |
|------|------|---------|
| 0–1  | `FourDir` | Stairs, doors, beds — facing direction (N/S/E/W) |
| 2    | `DoorHinge` | Doors — which column holds the hinge |
| 3–4  | `CompanionDir` | Beds (and future 2-block objects) — direction to partner block |
| 5    | `CompanionRole` | Beds — 0 = foot half, 1 = head half |
| 6–7  | (free) | Available for future use |

**`FourDir` encoding (bits 0–1):**
```
0 = +Z (South)   1 = +X (East)   2 = -Z (North)   3 = -X (West)
```

**`CompanionDir` encoding (bits 3–4):**
```
0 = +Z   1 = +X   2 = -Z   3 = -X
```

These bits point from *this block* toward its companion. Both halves store
their own `CompanionDir` pointing at each other, so either half can locate its
partner independently.

---

## Doors — Existing 2-Block Object

Doors are the original multi-block precedent. They use:
- Two separate `BlockId` values: `base:door` (closed) and `base:door_open`
- Param2 bit 2 = hinge flag (which column: left or right)
- Placement creates both halves (`PlaceBlock` sets blocks at `y` and `y+1`)
- `Interact` scans up/down for the other half and toggles both together
- Breaking one half removes both via the same vertical scan

The door implementation is ad-hoc — it does not use `MultiBlockHelper`. Future
objects should use the helper.

---

## Beds — IKEA-Style 2-Block Object

### Block IDs

- `base:bed_head` — the headboard end of the bed
- `base:bed_foot` — the footboard end

Both are solid blocks with hardness 0.2. The player places one "bed" item; the
server creates both halves atomically. If the partner position is occupied, the
placement is rejected entirely — orphaned halves never result from player
placement. (World gen may place them directly and must always place correct pairs.)

### Param2 Layout for Beds

The foot block (`CompanionRole` = 0) has `CompanionDir` pointing toward the head.
The head block (`CompanionRole` = 1) has `CompanionDir` pointing toward the foot.
`FourDir` in both halves = which direction a sleeping player faces (head toward
headboard = FourDir is the direction from foot to head).

### Mesh

Both halves share a low platform (`y = 0` to `y = 0.55`, full block width/depth).
The head block additionally emits a headboard box (`y = 0` to `y = 0.90`) at
the "back" face (the face away from the foot). The foot block emits a shorter
footboard (`y = 0` to `y = 0.65`) at its "front" face.

Colors are IKEA-style (no Minecraft bedsheets):
- Platform top: off-white upholstery `{0.85, 0.82, 0.78}`
- Platform sides + headboard + footboard: birch wood `{0.62, 0.47, 0.28}`

The renderer uses `m_paddedP2` (see §Mesher Changes below) to read the companion's
`CompanionRole` bit and confirms the expected block ID is present before emitting
the full connected shape. If the partner is missing (e.g. broken mid-save), a
simple flat slab is emitted as fallback.

### Breaking

Breaking either half: read `CompanionDir` from param2 → compute partner position
→ verify it is the matching half → set both to `BLOCK_AIR` → broadcast two
`S_BLOCK` changes → drop one `base:bed` item.

---

## Chests — Storage Blocks

### Overview

`base:chest` is a single-block storage object (no companion block needed). On
right-click (`Interact` action), the server sends the chest's inventory to
the requesting client. The client shows a UI panel alongside the player inventory
where items can be moved in either direction.

### Server Storage

```cpp
// In GameServer (server.h):
std::unordered_map<BlockStateKey, Inventory, BlockStateKeyHash> m_blockInventories;
std::unordered_map<BlockStateKey, std::unordered_set<ClientId>, BlockStateKeyHash> m_chestViewers;
```

Chest inventories are NOT part of chunk data. They live in a separate
`chest_inventories.bin` file alongside `inventories.bin`. Format:

```
[u32 count]
For each chest:
  [i32 x][i32 y][i32 z]
  [u32 itemCount]
  For each item: [string itemId][i32 count]
```

On `BreakBlock` for a chest: drop item entities for all contents, then erase the
map entry.

### Protocol

| Message | Dir | Payload | Purpose |
|---------|-----|---------|---------|
| `C_OPEN_CHEST` (0x0007) | C→S | `[i32 x][i32 y][i32 z]` | Request chest contents |
| `C_CHEST_MOVE` (0x0008) | C→S | `[i32 x][i32 y][i32 z][u8 dir][string itemId][i32 count]` | Transfer items; dir=0 player→chest, dir=1 chest→player |
| `S_CHEST_OPEN` (0x100C) | S→C | `[i32 x][i32 y][i32 z][u32 n][pairs...]` | Full chest inventory snapshot |

`S_CHEST_OPEN` is sent only to the requesting client — not broadcast. This avoids
leaking inventory contents to bystanders and keeps bandwidth proportional to use,
not proximity.

Existing `S_INVENTORY` handles the player-side of transfers (server pushes updated
player inventory after any `C_CHEST_MOVE`).

### UI

A `ChestUI` panel opens when the client receives `S_CHEST_OPEN`. It is a second
ImGui window positioned beside the existing `InventoryUI`. Clicking an item sends
`C_CHEST_MOVE`. Pressing ESC or moving more than 6 blocks from the chest closes it
(client-side heuristic; server validates range on every `C_CHEST_MOVE` anyway).

---

## MultiBlockHelper (New Utility)

All future multi-block objects should use `server/multiblock.h` rather than
ad-hoc scans in `server.cpp`:

```cpp
struct CompanionInfo {
    glm::ivec3 offset;    // position of companion relative to this block
    BlockId    expectedId; // what block ID we expect there
    uint8_t    param2;     // what param2 that companion should have
};

// Compute expected companions for a placed block given its param2.
// Returns empty vector for single-block types.
std::vector<CompanionInfo> getCompanions(const BlockDef& def, uint8_t p2);

// True iff all companion slots contain the right block IDs.
bool isIntact(const BlockDef& def, uint8_t p2, World& world, glm::ivec3 pos);

// Set all companion positions to AIR and broadcast S_BLOCK. Drop combined item.
void breakAll(World& world, glm::ivec3 pos, GameServer& server);
```

---

## Mesher Changes for Neighbor-Aware Rendering

The `ChunkMesher` fills a 18×18×18 padded volume (`m_padded`) to read neighbor
block IDs during mesh generation. To render connected shapes (beds), it also
needs neighbor `param2` values.

**Change required:** add a second padded array `m_paddedP2 : uint8_t[PADDED_SIZE³]`
filled in `fillPaddedVolume`. A `cachedParam2(lx, ly, lz)` accessor mirrors
`cachedBlock`. This is purely additive — non-multi-block types ignore it.

The bed mesh cases read `m_paddedP2` to confirm the companion block is present and
has the expected `CompanionRole` bit before emitting the full connected geometry.

---

## Tree Pattern Matching (Future)

Do not implement now. Architecture notes for when it is needed:

Trees are currently composed of individual `base:wood` and `base:leaves` blocks.
Pattern matching would render a whole matching tree as a single high-res mesh
(curves, detail geometry) instead of a stack of voxels.

**Where it runs:** `ChunkMesher::buildMesh` pre-pass, before the per-cell loop.
The 18×18×18 padded volume already contains all cells needed to evaluate any
tree that fits within one chunk span — no extra data needed.

**How it works:**
1. For each `base:wood` block that could be a trunk base (no `base:wood` below it),
   check whether all cells in the matching tree template are present and correct.
2. If the template matches: emit the high-res mesh, mark all those cells as
   "consumed" so the per-cell loop skips them.
3. If any cell is missing or wrong: skip; the per-cell loop renders individual
   voxels as normal.

**Templates** are defined as arrays of `{dx, dy, dz, blockId}` tuples, loaded
from Python artifacts (so new tree shapes don't require a C++ rebuild).

**Why not a multi-block object in the server sense:** trees grow and lose blocks
organically. There is no "placement" event to set param2. The pattern match is
purely a render-time decision — the server world state is unchanged.

---

## What NOT to Do

**Do not store multi-block membership in `activeBlocks`.**
That map is for ticking blocks (TNT, wheat growth). Beds and non-ticking objects
do not belong there. Adding them pollutes the tick loop and complicates save/load.

**Do not add a global position→companion map on `World`.**
Companion positions are always deterministic from param2 — a second map is
redundant state that can desync on save/load edge cases.

**Do not network chest inventory in chunk data.**
Chunks carry structural state (block IDs, param2). Inventory is semantic state.
Mixing them would send chest contents to every nearby client, a security and
bandwidth problem. Use `S_CHEST_OPEN` targeted at the opener only.

**Do not use `Inventory::hotbar` for chest inventories.**
Hotbar is a player concept. Chests use the plain `m_items` map only. Never call
`autoPopulateHotbar` on a chest inventory.

**Do not allow orphaned bed halves from player placement.**
If the partner position is blocked, reject the entire placement. Orphaned halves
may only arise from world gen (which must always write correct pairs) or from
block breaking (which cascades to remove both halves).

**Do not shadow param2 in `BlockStateMap`.**
`BlockStateMap` is `std::unordered_map<std::string, int>`. The canonical param2
is in `Chunk::m_param2`. The mesher reads from there — do not duplicate it in
`BlockStateMap`, which would create inconsistency on reload.

**Do not add texture-mapped geometry for bed rendering.**
The engine uses vertex-colored geometry throughout. The IKEA-style bed uses
multiple `emitBox` calls with different solid colors per sub-box, exactly as
stairs and doors already do.
