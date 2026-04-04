# Agentica - Networking

How the C++ server and Python clients communicate. TCP-based with delta compression.

---

## 1. Protocol Choice

```
Luanti uses custom reliable UDP (MTP).
Agentica uses TCP + custom binary framing.

Why TCP instead of UDP:
  - Simpler implementation (reliability built-in)
  - Python's asyncio/socket handles TCP natively
  - Chunk data is large and must be reliable anyway
  - Player position updates are small and frequent,
    but TCP with Nagle disabled is fast enough at 20Hz tick rate
  - Fewer edge cases (packet reordering, duplicate handling)

Tradeoff: slightly higher latency (~5-10ms) vs UDP.
Acceptable for a 20Hz tick rate game with client-side prediction.
```

---

## 2. Packet Format

```
Packet Layout:
+--------+--------+--------+--------+--------+---
| Length (4 bytes, u32 big-endian)  | Type   | Payload...
|         (of entire packet)        | (u16)  | (protobuf or msgpack)
+--------+--------+--------+--------+--------+---

Max packet size: 1 MB (configurable)
Compression: zstd for packets > 256 bytes
```

---

## 3. Packet Types

### Client -> Server (TOSERVER_*)

```
+------+----------------------------------+---------------------------+
| ID   | Name                             | Payload                   |
+------+----------------------------------+---------------------------+
| 0x01 | TOSERVER_INIT                    | protocol_ver, player_name |
| 0x02 | TOSERVER_AUTH                    | password_hash, token      |
| 0x10 | TOSERVER_PLAYER_POS              | pos, velocity, yaw, pitch |
| 0x11 | TOSERVER_PLAYER_ACTION           | action_type, action_data  |
| 0x12 | TOSERVER_CHAT                    | message (string)          |
| 0x13 | TOSERVER_INVENTORY_ACTION        | action (move/drop/craft)  |
| 0x20 | TOSERVER_UPLOAD_ARTIFACT         | type, source, assets[]    |
| 0x21 | TOSERVER_DELETE_ARTIFACT         | artifact_id               |
| 0x22 | TOSERVER_REQUEST_CHUNK           | chunk_pos                 |
| 0x23 | TOSERVER_REQUEST_ASSET           | asset_hash                |
| 0x30 | TOSERVER_KEEPALIVE               | (empty)                   |
| 0x31 | TOSERVER_DISCONNECT              | reason (string)           |
+------+----------------------------------+---------------------------+
```

### Server -> Client (TOCLIENT_*)

```
+------+----------------------------------+---------------------------+
| ID   | Name                             | Payload                   |
+------+----------------------------------+---------------------------+
| 0x01 | TOCLIENT_INIT_ACCEPTED           | player_id, spawn_pos      |
| 0x02 | TOCLIENT_AUTH_REQUIRED            | auth_methods              |
| 0x03 | TOCLIENT_KICK                    | reason (string)           |
| 0x10 | TOCLIENT_CHUNK_DATA              | chunk_pos, compressed     |
| 0x11 | TOCLIENT_BLOCK_CHANGE            | pos, new_block_type,      |
|      |                                  | new_attrs                 |
| 0x12 | TOCLIENT_BLOCK_CHANGE_BATCH      | [(pos, type, attrs), ...] |
| 0x20 | TOCLIENT_ENTITY_ADD              | entity_id, type, pos,     |
|      |                                  | attrs, velocity           |
| 0x21 | TOCLIENT_ENTITY_REMOVE           | entity_id                 |
| 0x22 | TOCLIENT_ENTITY_UPDATE           | entity_id, dirty_fields   |
| 0x23 | TOCLIENT_ENTITY_UPDATE_BATCH     | [(id, fields), ...]       |
| 0x24 | TOCLIENT_ENTITY_MOVE             | entity_id, pos, vel, yaw  |
| 0x25 | TOCLIENT_ENTITY_MOVE_BATCH       | [(id, pos, vel, yaw),...] |
| 0x30 | TOCLIENT_PLAYER_INFO             | hp, hunger, stamina, xp   |
| 0x31 | TOCLIENT_INVENTORY                | full inventory data       |
| 0x32 | TOCLIENT_MOVE_PLAYER             | pos (server correction)   |
| 0x40 | TOCLIENT_TIME_OF_DAY             | time_of_day, day_count    |
| 0x41 | TOCLIENT_WEATHER                 | type, intensity, wind     |
| 0x42 | TOCLIENT_CHAT                    | sender, message           |
| 0x50 | TOCLIENT_CONTENT_DEFS            | [ObjectMeta/ActionMeta]   |
| 0x51 | TOCLIENT_NEW_CONTENT             | type_id, ObjectMeta,      |
|      |                                  | asset_manifest            |
| 0x52 | TOCLIENT_REMOVE_CONTENT          | type_id                   |
| 0x53 | TOCLIENT_ASSET_DATA              | asset_hash, binary_data   |
| 0x60 | TOCLIENT_SOUND_PLAY              | sound_name, pos, gain     |
| 0x61 | TOCLIENT_PARTICLES               | particle_spec, pos        |
| 0x62 | TOCLIENT_ACTION_RESULT           | action_id, success, error |
| 0x70 | TOCLIENT_KEEPALIVE               | (empty)                   |
+------+----------------------------------+---------------------------+
```

---

## 4. Connection Lifecycle

