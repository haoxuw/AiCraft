# AiCraft - Block & Entity Model

How the world stores everything: blocks in a compact grid, entities as full objects, and the bridge between them for active blocks.

---

## The Two Storage Systems

```
WORLD
  |
  |-- Chunk Grid (compact)          Entity Manager (rich)
  |   BlockId per cell (uint16)     Full objects with properties
  |   4096 per chunk                Any count, any position
  |   Fixed to grid positions       Float positions
  |
  |   Stores: terrain, walls,       Stores: players, NPCs,
  |   ores, crops, circuits,        animals, items, projectiles,
  |   TNT, all placed blocks        particles, effects
  |
  +-- Block State Map (sparse)
      Per-instance data for
      ACTIVE blocks only
      (TNT fuse, wheat stage,
       wire signal level)
```

---

## Block Types: Passive vs Active

### Passive Blocks (majority)

Just a number. No per-instance state. No behavior.

```
Stone, Dirt, Grass, Sand, Wood, Leaves, Glass, Brick, ...

Storage: chunk[x][y][z] = BlockId (2 bytes)
State:   none
Behavior: none (only reacts when an Action targets it)

Memory per block: 2 bytes
Memory per chunk: 8 KB
```

### Active Blocks (special)

Still stored as a BlockId in the chunk grid, BUT also have per-instance state in a side-map and behavior that runs each tick.

```
TNT, Wheat, Wire, NAND Gate, Furnace, Piston, ...

Storage: chunk[x][y][z] = BlockId (2 bytes)  -- same as passive
State:   block_states[(x,y,z)] = {fuse: 80, lit: false}  -- extra
Behavior: Python class with decide() method  -- hot-loaded

Memory per block: 2 bytes + state dict (~50-200 bytes)
```

### Why This Design?

```
Option A: Every block is a full object instance
  Memory: 4096 * 200 bytes = 800 KB per chunk
  Problem: 99% of blocks are stone/dirt, wasting memory

Option B: Blocks are compact IDs + sparse state for active ones  <-- OUR CHOICE
  Memory: 4096 * 2 bytes + ~10 active blocks * 200 bytes = 10 KB per chunk
  Advantage: 80x less memory, same expressiveness

Most chunks have 0-10 active blocks out of 4096 total.
```

---

## Data Flow

```
                    Python Definitions
                    (source of truth for WHAT exists)
                           |
                           v
    +--------------------------------------------------+
    |              Block Registry (C++)                 |
    |                                                    |
    |  BlockId  string_id        behavior   default_state|
    |  ------  ----------        --------   ------------ |
    |  0       "base:air"        Passive    {}           |
    |  1       "base:stone"      Passive    {}           |
    |  2       "base:dirt"       Passive    {}           |
    |  3       "base:grass"      Passive    {}           |
    |  ...     ...               ...        ...          |
    |  9       "base:tnt"        Active     {fuse:80}    |
    |  10      "base:wheat"      Active     {stage:0}    |
    |  11      "base:wire"       Active     {power:0}    |
    |  12      "base:nand_gate"  Active     {in_a:0,...} |
    +--------------------------------------------------+
                           |
              +------------+------------+
              |                         |
    Chunk Grid (compact)       Block State Map (sparse)
    +------------------+       +------------------------+
    | 1 1 1 1 1 1 1 1 |       | (5,3,2): {power: 12}  |
    | 1 3 3 3 3 3 3 1 |       | (7,3,4): {power: 11}  |
    | 1 3 3 3 3 11 3 1|       | (8,3,4): {in_a:11,    |
    | 1 3 3 9 3 11 3 1|       |           in_b:0,     |
    | 1 3 3 3 3 12 3 1|       |           out:15}     |
    | 1 1 1 1 1 1 1 1 |       | (5,3,5): {fuse:80,    |
    +------------------+       |           lit:false}   |
     All blocks: 2 bytes       +------------------------+
     per cell                   Only active blocks
```

---

## Entity Manager (Non-Block Objects)

```
Entity Manager
  |
  |-- Entity: base:player (id=1)
  |     pos: (142.5, 67.3, -89.1)
  |     props: {hp: 20, hunger: 18.5, selected_slot: 2}
  |
  |-- Entity: base:pig (id=42)
  |     pos: (100.2, 5.0, 55.8)
  |     props: {hp: 10, hunger: 0.7, age: 450.0}
  |
  |-- Entity: base:pig (id=43)
  |     pos: (103.1, 5.0, 58.2)
  |     props: {hp: 8, hunger: 0.3, age: 120.0}
  |
  |-- Entity: base:item_entity (id=100)
  |     pos: (50.0, 6.5, 30.0)
  |     props: {item_type: "base:stone", count: 1, age: 5.0}
  |
  ...
```

Entities have:
- **Type ID** (string) -> looks up EntityDef for model, texture, collision, etc.
- **Position** (float Vec3) -> not grid-locked
- **Properties** (key-value map) -> hp, hunger, inventory, custom attrs
- **Behavior** (Python decide()) -> AI logic, physics reactions

---

## What Goes Where?

```
+---------------------+----------+----------+
| Thing                | Storage  | Type     |
+---------------------+----------+----------+
| Stone block          | Grid     | Passive  |
| Dirt block           | Grid     | Passive  |
| Grass block          | Grid     | Passive  |
| TNT block            | Grid+State| Active  |
| Wheat crop           | Grid+State| Active  |
| Redstone wire        | Grid+State| Active  |
| NAND gate            | Grid+State| Active  |
| Furnace              | Grid+State| Active  |
| Player               | Entity   | Active   |
| Pig                  | Entity   | Active   |
| Villager NPC         | Entity   | Active   |
| Dropped item         | Entity   | Active   |
| Arrow projectile     | Entity   | Active   |
| Potion effect        | Entity   | Active   |
+---------------------+----------+----------+

Rule of thumb:
  Fixed to grid + no state? -> Passive block (just BlockId)
  Fixed to grid + has state? -> Active block (BlockId + state map)
  Free-moving or has position? -> Entity
```

---

## Building a Computer

With NAND gates, wires, and power sources, you can build:

```
NOT gate:    NAND with both inputs from same source
AND gate:    NOT(NAND(a,b))  = two NANDs
OR gate:     NAND(NOT(a), NOT(b)) = three NANDs
XOR gate:    four NANDs
SR Latch:    two cross-coupled NANDs
D Flip-Flop: four NANDs + clock
Counter:     chain of flip-flops
ALU:         many gates
CPU:         ALU + registers + control unit

All defined in Python. All hot-loadable.
All stored as compact blocks in the chunk grid.
```
