# Network Performance — Implementation Plan

Three targeted improvements learned from Terasology's architecture.
Each is self-contained and can be implemented and tested independently.

---

## Current Baseline (Numbers to Beat)

| Metric | Current value |
|--------|---------------|
| `S_CHUNK` wire size | 16,404 bytes uncompressed per chunk |
| Initial world send | 81 chunks × 16.4 KB = **1.33 MB** per new player |
| `sentChunks` lifetime | Forever — set grows without bound, never evicted |
| Chunk invalidation | None — stale chunks on client have no recovery path |
| Protocol versioning | None — any format change breaks all clients |

---

## Optimization 1: zstd Chunk Compression

### What and Why

Chunk payloads (4,096 × u32 of block IDs) contain huge runs of identical values
(all-air above terrain, all-stone below, a few grass/dirt in the middle).
This is textbook zstd input. Expected compression ratios:

| World type | Uncompressed | Compressed (zstd lv1) | Saving |
|------------|-------------|----------------------|--------|
| Flat world | 16.4 KB | ~1.6 KB | 90% |
| Natural terrain | 16.4 KB | ~3.5 KB | 79% |
| Dense cave | 16.4 KB | ~5.5 KB | 67% |

On initial connect (81 chunks): 1.33 MB → ~150–280 KB. On a slow 10 Mbit/s WAN
link, initial load time drops from ~1.1 s to ~0.1 s.

Only `S_CHUNK` is compressed. All other messages are tiny (<300 bytes) —
the compression overhead would be larger than the gain.

### Library: zstd via FetchContent

zstd is pure C99, header+source only, compiles with Emscripten, and has a
one-shot API with no streaming complexity required.

### Step 1 — Add zstd to `CMakeLists.txt`

After the pybind11 block (around line 79), add:

```cmake
# zstd: fast lossless compression for S_CHUNK payloads
FetchContent_Declare(zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.6
    SOURCE_SUBDIR build/cmake
)
set(ZSTD_BUILD_PROGRAMS  OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zstd)
```

Then add `libzstd_static` to every target that touches network code.
Find the `target_link_libraries(modcraft-server ...)` block and add it.
Same for `modcraft` (player client), `modcraft-client`, and `modcraft-agent`.

```cmake
# In each target_link_libraries:
target_link_libraries(modcraft-server  PRIVATE ... libzstd_static)
target_link_libraries(modcraft         PRIVATE ... libzstd_static)
target_link_libraries(modcraft-client  PRIVATE ... libzstd_static)
target_link_libraries(modcraft-agent   PRIVATE ... libzstd_static)
```

### Step 2 — Protocol version handshake in `net_protocol.h`

**Purpose:** allow server to know whether the connecting client supports zstd.
Prevents old client binaries from receiving compressed chunks they can't parse.

In `src/shared/net_protocol.h`, add a protocol version constant and update
the `C_HELLO` and `S_WELCOME` documentation comments:

```cpp
// Protocol version — increment when wire format changes incompatibly.
// Clients send their version in C_HELLO; server rejects lower versions.
static constexpr uint16_t PROTOCOL_VERSION = 2;
// Version history:
//   1 = original (S_CHUNK uncompressed)
//   2 = S_CHUNK payload is zstd-compressed; C_HELLO adds uint16 version field
```

No struct change to `MsgHeader` — version lives in the C_HELLO payload, not
the per-message header (that would add 2 bytes to every message).

### Step 3 — Update `C_HELLO` serialization

**In `src/server/client_manager.h`**, the `C_HELLO` handler reads
`creatureType` and `name`. Add reading of the protocol version before those:

```cpp
case net::C_HELLO: {
    uint16_t clientVersion = rb.hasMore() ? rb.readU16() : 1; // default 1 = old client
    client.protocolVersion = clientVersion;
    client.supportsZstd = (clientVersion >= PROTOCOL_VERSION); // version 2+
    std::string creatureType = rb.readString();
    // ... rest unchanged ...
```

Add `uint16_t protocolVersion = 1` and `bool supportsZstd = false` to the
`ConnectedClient` struct.

**In the player client's C_HELLO sender** (find in `src/client/network_server.h`
or wherever the client sends C_HELLO), prepend the version field:

```cpp
net::WriteBuffer wb;
wb.writeU16(net::PROTOCOL_VERSION);  // NEW — must be first field
wb.writeString(creatureType);
wb.writeString(playerName);
net::sendMessage(fd, net::C_HELLO, wb);
```