```
Client                                           Server
  |                                                 |
  |  TCP connect to server:port                     |
  |================================================>|
  |                                                 |
  |  TOSERVER_INIT (protocol_ver, name)             |
  |------------------------------------------------>|
  |                                                 |
  |             TOCLIENT_AUTH_REQUIRED (methods)     |
  |<------------------------------------------------|
  |                                                 |
  |  TOSERVER_AUTH (hash, token)                    |
  |------------------------------------------------>|
  |                                                 |
  |             TOCLIENT_INIT_ACCEPTED              |
  |             (player_id, spawn_pos)              |
  |<------------------------------------------------|
  |                                                 |
  |  Phase: CONTENT DOWNLOAD                        |
  |                                                 |
  |             TOCLIENT_CONTENT_DEFS               |
  |             (all ObjectMeta + ActionMeta)        |
  |<------------------------------------------------|
  |                                                 |
  |  TOSERVER_REQUEST_ASSET (for each missing)      |
  |------------------------------------------------>|
  |             TOCLIENT_ASSET_DATA (textures,etc)  |
  |<------------------------------------------------|
  |                                                 |
  |  Phase: CHUNK LOADING                           |
  |                                                 |
  |             TOCLIENT_CHUNK_DATA (spawn area)    |
  |             TOCLIENT_CHUNK_DATA ...             |
  |             TOCLIENT_CHUNK_DATA ...             |
  |<------------------------------------------------|
  |                                                 |
  |  Phase: GAMEPLAY                                |
  |                                                 |
  |  TOSERVER_PLAYER_POS (~20/s)                    |
  |------------------------------------------------>|
  |             TOCLIENT_ENTITY_UPDATE_BATCH (~20/s)|
  |<------------------------------------------------|
  |  TOSERVER_PLAYER_ACTION (mine, place, etc.)     |
  |------------------------------------------------>|
  |             TOCLIENT_ACTION_RESULT              |
  |             TOCLIENT_BLOCK_CHANGE               |
  |<------------------------------------------------|
  |                                                 |
  |  ... game continues ...                         |
```

---

## 5. Delta Compression

Only changed data is sent each tick:

### Block Changes

```
Instead of sending full chunk data on every change:

  TOCLIENT_BLOCK_CHANGE:
    pos: (10, 5, -3)                   # 12 bytes
    new_type_id: 0                     # 2 bytes (0 = air, block was mined)

  TOCLIENT_BLOCK_CHANGE_BATCH:
    count: 3
    changes: [
      {pos: (10, 5, -3), type: 0},
      {pos: (10, 5, -4), type: 0},
      {pos: (10, 5, -5), type: 0},
    ]
    # ~42 bytes for 3 block changes, vs 16KB+ for full chunk
```

### Entity Updates (Dirty Field Tracking)

```
Pig's hunger changed from 0.8 to 0.5:

  Full entity sync: ~200 bytes (all fields)
  Delta sync:       ~12 bytes

  TOCLIENT_ENTITY_UPDATE:
    entity_id: 42                      # 4 bytes
    field_count: 1                     # 1 byte
    fields: [
      {field_id: 3, value: 0.5}       # 1 + 4 bytes (hunger)
    ]

Batch version groups updates for multiple entities:

  TOCLIENT_ENTITY_UPDATE_BATCH:
    count: 5
    updates: [
      {id: 42, fields: [{3, 0.5}]},
      {id: 43, fields: [{1, 8.0}]},   # hp changed
      ...
    ]
```

### Position Updates (Packed Format)

```
Entity movement is the most frequent update.
Packed into a compact format:

  TOCLIENT_ENTITY_MOVE_BATCH:
    count: 10
    entries: [
      {                                # 22 bytes per entity
        entity_id: u32,                # 4
        x: f32, y: f32, z: f32,       # 12 (position)
        yaw: u16,                      # 2 (angle, 0-65535 maps to 0-360)
        vx: i16, vy: i16, vz: i16,    # 6 (velocity, fixed-point)
      },
      ...
    ]

At 20Hz with 30 entities visible: ~660 bytes/tick = ~13 KB/s
```

---

## 6. Chunk Data Transfer

```
Chunk data is the biggest payload. Compressed with zstd:

  Raw chunk:  4096 * 2 (type_ids) + 4096 * N (params) = ~16-32 KB
  Compressed: typically 1-4 KB (terrain is repetitive)

  TOCLIENT_CHUNK_DATA:
    chunk_pos: (x, y, z)              # 12 bytes
    compression: "zstd"               # 1 byte
    data_len: u32                     # 4 bytes
    data: bytes                       # 1-4 KB compressed

  Initial load (16-chunk radius, full sphere):
    ~4000 chunks * ~2 KB avg = ~8 MB
    At 10 Mbps: ~6 seconds
    Sent in priority order: spawn area first, then expanding outward
```

---

## 7. Artifact Upload Protocol

```
When a player uploads a new Object or Action:

  TOSERVER_UPLOAD_ARTIFACT:
    type: "object" | "action"          # 1 byte
    source_code: str                   # Python source
    asset_count: u16                   # number of assets
    assets: [
      {
        name: "flying_pig.png"         # filename
        mime: "image/png"              # type
        data: bytes                    # binary content
      },
      ...
    ]

  Server responds with:
    TOCLIENT_ACTION_RESULT:
      action_id: "upload"
      success: true/false
      error: "..." (if failed)

  On success, server broadcasts to ALL clients:
    TOCLIENT_NEW_CONTENT:
      type_id: "alice:flying_pig"
      meta: ObjectMeta(...)            # serialized definition
      asset_manifest: [
        {name: "flying_pig.png", hash: "a1b2c3..."},
      ]

  Other clients then request missing assets:
    TOSERVER_REQUEST_ASSET: hash="a1b2c3..."
    TOCLIENT_ASSET_DATA: hash="a1b2c3...", data=bytes
```
