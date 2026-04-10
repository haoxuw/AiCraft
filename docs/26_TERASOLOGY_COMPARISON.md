# Terasology vs AiCraft — Network Architecture Comparison

Terasology source: `ref/Terasology/` (cloned from github.com/MovingBlocks/Terasology)

---

## 1. Network Transport

| Dimension | Terasology | AiCraft |
|-----------|-----------|---------|
| **Library** | Netty (NIO event-loop) | Raw POSIX TCP sockets |
| **Protocol** | TCP over Netty ChannelPipeline | TCP, Nagle disabled (TCP_NODELAY) |
| **Compression** | LZ4 frame compression | None |
| **Framing** | 3-byte length-prefix (`LengthFieldPrepender`) | 8-byte header: 4-byte MsgType + 4-byte length |
| **Discovery** | Module negotiation at connect | UDP broadcast on port 4444 |
| **Encryption** | Handshake identity verification | None |

**Terasology pros:** LZ4 compression matters for chunk data (4KB per chunk compresses well). Netty gives battle-tested backpressure, SSL support, and easy protocol evolution via pipeline stages.

**AiCraft pros:** Zero-dependency. Simpler to reason about, easier to port (WASM target). No Netty JVM overhead.

**Lesson for AiCraft:** Consider LZ4 for `S_CHUNK` messages only — chunk payloads (4,096 × u32 = 16 KB each) are the dominant bandwidth cost and compressible by 60–80%.

---

## 2. Message / Packet Types

| Dimension | Terasology | AiCraft |
|-----------|-----------|---------|
| **Format** | Protocol Buffers (`.proto` files) | Hand-written binary (`WriteBuffer`/`ReadBuffer`) |
| **Type registry** | Protobuf reflection + ECS ComponentLibrary | `MsgType` enum in `net_protocol.h` |
| **Schema evolution** | Protobuf wire compatibility (add fields freely) | Breaking — old clients break if format changes |
| **New message cost** | Add field to `.proto`, regenerate | Add enum entry + hand-write ser/deser |
| **Size** | Protobuf varint encoding | Fixed-width fields (some waste) |
| **Event-as-message** | Any `@NetworkEvent` class becomes a message | Separate explicit enum per message |

**Terasology pros:** Protobuf schema evolution is huge for live games. Adding optional fields never breaks old clients. Reflection means new component types are automatically serialized without touching network code.

**AiCraft pros:** No codegen step. Binary layout is explicit and readable. No reflection overhead at runtime. Trivial to add to a WASM build.

**Lesson for AiCraft:** The "new message cost" is real but manageable at AiCraft's current scale. However, as multi-block objects, chest inventory, and more message types are added (targeting ~20 total), a simple schema file listing all message IDs and field layouts (even just as a comment block in `net_protocol.h`) will prevent field-order bugs.

---

## 3. World State Synchronization

| Dimension | Terasology | AiCraft |
|-----------|-----------|---------|
| **Chunk streaming** | View-distance based, continuous relevance tracking | 6-chunk radius, max 4 new per tick, `sentChunks` set |
| **Chunk format on wire** | `ChunkStore` protobuf with compressed block array | 16KB raw u32 array (blockId | param2 << 16) |
| **Chunk invalidation** | `InvalidateChunkMessage` to evict stale chunks | Chunks never invalidated once sent |
| **Block updates** | `BlockChangeMessage` in `NetMessage` batches | `S_BLOCK` per change, broadcast to all |
| **Initial world send** | `JoinMessage` triggers view-distance scan | On `C_HELLO`: 9×9 chunk region queued |
| **Client-side regeneration** | Client CAN regenerate chunks (same seed) | Client never generates; only receives |
| **Chunk ownership** | Server generates; client gets or regenerates | Server only |

**Terasology pros:** `InvalidateChunkMessage` is critical for mutable worlds — when a chunk changes significantly (explosion, large build), Terasology can evict and re-send. AiCraft can silently drift if a client misses a `S_BLOCK` message.