**In agent_client.h's C_AGENT_HELLO** — agents don't receive chunks, so no
version needed there. Leave C_AGENT_HELLO unchanged.

### Step 4 — Compress `queueChunk()` in `client_manager.h`

Current `queueChunk()` is at line 653. Replace the message-building section:

```cpp
void queueChunk(ConnectedClient& cc, ChunkPos pos) {
    if (cc.sentChunks.count(pos)) return;
    Chunk* chunk = m_server.world().getChunk(pos);
    if (!chunk) return;

    // Build uncompressed payload
    net::WriteBuffer cb;
    cb.writeI32(pos.x); cb.writeI32(pos.y); cb.writeI32(pos.z);
    for (int i = 0; i < CHUNK_VOLUME; i++)
        cb.writeU32(((uint32_t)chunk->getRawParam2(i) << 16) | chunk->getRaw(i));

    // --- NEW: compress if client supports zstd ---
    std::vector<uint8_t> msgPayload;
    net::MsgType msgType = net::S_CHUNK;  // default: uncompressed

    if (cc.supportsZstd) {
        size_t srcSize = cb.size();
        size_t dstBound = ZSTD_compressBound(srcSize);
        std::vector<uint8_t> compressed(dstBound);
        // Level 1 = fastest compression (still 70-90% ratio on chunk data)
        size_t compressedSize = ZSTD_compress(
            compressed.data(), dstBound,
            cb.data().data(), srcSize,
            1 /* level */);
        if (!ZSTD_isError(compressedSize) && compressedSize < srcSize) {
            compressed.resize(compressedSize);
            msgPayload = std::move(compressed);
            msgType = net::S_CHUNK_Z;  // compressed variant
        } else {
            // Compression made it larger (shouldn't happen for chunks) — send raw
            msgPayload.assign(cb.data().begin(), cb.data().end());
        }
    } else {
        msgPayload.assign(cb.data().begin(), cb.data().end());
    }
    // --- end new ---

    // Build framed message: [8-byte header][payload]
    std::vector<uint8_t> msg;
    net::MsgHeader hdr;
    hdr.type = msgType;
    hdr.length = (uint32_t)msgPayload.size();
    msg.resize(8 + msgPayload.size());
    memcpy(msg.data(), &hdr, 8);
    memcpy(msg.data() + 8, msgPayload.data(), msgPayload.size());
    cc.pendingChunks.push_back({pos, std::move(msg)});
    cc.sentChunks.insert(pos);
}
```

Add `#include <zstd.h>` at the top of `client_manager.h`.

Add `S_CHUNK_Z = 0x100F` to the `MsgType` enum in `net_protocol.h`.

The `S_CHUNK_Z` payload is:
```
[zstd-compressed frame]
```
where the compressed frame decompresses to the same layout as `S_CHUNK`:
```
[i32 cx][i32 cy][i32 cz][u32 × 4096 blockId|param2]
```

### Step 5 — Decompress in both client handlers

**Player client — `src/client/network_server.h:370`:**

The `case net::S_CHUNK:` block exists at line 370. Add a parallel case for
`S_CHUNK_Z` just before it:

```cpp
case net::S_CHUNK_Z: {
    // Decompress zstd payload, then parse identical to S_CHUNK
    const auto& raw = rb.remainingBytes(); // NEW method — see below
    size_t decompBound = ZSTD_getFrameContentSize(raw.data(), raw.size());
    if (decompBound == ZSTD_CONTENTSIZE_ERROR || decompBound == ZSTD_CONTENTSIZE_UNKNOWN) {
        fprintf(stderr, "[Client] S_CHUNK_Z: invalid zstd frame\n");
        break;
    }
    std::vector<uint8_t> decomp(decompBound);
    size_t actual = ZSTD_decompress(decomp.data(), decompBound, raw.data(), raw.size());
    if (ZSTD_isError(actual)) {
        fprintf(stderr, "[Client] S_CHUNK_Z: decompression failed: %s\n",
                ZSTD_getErrorName(actual));
        break;
    }
    net::ReadBuffer zrb(decomp.data(), actual);
    int cx = zrb.readI32(), cy = zrb.readI32(), cz = zrb.readI32();
    ChunkPos cp = {cx, cy, cz};
    auto chunk = std::make_unique<Chunk>();
    for (int ly = 0; ly < CHUNK_SIZE; ly++)
        for (int lz = 0; lz < CHUNK_SIZE; lz++)
            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                uint32_t v = zrb.readU32();
                chunk->set(lx, ly, lz, (BlockId)(v & 0xFFFF), (uint8_t)((v >> 16) & 0xFF));
            }
    m_chunkData[cp] = std::move(chunk);
    if (m_onChunkDirty) m_onChunkDirty(cp);
    break;
}
case net::S_CHUNK: {
    // unchanged — handles old/uncompressed path
    ...
}
```

