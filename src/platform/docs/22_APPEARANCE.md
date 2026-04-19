# Block appearance & chunk compression — invariants

Scope: two related refactors — (a) separating *visual variation* from *block
type*, and (b) compressing homogeneous chunks to a lite form so view radius
can scale without RAM blowup.

## Model

A block cell has three independent dimensions:

| Layer       | Stable?   | Writer                       | Storage                 | Purpose |
|-------------|-----------|------------------------------|-------------------------|---------|
| **Type**    | stable    | `World::setBlock`            | `Chunk::m_blocks` (u16) | Gameplay id: `leaves`, `stone` |
| **Param2**  | mutable   | `World::setBlock/setState`   | `Chunk::m_param2` (u8)  | Rotation / state bits |
| **Appearance** | mutable | `World::setAppearance`       | `Chunk::m_appearance` (u8) | Palette index into `BlockDef::appearance_palette` |

Annotations (flowers, moss, grass tufts *on top of* a block) are entities at a
block position — unchanged by this refactor.

## Invariants

These are load-bearing. CI/review should reject violations.

### I1. One writer per layer (runtime gameplay)
For **runtime mutations** — anything driven by a tick, action, or behavior —
`World::setBlock(x,y,z,bid,p2)` and `World::setAppearance(x,y,z,idx)` are the
only mutators. Convert handler, seasonal painter, INTERACT toggles, and tests
all funnel through them so I2's notification fires.

**Exempted bulk paths** (write the chunk grid directly, then call
`Chunk::classify()` to land in the right mode):
- `server/world_template.h` — worldgen builds chunks before any client knows
  about them; no S_BLOCK to send.
- `server/world_save.h` — `setRawBlocks/setRawParam2Array/setRawAppearanceArray`
  reload a serialized chunk in one shot.
- `client/network_server.h` — S_CHUNK replay writes the server's snapshot;
  the client is consuming a notification, not creating one.

Verify (runtime): `grep -n "chunk->set\|c->set\b" src/platform/server/{server.cpp,server.h}`
should be limited to spawn-anchor placement, dropped-item materialization, and
the same-tick chunk-just-loaded fast path inside `World::setBlock` itself.

### I2. One change notification
Both mutators emit `onBlockChange(pos, {oldBid, newBid, oldP2, newP2, oldApp, newApp})`.
No partial notifications. Client rebuilds (mesh, vertex tints) are derived
from that stream; never cached independently.

### I3. One action family per concept
- Type mutation → `TYPE_CONVERT` (existing).
- Appearance mutation → `TYPE_INTERACT` with an appearance-index payload.
  Appearance *is* a state toggle, same family as door/TNT. No new action
  type; Rule 0's four-type invariant stands.
- `TYPE_CONVERT` with `convertFrom.kind == Block` is **position-authoritative**:
  server derives `fromItem` from the actual block at the position. Clients
  may leave `fromItem` empty; if provided, it's ignored for block sources.

### I4. One parser, one validator
`BlockDef::appearance_palette` is parsed only in `python_bridge.cpp`.
`BlockDef::clampAppearance(idx)` is the only bounds-check; called from the
loader, Convert handler, mesher, and migration.

### I5. One wire + disk format, version-gated
Protocol and save versions each bump exactly once to carry the
`{bid, param2, appearance}` triple. The legacy variant-name → (type,
appearance) migration table is a single static map, consulted only at
save-load time.

### I6. No implicit variant types
After migration, `leaves_orange/red/gold/spring/summer/bare/snow` do not
exist as `BlockDef`s. Their names live only in the migration table.

Verify: `grep "leaves_orange\|leaves_red\|leaves_gold" src/` — should show
only the migration table entry.

## Chunk compression

A chunk is in one of two modes:

| Mode     | Storage                              | When |
|----------|--------------------------------------|------|
| **Lite** | `{bid (u16), appearance (u8)}` — 3 B; per-cell arrays unallocated | All 4096 cells share one `(bid, appearance)` and `param2 == 0` |
| **Full** | `blocks[4096] (u16) + param2[4096] (u8) + appearance[4096] (u8)` — 16 KB | Any divergent cell |

### I7. One classifier, one hydrator
- `Chunk::classify()` (server-only) is the sole decider of Lite vs. Full.
  Called on generate, load, and on every write that might break homogeneity.
- `Chunk::hydrate()` is the sole Lite → Full transition. Called transparently
  by `World::setBlock/setAppearance` on the first divergent write.

### I8. Mesher is mode-oblivious
The chunk mesher calls `chunk.get(x,y,z)` and `chunk.getAppearance(x,y,z)` —
these return the stored value whether the chunk is Lite or Full. The mesher
may *inspect* `isLite()` / `liteBid()` to short-circuit Lite chunks whose
six neighbors are also Lite and fully occlude (optimization, not correctness;
mirrors the inner-loop face-cull rule exactly so glass/leaves chunks still
emit faces against same-typed neighbors).

### I9. No hidden hydration
Read paths (`get`, `getAppearance`, `getParam2`) never hydrate. Only writes
do. A pure observer can never cause a chunk's mode to change.

## Non-goals

- Per-cell RGB (palette index only; BlockDef palette entries hold the RGB).
- Sub-chunk compression (16-block-inside-stone air pockets stay in Full mode;
  a future refactor could add 8³ sub-chunks, but not this pass).
- LOD distant terrain (tracked separately; heightmap horizon is a future
  proposal that builds on top of the palette system).

## Reference layout

```
src/platform/logic/
  chunk.h                — Lite/Full mode, appearance array, classify/hydrate
  appearance.h           — AppearanceEntry { tint, pattern }
  block_registry.h       — BlockDef gains appearance_palette + clampAppearance
  material_values.h      — unchanged; palette doesn't affect value
src/platform/server/
  world.h                — setBlock/setAppearance are the only mutators
  python_bridge.cpp      — palette parser; get/set_appearance Python API
  server.cpp             — Convert handler: position-authoritative source
  world_save.h           — header-only; mode byte + Lite/Full payload + v1 migration
src/platform/net/
  net_protocol.h         — S_CHUNK mode byte; S_BLOCK carries appearance
src/platform/client/
  local_world.h          — mirrors server chunk modes
  chunk_mesher.cpp       — consults classify() for cull, reads palette for tint
```