**AiCraft gap:** No chunk invalidation / re-sync. If a TCP packet is lost (shouldn't happen on localhost but will on WAN), the client's chunk is wrong forever. **This is a reliability risk for the multi-block placement protocol** — both `S_BLOCK` changes for a bed placement must arrive; if only one arrives, the client has an orphaned half.

**Lesson for AiCraft:**
1. For multi-block placement: send both `S_BLOCK` changes together in one `S_MULTI_BLOCK` message (atomic from the client's perspective).
2. Add a "request chunk resync" message (`C_RESYNC_CHUNK`) so the client can recover from drift.

---

## 4. Entity / Actor Replication

| Dimension | Terasology | AiCraft |
|-----------|-----------|---------|
| **Replication model** | ECS: `@Replicate` fields on Components | Explicit `EntityState` struct broadcast |
| **Granularity** | Field-level: only changed `@Replicate` fields sent | Full `EntityState` every broadcast interval |
| **Ownership** | `EntityInfoComponent.owner`; client can own player entity | `m_entityOwner` map; server owns everything |
| **Client prediction** | Yes: `ClientCharacterPredictionSystem` (circular input buffer, 128 frames) | No: pure server-authoritative |
| **Lag compensation** | Yes: `@ServerEvent(lagCompensate=true)` rewinds state | No |
| **Creatures ownership** | Server-owned (RELEVANT replication) | Agent client process per Creatures |
| **Entity interpolation** | Yes (position interpolated between snapshots) | No (teleport on update) |

**Terasology pros:** Client prediction makes player movement feel instant at any latency. Without it, 100ms ping causes visible input lag. Field-level replication reduces bandwidth — if only HP changes, only HP is sent, not the full entity.

**AiCraft pros:** Server-authoritative-only is far simpler to reason about and debug. No reconciliation bugs, no prediction rollback complexity. Acceptable for LAN/localhost (primary use case today).

**Lesson for AiCraft:** For the multi-block objects goal, entity replication changes are not required. However, as AiCraft moves toward WAN multiplayer:
- Add velocity-based client interpolation for player movement (smooth at 50ms ping)
- Client prediction is not needed for NPCs (players don't control them)

---

## 5. Action / Intent System

| Dimension | Terasology | AiCraft |
|-----------|-----------|---------|
| **Model** | Event-driven: `@ServerEvent` classes | Proposal queue: `ActionProposal` enum |
| **New action cost** | Create `Event` subclass, annotate `@ServerEvent`, handle in any System | Add `Type` enum value, add ser/deser case, add resolution case in `server.cpp` |
| **Validation location** | Server Systems handle events; any System can validate | Single `resolveActions()` function in `server.cpp` |
| **Lag compensation** | Per-action flag `@ServerEvent(lagCompensate=true)` | None |
| **Atomicity** | One event = one action | One `ActionProposal` = one action |
| **AI action path** | Same `@ServerEvent` mechanism (server-side behavior tree fires events) | Different: agent sends `ActionProposal` via TCP, same path as players |
| **Chaining** | Systems listen for events, fire new events | Direct code: `server.cpp` resolves and calls callbacks |

**Terasology pros:** Any System can handle any action without centralizing in one file. Adding a new action type doesn't touch a switch statement — just annotate a new class and add a handler. Composable.

**AiCraft pros:** The single `resolveActions()` switch is easy to audit — all action handling is in one place. No magic annotation scanning. No accidental double-handling by two systems.

**Lesson for AiCraft:** The centralized switch is fine at current scale (~12 action types). For multi-block objects, the new actions (`OpenChest`, `TransferChestItem`) fit naturally into the existing pattern. No architectural change needed.

However, **for the chest/bed placement**, consider batching two `S_BLOCK` updates into a single atomic server callback to avoid partial-state flicker:
```
// Instead of: onBlockChange × 2
// Add:        onMultiBlockChange(vector<BlockChange>)
```

---

## 6. Inventory Protocol

| Dimension | Terasology | AiCraft |
|-----------|-----------|---------|
| **Model** | Slot grid: container entity with item child entities | Counter map: `item_id → count` |
| **Wire format** | Component replication (`@Replicate stackCount`) | Full inventory dump `S_INVENTORY` on every change |
| **Container (chest)** | Container entity; `InventoryComponent`; event-based open | Structure entity with `has_inventory=true`, shares `S_INVENTORY` path |
| **Chest sync** | All viewers get component updates automatically | `S_INVENTORY` broadcast on every change — any client with the entity stays in sync |
| **Item as entity** | Yes: each item stack is a networked entity with components | No: items are just string IDs in a map |
| **Drop → world** | Spawn item entity with `ItemComponent` | Spawn item entity with `ItemType` property |
| **Slot drag-drop** | `MoveItemRequest` event; server validates, replaces | `Relocate` action with `Container::self()` ↔ `Container::entity(eid)` |

**Terasology pros:** Items-as-entities means item enchantments, durability, NBT data, etc., all fall out of the component system. No special cases. Container inventories are automatically replicated to viewers.

**AiCraft pros:** Counter model is dead simple — perfect for a game where items don't have per-instance state. No entity spawning overhead for inventory manipulation.

**Lesson for AiCraft (applied):** AiCraft achieves Terasology's "replicated to all viewers" property by broadcasting `S_INVENTORY` to all clients on every inventory change. No per-chest viewer tracking is needed — the broadcast itself does the job, and chest inventories ride the *same* wire format as player inventories because chests are just Structure entities.

---

## 7. AI / Creatures Architecture

| Dimension | Terasology | AiCraft |
|-----------|-----------|---------|
| **Where AI runs** | Server-side, same process, same JVM | Separate OS process per Creatures |
| **AI model** | Behavior tree (XML-defined, `BehaviorSystem`) | Python `decide()` function at 4 Hz |
| **Language** | Java behavior node classes | Python scripts (hot-swappable) |
| **Frequency** | Every server tick | 4 Hz decide; 50 Hz action replay |
| **Crash isolation** | Creatures AI crash = server exception (needs try/catch) | Agent crash = server sees disconnection; Creatures freezes; new agent spawned |
| **Hot-reload** | Runtime BehaviorTree asset update | `C_RELOAD_BEHAVIOR` message with new Python source |
| **World view** | Full server world access (same memory) | Partial: only `S_ENTITY` + `S_CHUNK` received via TCP |
| **Action latency** | Zero (immediate) | ~1 tick (TCP round-trip to localhost) |
| **Parallelism** | Single-threaded (server tick) | True parallel: each agent is an OS process |

**Terasology pros:** Zero latency AI decisions. AI has full world access (no "perception horizon"). Behavior trees are composable — mix and match nodes without rewriting scripts.

**AiCraft pros:** **Process isolation is the star feature.** A buggy Python behavior cannot crash the server. Behaviors run in true parallel (multiple CPU cores). Python is far more accessible for modders than Java behavior node classes. The partial-world-view is a deliberate constraint that makes AI "fair" (it only knows what it can see).

**This is AiCraft's biggest architectural advantage over Terasology.** The agent-client model is novel and worth preserving.

**Lesson for AiCraft:** Consider adding a "world query RPC" for agent clients — currently agents only see what the server broadcasts. If a behavior needs to query a block 100 blocks away that hasn't been chunked to the agent, it can't. A `C_QUERY_BLOCK` → `S_QUERY_RESULT` message pair would give agents targeted world queries without flooding them with all chunks.

---

## 8. World Generation

| Dimension | Terasology | AiCraft |
|-----------|-----------|---------|
| **Execution** | Server only | Server only |
| **Architecture** | Plugin system (`WorldGeneratorPlugin`), facet-based pipeline | Monolithic `WorldTemplate::generate()` per chunk |
| **Config** | Java WorldGenerator subclasses | Python artifact `worlds/base/village.py` |
| **Structures** | Module-defined structure templates | Hand-coded house/barn/portal generation |
| **Chunk demand** | On-demand when client enters range | On-demand via `getChunk()` → `generateChunk()` |
| **Determinism** | Seed-based | Seed-based |
| **Lighting** | Server generates, client processes pipeline | Lighting baked into vertex colors at mesh time |

**Terasology pros:** Facet pipeline means terrain, biomes, structures, decorations, and entities are separate passes that compose. A mod can inject a new structure pass without touching terrain code.

**AiCraft pros:** Single `generate()` function per chunk is easy to understand. Python config makes world tuning accessible without a rebuild.

**Lesson for AiCraft:** For multi-block structure generation (beds, chests placed by world gen), the current approach of calling `ctx.set()` multiple times works fine. No architectural change needed. The lesson is: **pass param2 correctly when world gen places multi-block objects** — both halves must have their `CompanionDir` bits set, exactly as the server would set them during player placement.

---

## Summary: What AiCraft Should Adopt

| Terasology Pattern | Adopt? | How |
|--------------------|--------|-----|
| LZ4 chunk compression | **Yes** | Compress `S_CHUNK` payload only; add `zstd` or `lz4` to `net_socket.h` |
| Atomic multi-block S_BLOCK | **Yes** | Add `onMultiBlockChange(vector<BlockChange>)` to `ServerCallbacks` for bed/door placement |
| Chunk invalidation message | **Yes (small)** | Add `C_RESYNC_CHUNK` + server resends single chunk on request |
| Per-viewer chest sync | **Done (differently)** | Chests are Structure entities; `S_INVENTORY` broadcasts on every change keep all clients in sync without a viewer set |
| Client position interpolation | **Later** | Low priority; needed for WAN multi-player |
| Client-side prediction | **No** | Adds complexity; not needed for AiCraft's scope |
| Protobuf serialization | **No** | Overkill; AiCraft's hand-written binary is fine at current scale |
| ECS component replication | **No** | AiCraft's explicit `EntityState` is simpler and sufficient |
| Items-as-entities | **No** | Counter model is correct for AiCraft's item design |
| Server-side behavior trees | **No** | AiCraft's per-process Python AI is a deliberate architectural strength |
| `@ServerEvent` annotation magic | **No** | Centralized `resolveActions()` switch is easier to audit |

---

## Impact on the Multi-Block + Chest Inventory Plan

Terasology's patterns directly inform three changes to `docs/25_MULTI_BLOCK_OBJECTS.md`:

1. **Atomic S_MULTI_BLOCK message** (from Terasology's block change batching):
   Add `S_MULTI_BLOCK` (0x100E): `[u32 count]` then `count × [i32 x][i32 y][i32 z][u32 blockId][u8 param2]`.
   Use this for bed placement (2 blocks), door placement, and any future N-block atomic change.
   This prevents the client from seeing a half-placed bed even for a single network tick.

2. **Chest viewer push** — *achieved without per-chest bookkeeping*:
   Chests became Structure entities with `has_inventory=true`. `S_INVENTORY`
   is already broadcast on every inventory change, so any client currently
   viewing a chest entity stays in sync automatically — no `m_chestViewers`
   map required.

3. **World-gen param2 discipline** (from Terasology's structure generation):
   World gen code in `world_template.h` must set correct param2 on both halves of every
   multi-block object it places. Add a `placeMultiBlock(ctx, pos, headId, footId, dir)`
   helper in `world_template.h` that sets both blocks + both param2 values atomically.