**Agent client — `src/agent/agent_client.h:268`:** Add the identical `S_CHUNK_Z`
case before the existing `S_CHUNK` case (same code, but writes to `m_chunks`
instead of `m_chunkData`).

**`ReadBuffer` needs `remainingBytes()` accessor.** In `net_protocol.h`, add:

```cpp
// Returns a span over the unread bytes (for passing to external decoders)
std::span<const uint8_t> remainingBytes() const {
    return {m_data + m_pos, m_data + m_size};
}
```

### Step 6 — Measure

After implementing, add a one-time log in `queueChunk()`:

```cpp
static bool logged = false;
if (!logged && cc.supportsZstd) {
    printf("[Server] Chunk compression: %zu → %zu bytes (%.0f%%)\n",
           cb.size(), msgPayload.size(),
           100.0 * (1.0 - (double)msgPayload.size() / cb.size()));
    logged = true;
}
```

Expected output: `Chunk compression: 16384 → 3200 bytes (80%)`

---

## Optimization 2: View-Distance Streaming + Chunk Invalidation

### What and Why

**Problem 1 — `sentChunks` grows forever:**
After an hour of play (walking around), a player has thousands of entries in
`sentChunks`. Those far-away chunks will never be re-sent, but they consume
memory and mean the player gets stale terrain if they return to an old area
(e.g. after a TNT explosion or a world gen edge case).

**Problem 2 — No chunk invalidation:**
The server has no way to tell a client "throw away chunk (3,0,7)". Terasology
uses `InvalidateChunkMessage` for this. Without it, multi-block operations that
change terrain in bulk (explosions, future admin commands) leave the client in
a permanently wrong state.

**Problem 3 — Chunk queue doesn't skip if pendingChunks non-empty:**
The streaming loop at `client_manager.h:257` does `if (!client.pendingChunks.empty()) continue`.
This means: while the initial 81 chunks are draining, zero new chunks are queued
— including chunks directly under the player's feet that were just generated.

**Problem 4 — No priority in chunk queue:**
The streaming scan is a flat `dx/dz/dy` triple loop. Chunks directly in front
of the player are no more urgent than chunks behind. Nearest-first feels much
better in practice.

### New message types

Add to `net_protocol.h` enum:

```cpp
C_RESYNC_CHUNK = 0x0007,  // client requests a chunk be re-sent: [i32 cx][i32 cy][i32 cz]
S_CHUNK_EVICT  = 0x100E,  // server tells client to discard a chunk: [i32 cx][i32 cy][i32 cz]
```

(`C_RESYNC_CHUNK` is 0x0007; the previously planned `C_OPEN_CHEST` will use 0x0009 instead,
since chest inventory is a later feature.)

### Step 1 — Track view distance and evict far chunks on the server

Add to `ConnectedClient` struct:

```cpp
static constexpr int STREAM_R   = 6;   // chunks to stream (existing value)
static constexpr int EVICT_R    = 9;   // chunks to keep in sentChunks (3 extra buffer)
static constexpr int EVICT_DY   = 3;   // vertical eviction radius
```

Add a new method `evictFarChunks()` to `ClientManager`:

```cpp
void evictFarChunks(ConnectedClient& cc, ChunkPos center) {
    std::vector<ChunkPos> toEvict;
    for (auto& pos : cc.sentChunks) {
        if (std::abs(pos.x - center.x) > ConnectedClient::EVICT_R ||
            std::abs(pos.z - center.z) > ConnectedClient::EVICT_R ||
            std::abs(pos.y - center.y) > ConnectedClient::EVICT_DY) {
            toEvict.push_back(pos);
        }
    }
    if (toEvict.empty()) return;

    for (auto& pos : toEvict) {
        cc.sentChunks.erase(pos);
        // Tell client to discard the chunk too
        net::WriteBuffer wb;
        wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
        net::sendMessage(cc.fd, net::S_CHUNK_EVICT, wb);
    }
    // Also cancel pending chunks that are now out of range
    cc.pendingChunks.erase(
        std::remove_if(cc.pendingChunks.begin(), cc.pendingChunks.end(),
            [&](auto& p) {
                return std::find(toEvict.begin(), toEvict.end(), p.first) != toEvict.end();
            }),
        cc.pendingChunks.end());
}
```

