# ChunkInfo — Event-Driven Block Awareness

## Problem

The old design scanned blocks around each entity every tick:

```
getKnownBlocks(radius=80) → O(radius² × height) per entity per tick
```

With N agents near the same area this is `O(N × radius² × height)` redundant work
every tick, all producing identical results from the same chunk data.

---

## Design

The server maintains a `ChunkInfo` alongside each chunk. Behaviors access block
counts and sample positions through `LocalWorld` — no per-entity scanning needed.

### ChunkInfo structure

```
ChunkInfo {
    counts:  {type_id: int}          // census of every block in the chunk (air included)
    samples: {type_id: [pos, ...]}   // up to K nearest-to-center positions per type
}
```

- **`counts`** — full census. Answers "does this chunk have any trunk?" in O(1).
- **`samples`** — up to K positions per type, sorted nearest-to-chunk-center. Used by
  behaviors to navigate toward a block of a given type.
- **K** (samples per type) is configured at server startup (default: 8).
- **No block types are filtered** — air, grass, stone, all included.
  Behaviors decide what's interesting; the engine doesn't prejudge.

---

## Lifetime

```
Chunk generated / loaded
  → Server builds ChunkInfo (iterates 16×16 per-column blocks once)
  → Server broadcasts S_CHUNK_INFO to all connected agent clients

Single block changes (S_BLOCK)
  → Server updates ChunkInfo in O(1):
      counts[old_type]--; remove pos from samples[old_type] if present
      counts[new_type]++; insert pos into samples[new_type] (keep sorted, trim to K)
  → Server broadcasts S_CHUNK_INFO_DELTA (chunk_key + affected type entries only)
```

The server processes all chunk mutations on a **single thread**, so ChunkInfo is
always consistent with the actual block data — no locking needed.

---

## Wire protocol (new messages)

```
S_CHUNK_INFO       0x1010
    u64 chunk_key             // packed (cx << 32 | cz)
    u32 entry_count
    for each entry:
        str  type_id          // e.g. "base:trunk"
        u32  count
        u8   sample_count     // ≤ K
        for each sample:
            i32 x, i32 y, i32 z

S_CHUNK_INFO_DELTA 0x1011
    u64 chunk_key
    u32 entry_count           // only the changed type entries
    for each entry:           // (same format as above)
        ...
```

`S_CHUNK_INFO` is sent once per chunk when an agent client first receives that
chunk. `S_CHUNK_INFO_DELTA` is sent on every `S_BLOCK` change.

---

## Agent client — LocalWorld query API

Behaviors see exactly the same API as before. The implementation changes under the hood.

### Block count (new)

```python
# How many trunks are currently known in all nearby chunks?
local_world.count("base:trunk")          # int, sum across nearby chunks
local_world.count("base:trunk", max_dist=80)
```

### Block position queries (same API, new backend)

```python
# Nearest sample of type across nearby chunks (chunks queried nearest-first)
local_world.get("base:trunk")            # BlockView | None
local_world.get("base:trunk", max_dist=80)

# All samples, nearest-chunk-first (within each chunk, samples are chunk-center sorted)
local_world.all("base:trunk")            # [BlockView, ...]
local_world.all("base:trunk", max_dist=80)
```

**"Nearest" semantics**: chunks are queried in order of distance from the entity.
Within a chunk, the K samples are sorted nearest-to-chunk-center (not to entity).
So `local_world.get()` returns "a block of this type in the nearest chunk that has one",
which is close-enough for navigation. This is `O(chunks_in_radius)` — ~50 chunks for
radius=80 with 16×16 chunks.

---

## What was removed

| Removed | Reason |
|---------|--------|
| `getKnownBlocks()` | Replaced by ChunkInfo lookup |
| `BlockCache` per entity | Per-entity caches replaced by shared ChunkInfo cache |
| `kIgnoredBlockTypes` | No block types are filtered |
| Per-tick block scan | Replaced by event-driven S_CHUNK_INFO_DELTA |

---

## Scalability

### Proximity-based subscription (one process per entity is fine)

Each agent process controls one entity and maintains its own `m_chunkInfoCache`.
The server keeps data volume sane by only sending chunk info to agents whose entity
is **near** that chunk — not broadcasting to every agent in the world.

```
Server tracks: chunk_key → set<AgentConnectionId>  (subscribed agents)

Agent entity moves into new chunk range
  → Server adds agent to subscription set for new chunks
  → Server sends S_CHUNK_INFO for those chunks (one-time, per agent)
  → Server removes agent from subscription set for chunks now out of range

Block placed/broken in chunk C
  → Server updates ChunkInfo for C in O(1)
  → Server sends S_CHUNK_INFO_DELTA only to agents subscribed to C
  → Cost = O(agents near C), not O(all agents in world)
```

With N agents spread across the world, a block change in one chunk reaches only the
agents nearby — typically a small constant, not N.

### Cost comparison

| Scenario | Old cost | New cost |
|----------|----------|----------|
| 1 agent, 80-unit radius | O(radius² × height) per tick | O(~50 chunks) per query, 0/tick |
| N agents spread across world, block change | O(N × radius² × height) per tick | O(agents near chunk) per block change |
| N agents all in same area, block change | O(N × radius² × height) per tick | O(N) delta sends (small msg each) |
| Agent connects / moves into new chunks | Full block scan | S_CHUNK_INFO for new chunks only |

---

## Staleness

Agent clients may have outdated ChunkInfo if S_CHUNK_INFO_DELTA messages are delayed
or dropped. This is acceptable: the server validates every `ActionProposal` and rejects
impossible ones (SourceBlockGone, etc.). Behaviors recover on the next decide() tick.

This matches the existing policy for entity state (S_ENTITY may also be delayed).

---

## Implementation status

Implemented. Key files:

| Component | File |
|-----------|------|
| ChunkInfo struct + build/update | `src/server/chunk_info.h` |
| S_CHUNK_INFO / S_CHUNK_INFO_DELTA wire format | `src/shared/net_protocol.h` |
| Server: build on chunk send, delta on block change | `src/server/client_manager.h` |
| Agent: cache + query in tick() | `src/agent/agent_client.h` |
| Python bridge: blocks list from ChunkInfo samples | `src/server/python_bridge.cpp` |