### Step 2 — Call eviction when player moves in `broadcastState()`

The broadcast loop already computes `cp` at line 298. Replace the existing
streaming block (lines 296–310) with:

```cpp
// Stream chunks around player
if (pe) {
    auto cp = worldToChunk((int)pe->position.x, (int)pe->position.y, (int)pe->position.z);

    // Evict far chunks when player moves to a new chunk position
    if (cp != client.lastChunkPos) {
        client.lastChunkPos = cp;
        evictFarChunks(client, cp);
    }

    // Queue nearby chunks — sorted nearest-first for better perceived load time
    if (client.pendingChunks.size() < 20) {
        struct Candidate { ChunkPos pos; int dist; };
        std::vector<Candidate> candidates;

        for (int dy = -1; dy <= 2; dy++)
        for (int dz = -ConnectedClient::STREAM_R; dz <= ConnectedClient::STREAM_R; dz++)
        for (int dx = -ConnectedClient::STREAM_R; dx <= ConnectedClient::STREAM_R; dx++) {
            ChunkPos pos = {cp.x + dx, cp.y + dy, cp.z + dz};
            if (client.sentChunks.count(pos)) continue;
            // Skip if chunk hasn't been generated yet (avoids forcing generation)
            if (!m_server.world().hasChunk(pos)) continue;
            int dist = std::abs(dx) + std::abs(dy*4) + std::abs(dz); // manhattan, weight Y less
            candidates.push_back({pos, dist});
        }

        // Sort nearest-first
        std::sort(candidates.begin(), candidates.end(),
                  [](auto& a, auto& b) { return a.dist < b.dist; });

        // Queue up to 4 new chunks this tick
        int queued = 0;
        for (auto& c : candidates) {
            if (queued >= 4) break;
            if (client.pendingChunks.size() >= 20) break;
            queueChunk(client, c.pos);
            queued++;
        }
    }
}
```

Note: `m_server.world().hasChunk(pos)` needs to be added to `World`:

```cpp
// In src/server/world.h — non-generating variant
bool hasChunk(ChunkPos pos) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_chunks.count(pos) > 0;
}
```

This prevents the streaming loop from forcing generation of all chunks in the
radius — only chunks that already exist (generated by world gen on startup or
by a previous player visit) are queued.

### Step 3 — Handle `C_RESYNC_CHUNK` on the server

In the message dispatch switch in `ClientManager::processMessages()`:

```cpp
case net::C_RESYNC_CHUNK: {
    int cx = rb.readI32(), cy = rb.readI32(), cz = rb.readI32();
    ChunkPos pos = {cx, cy, cz};
    // Remove from sentChunks so it gets re-queued on next broadcastState
    client.sentChunks.erase(pos);
    // Cancel any pending send of the old version
    client.pendingChunks.erase(
        std::remove_if(client.pendingChunks.begin(), client.pendingChunks.end(),
            [&](auto& p) { return p.first == pos; }),
        client.pendingChunks.end());
    break;
}
```

### Step 4 — Handle `S_CHUNK_EVICT` on both clients

**Player client `src/client/network_server.h`** — add after `S_CHUNK` handler:

```cpp
case net::S_CHUNK_EVICT: {
    ChunkPos cp = {rb.readI32(), rb.readI32(), rb.readI32()};
    m_chunkData.erase(cp);
    if (m_onChunkDirty) m_onChunkDirty(cp);  // triggers mesh rebuild (will show hole → ok)
    break;
}
```

**Agent client `src/agent/agent_client.h`** — add after `S_CHUNK` handler:

```cpp
case net::S_CHUNK_EVICT: {
    ChunkPos cp = {rb.readI32(), rb.readI32(), rb.readI32()};
    m_chunks.erase(cp);
    break;
}
```

### Step 5 — Server-side forced invalidation API

For admin commands, explosions, and future world-modifying operations that
change many blocks at once, add a method to `ClientManager`:

```cpp
// Force all clients to discard and re-request a chunk.
// Use after bulk block changes (explosion, admin fill command).
void invalidateChunkForAll(ChunkPos pos) {
    for (auto& [cid, client] : m_clients) {
        if (!client.sentChunks.count(pos)) continue;  // client never had it
        client.sentChunks.erase(pos);
        // Cancel any in-flight stale version
        client.pendingChunks.erase(
            std::remove_if(client.pendingChunks.begin(), client.pendingChunks.end(),
                [&](auto& p) { return p.first == pos; }),
            client.pendingChunks.end());
        // Tell client to discard
        net::WriteBuffer wb;
        wb.writeI32(pos.x); wb.writeI32(pos.y); wb.writeI32(pos.z);
        net::sendMessage(client.fd, net::S_CHUNK_EVICT, wb);
    }
}
```

Expose this on `GameServer` via a callback (similar to `onBlockChange`):

```cpp
// In GameServer::ServerCallbacks:
std::function<void(ChunkPos)> onChunkInvalidate;
```

---

## Optimization 3: Protocol Versioning + Structured Schema

### What and Why

**Problem:** There is no way to add a new field to any message without breaking
all existing clients. `C_HELLO` silently gets garbage data if an old client
connects to a new server. There is no error, no fallback — the server just
misreads a string as a version number.

**What we want (from Terasology):** protobuf gives schema evolution (add optional
fields freely) and a machine-readable schema file. We won't adopt full protobuf,
but we will achieve:
1. Explicit version negotiation on connect
2. `rb.hasMore()` guards on new optional fields (already used in `S_BLOCK`)
3. A human-readable schema doc that is the single source of truth

### Step 1 — Version negotiation in C_HELLO / S_WELCOME

Already described in Optimization 1 Step 2. `C_HELLO` sends `uint16 version`
as its first field. Server records `client.protocolVersion`. If version <
minimum supported, server sends `S_ERROR` with message "version too old" and
closes the connection.

Add `PROTOCOL_VERSION_MIN = 1` to `net_protocol.h` for the minimum version
the server will accept.

```cpp
static constexpr uint16_t PROTOCOL_VERSION     = 2;  // current
static constexpr uint16_t PROTOCOL_VERSION_MIN = 1;  // oldest accepted
```

### Step 2 — Structured schema comments in `net_protocol.h`

Replace the current brief comment block at the top with a full schema table
that serves as the authoritative wire format reference. This is `net_protocol.h`
itself acting as the `.proto` file.

Format for each message:

```cpp
// ── S_CHUNK_Z (0x100F) ── Server → Client ──────────────────────────────
// Compressed chunk (zstd). Protocol version 2+. Decompresses to S_CHUNK layout.
//
//   [zstd_frame]           ← entire payload is one zstd frame
//
// Decompressed layout (identical to S_CHUNK payload):
//   i32  chunk_x
//   i32  chunk_y
//   i32  chunk_z
//   u32[4096]              ← (param2 << 16) | blockId, Y-major order
```

All 18 existing messages get this treatment in the same file. Total: ~80 lines
of structured comments that replace the current 5-line summary.

### Step 3 — `hasMore()` pattern for optional fields

`ReadBuffer::hasMore()` already exists and is already used in `S_BLOCK`
(line 397 of `network_server.h`). Enforce its use as the standard way to add
fields to an existing message without incrementing the version:

```cpp
// PATTERN: adding a field to an existing message
// Old:  [string name]
// New:  [string name][string skin]  — skin is optional, guarded by hasMore()
//
// Sender:
wb.writeString(name);
if (includeSkin) wb.writeString(skin);  // only send if new feature enabled
//
// Receiver:
std::string name = rb.readString();
std::string skin = rb.hasMore() ? rb.readString() : "";  // safe fallback
```

This keeps backward compat without a version bump for purely additive changes.
A version bump is only needed when a field is removed or reordered.

### Step 4 — `messages.schema` (new file)

Create `src/shared/messages.schema` — a plain-text file, no build step, purely
for human reference:

```
# AiCraft Wire Protocol — Message Schema
# Format: DIRECTION MSG_TYPE (HEX_ID) "Description"
#         [field_type  field_name  — notes]
# Types: u8, u16, u32, i32, f32, bool(u8), str(u32len+bytes), vec3(3×f32), ivec3(3×i32)
# Version: added in which protocol version (if omitted, version 1)

C→S  C_HELLO (0x0001)  "GUI player identifies itself"
     u16   version        — PROTOCOL_VERSION (added v2)
     str   creature_type  — "base:player" etc
     str   display_name

C→S  C_ACTION (0x0001) "Entity action proposal"  [see action.h for full layout]
     u32   action_type
     ...

S→C  S_CHUNK (0x1003) "Uncompressed chunk (protocol v1 clients only)"
     i32   chunk_x
     i32   chunk_y
     i32   chunk_z
     u32[4096]  blocks    — (param2<<16)|blockId, Y-major

S→C  S_CHUNK_Z (0x100F) "zstd-compressed chunk (protocol v2+)"  [v2]
     byte[]  zstd_frame   — decompresses to S_CHUNK payload layout

S→C  S_CHUNK_EVICT (0x100E) "Discard chunk from client cache"  [v2]
     i32   chunk_x
     i32   chunk_y
     i32   chunk_z

C→S  C_RESYNC_CHUNK (0x0007) "Request chunk re-send"  [v2]
     i32   chunk_x
     i32   chunk_y
     i32   chunk_z
```

This file lives alongside `net_protocol.h`, is checked in, and updated whenever
`net_protocol.h` changes. It is the answer to "what does message X look like"
without reading C++ code.

---

## Implementation Order

| Order | Task | Files changed | Estimated compile/test |
|-------|------|--------------|------------------------|
| 1 | Add zstd to CMakeLists.txt | `CMakeLists.txt` | cmake reconfigure |
| 2 | Add `PROTOCOL_VERSION`, `S_CHUNK_Z`, `S_CHUNK_EVICT`, `C_RESYNC_CHUNK` to enum | `net_protocol.h` | header-only → touch builtin.cpp |
| 3 | Update `C_HELLO` sender (player client) and handler (server) | `network_server.h`, `client_manager.h` | rebuild |
| 4 | Compress `queueChunk()` on server | `client_manager.h` | rebuild server |
| 5 | Decompress `S_CHUNK_Z` on player client | `network_server.h` | rebuild client |
| 6 | Decompress `S_CHUNK_Z` on agent client | `agent_client.h` | rebuild agent |
| 7 | Add `evictFarChunks()` + update broadcast loop | `client_manager.h` | rebuild server |
| 8 | Handle `S_CHUNK_EVICT` on both clients | `network_server.h`, `agent_client.h` | rebuild |
| 9 | Handle `C_RESYNC_CHUNK` on server | `client_manager.h` | rebuild server |
| 10 | Add `hasChunk()` to World | `world.h` | rebuild server |
| 11 | Add `invalidateChunkForAll()` to ClientManager | `client_manager.h` | rebuild |
| 12 | Add schema comments to net_protocol.h + write messages.schema | `net_protocol.h`, `src/shared/messages.schema` | docs only |

Steps 1–6 are the compression path (biggest win). Steps 7–11 are the eviction
path (correctness). Step 12 is documentation only.

Start with steps 1–6. Verify with `make game` that initial load is visibly
faster and the log shows compression ratio. Then add 7–11.

---

## Testing

### Compression

```bash
# Start game, watch server log
make game
# Look for: [Server] Chunk compression: 16384 → XXXX bytes (YY%)
# Expected: ~3000 bytes (81%) for natural terrain
```

### Eviction

```bash
# Walk 200 blocks, check memory via /proc
grep VmRSS /proc/$(pgrep modcraft-server)/status
# sentChunks should cap at ~(2*9+1)^2 * 2 = 722 entries per client
# not grow to thousands
```

### Invalidation

```bash
# After implementing S_CHUNK_EVICT, test:
# 1. Join world, walk 100 blocks
# 2. Call invalidateChunkForAll() on a near chunk (add a debug key binding)
# 3. Verify client re-requests and re-renders the chunk
```

### Protocol version rejection

```cpp
// In a test: set client PROTOCOL_VERSION to 0, verify server sends S_ERROR
// and closes connection cleanly, not undefined behavior
```

---

## What This Does NOT Change

- The `ActionProposal` / `S_ENTITY` / `S_BLOCK` paths are unchanged — these are
  already small messages and don't benefit from compression
- No client-side prediction is added — AiCraft stays fully server-authoritative
- The agent client AI architecture is unchanged — only its chunk handling improves
- WASM build: zstd compiles with Emscripten; `S_CHUNK_Z` decompression is included
  in the WASM player client since it only receives, never compresses
