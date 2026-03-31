# Luanti Engine Architecture

How the engine works under the hood: loops, timing, threading, and data flow.

---

## Table of Contents

1. [Timing Model: Not Framerate-Locked](#1-timing-model)
2. [The Two Loops](#2-the-two-loops)
3. [Client Game Loop](#3-client-game-loop)
4. [Server Loop](#4-server-loop)
5. [Threading Model](#5-threading-model)
6. [ServerEnvironment Step](#6-serverenvironment-step)
7. [Map System & Block Lifecycle](#7-map-system--block-lifecycle)
8. [Map Generation (Emerge)](#8-map-generation-emerge)
9. [Mesh Generation Pipeline](#9-mesh-generation-pipeline)
10. [Networking (MTP Protocol)](#10-networking-mtp-protocol)
11. [Object System (SAO/CAO)](#11-object-system-saocao)
12. [Lua Scripting Integration](#12-lua-scripting-integration)
13. [Timing Reference](#13-timing-reference)

---

## 1. Timing Model

**The engine uses a variable-timestep model, NOT a fixed framerate tick.**

Every loop iteration computes `dtime` -- the wall-clock seconds elapsed since the last iteration. This `dtime` is passed to every subsystem's `step()` function. Subsystems accumulate `dtime` and fire their logic when their individual interval threshold is reached.

```
Frame N                          Frame N+1                        Frame N+2
  |-------- dtime=16ms ------------|-------- dtime=18ms ------------|
  |                                |                                |
  step(0.016)                      step(0.018)                      ...
```

There is a safety cap: `DTIME_LIMIT = 2.5 seconds`. If a frame takes longer than 2.5s (e.g. during a freeze), dtime is clamped to prevent physics explosions.

**Client and server have DIFFERENT tick rates:**

| Component | Tick Rate | How |
|-----------|-----------|-----|
| Client (render) | ~60 FPS (configurable) | Sleeps to hit `fps_max` target |
| Client (unfocused) | ~10 FPS | Sleeps to hit `fps_max_unfocused` |
| Server (dedicated) | ~11 Hz | `dedicated_server_step = 0.09s` |
| Server (singleplayer) | Up to 60 Hz | Tied to client: `1/fps_max` |

---

## 2. The Two Loops

The engine always runs a client/server pair. Even singleplayer starts a local server.

```
                    +-----------------------+
                    |      main()           |
                    |   src/main.cpp        |
                    +-----------+-----------+
                                |
                    +-----------+-----------+
                    |                       |
            Dedicated server?          Client mode?
                    |                       |
                    v                       v
        +-------------------+    +---------------------+
        | run_dedicated_    |    | ClientLauncher::     |
        | server()          |    | run()                |
        |                   |    |                      |
        | Creates Server    |    | Main menu -> Game    |
        | Enters sleep loop |    | Creates local Server |
        +-------------------+    | + Client             |
                                 | Enters Game::run()   |
                                 +---------------------+
```

In singleplayer, both loops run in the same process:
- **ServerThread** runs `AsyncRunStep()` on its own thread
- **Main thread** runs the client render loop (`Game::run()`)

---

## 3. Client Game Loop

`Game::run()` in `src/client/game.cpp` -- the main rendering and input loop.

```
+================================================================+
|                    Game::run()  (Main Thread)                   |
|================================================================|
|                                                                 |
|  while (running) {                                              |
|      +--------------------------------------------------+      |
|      |  1. FPS Limiter                                   |      |
|      |     Sleep until target frame time reached         |      |
|      |     Compute dtime = wall_clock_delta (seconds)    |      |
|      +--------------------------------------------------+      |
|                          |                                      |
|      +--------------------------------------------------+      |
|      |  2. Input Processing                              |      |
|      |     - Keyboard, mouse events                      |      |
|      |     - Camera direction update                     |      |
|      |     - Player controls (WASD, jump, etc.)          |      |
|      +--------------------------------------------------+      |
|                          |                                      |
|      +--------------------------------------------------+      |
|      |  3. Game::step(dtime)                             |      |
|      |     - If singleplayer: server->step(dtime)        |      |
|      |       (checks async errors, server runs on its    |      |
|      |        own thread via ServerThread)                |      |
|      |     - client->step(dtime)                         |      |
|      |       - ReceiveAll() network packets              |      |
|      |       - ClientEnvironment::step(dtime)            |      |
|      |         - Physics sub-stepping (max 10ms steps)   |      |
|      |         - Player movement + collision             |      |
|      |         - Active object stepping                  |      |
|      +--------------------------------------------------+      |
|                          |                                      |
|      +--------------------------------------------------+      |
|      |  4. Process Server Events                         |      |
|      |     - Handle TOCLIENT_* packets (chat, inventory, |      |
|      |       block data, object updates, etc.)           |      |
|      +--------------------------------------------------+      |
|                          |                                      |
|      +--------------------------------------------------+      |
|      |  5. Interaction                                   |      |
|      |     - Digging progress, node placement            |      |
|      |     - Item use, object interaction                 |      |
|      +--------------------------------------------------+      |
|                          |                                      |
|      +--------------------------------------------------+      |
|      |  6. Rendering                                     |      |
|      |     - Update camera, sky, clouds                  |      |
|      |     - Apply completed meshes from worker threads  |      |
|      |     - Draw scene via Irrlicht                     |      |
|      |     - HUD overlay                                 |      |
|      +--------------------------------------------------+      |
|  }                                                              |
+================================================================+
```

### Client Physics Sub-stepping

Player physics don't use `dtime` directly. Instead, `dtime` is subdivided into steps of at most **10ms** to ensure stable collision detection:

```
dtime = 33ms (e.g. at 30 FPS)

Sub-step 1:  move(0.010)  ->  collide  ->  update position
Sub-step 2:  move(0.010)  ->  collide  ->  update position
Sub-step 3:  move(0.010)  ->  collide  ->  update position
Sub-step 4:  move(0.003)  ->  collide  ->  update position
```

This prevents players from tunneling through walls at low framerates.

---

## 4. Server Loop

`ServerThread::run()` in `src/server.cpp` -- runs on a dedicated thread.

```
+================================================================+
|                  ServerThread  (Server Thread)                  |
|================================================================|
|                                                                 |
|  Initial AsyncRunStep(0.0, true)   // bootstrap                 |
|                                                                 |
|  while (!stopRequested) {                                       |
|      +--------------------------------------------------+      |
|      |  1. Compute dtime                                 |      |
|      |     dtime = time_since_last_iteration (seconds)   |      |
|      +--------------------------------------------------+      |
|                          |                                      |
|      +--------------------------------------------------+      |
|      |  2. Yield if overloaded                           |      |
|      |     If dtime > steplen AND emerge queue >= 32:    |      |
|      |       sleep(1ms) up to 10x to let emerge threads  |      |
|      |       acquire the envlock                         |      |
|      +--------------------------------------------------+      |
|                          |                                      |
|      +--------------------------------------------------+      |
|      |  3. AsyncRunStep(dtime)                           |      |
|      |     (see Section 6 for full breakdown)            |      |
|      |     - Send blocks to clients                      |      |
|      |     - Environment step (ABMs, timers, objects)    |      |
|      |     - Liquid transforms                           |      |
|      |     - Object add/remove for clients               |      |
|      |     - Save map/mod data periodically              |      |
|      +--------------------------------------------------+      |
|                          |                                      |
|      +--------------------------------------------------+      |
|      |  4. Receive(remaining_time)                       |      |
|      |     Block on UDP socket for remainder of steplen  |      |
|      |     Process all incoming TOSERVER_* packets       |      |
|      |     This is how the server paces itself --        |      |
|      |     fast steps = more receive time                |      |
|      +--------------------------------------------------+      |
|  }                                                              |
+================================================================+

steplen = 0.09s (dedicated) or 1/fps_max (singleplayer)

Timeline for one server tick at 11 Hz:
|<------- 90ms total ------->|
|<-- AsyncRunStep -->|<- Receive (idle, processing packets) ->|
|     ~20-40ms       |              ~50-70ms                  |
```

---

## 5. Threading Model

```
+------------------------------------------------------------------+
|                        PROCESS                                    |
|                                                                   |
|  +---------------------------+    +----------------------------+  |
|  |     Main Thread           |    |     ServerThread            |  |
|  |                           |    |                            |  |
|  |  Game::run()              |    |  AsyncRunStep()            |  |
|  |  - Input                  |    |  - Environment step        |  |
|  |  - Client::step()         |    |  - ABMs, node timers       |  |
|  |  - Apply mesh results     |    |  - Object management       |  |
|  |  - Rendering              |    |  - Liquid transforms       |  |
|  |  - Interaction            |    |  - Block send/save         |  |
|  |                           |    |  Receive()                 |  |
|  +---------------------------+    |  - Process TOSERVER_*      |  |
|                                   +----------------------------+  |
|                                              |                    |
|                                         [envlock]                 |
|                                              |                    |
|  +---------------------------+    +----------------------------+  |
|  |  MeshUpdateWorkerThreads  |    |     EmergeThreads          |  |
|  |  (1-3 threads)            |    |     (1-4 threads)          |  |
|  |                           |    |                            |  |
|  |  Pop from MeshUpdateQueue |    |  Load blocks from DB       |  |
|  |  Copy voxel data          |    |  Run mapgen                |  |
|  |  Build triangle meshes    |    |  Call on_generated (Lua)   |  |
|  |  Push to result queue     |    |  Each has own Lua state    |  |
|  +---------------------------+    +----------------------------+  |
|                                                                   |
|  +---------------------------+    +----------------------------+  |
|  |  ConnectionSendThread     |    |  ConnectionReceiveThread   |  |
|  |                           |    |                            |  |
|  |  Serialize packets        |    |  Read UDP socket           |  |
|  |  Fragment large data      |    |  Reassemble split packets  |  |
|  |  Retransmit reliable      |    |  Order reliable packets    |  |
|  |  packets on timeout       |    |  Produce ConnectionEvents  |  |
|  +---------------------------+    +----------------------------+  |
+------------------------------------------------------------------+

Thread count: 7-13 threads typical
  Main(1) + Server(1) + Emerge(1-4) + Mesh(1-3) + NetSend(1) + NetRecv(1)
```

### The envlock

The **environment lock** (`m_env_mutex`) is the critical synchronization point:

```
ServerThread holds envlock for most of AsyncRunStep()
    |
    |  EmergeThreads need envlock briefly for:
    |    - getBlockOrStartGen()  (check if block exists)
    |    - finishGen()           (commit generated block)
    |
    |  Contention relief:
    |    ServerThread::yieldToOtherThreads()
    |    If emerge queue >= 32 items, sleeps 1ms x10
    |    to let emerge threads acquire the lock
```

---

## 6. ServerEnvironment Step

`ServerEnvironment::step(dtime)` in `src/serverenvironment.cpp` -- the heart of the simulation.

Each subsystem accumulates dtime independently and fires at its own interval:

```
ServerEnvironment::step(dtime)
|
|-- Time of day update                          [every tick]
|-- Game time counter                           [every tick]
|
|-- Active block management                     [every 2.0s]
|   |
|   |-- Collect player positions
|   |-- Compute active block set (sphere around each player)
|   |-- For newly-active blocks:
|   |     getBlockOrEmerge()  -->  may trigger map generation
|   |     activateBlock()     -->  load static objects, run LBMs
|   |-- For newly-inactive blocks:
|   |     deactivate objects  -->  convert SAOs to static data
|   |
|
|-- Node timers                                 [every 0.2s]
|   |
|   |-- For each active block:
|   |     Run expired node timers
|   |     Call m_script->node_on_timer(pos, elapsed)
|   |
|
|-- ABMs (Active Block Modifiers)               [every 1.0s]
|   |
|   |-- Shuffle ABM list randomly
|   |-- For each active block (shuffled):
|   |     For each ABM:
|   |       Check if interval has elapsed
|   |       Scan block for matching nodenames/groups
|   |       Roll chance (1/trigger_chance per match)
|   |       Call ABM trigger callback (Lua function)
|   |     Abort if time budget exceeded (20% of interval)
|   |
|
|-- Lua globalstep                              [every tick]
|   |
|   |-- Call all core.registered_globalsteps(dtime)
|   |
|
|-- Active object stepping                      [every tick]
|   |
|   |-- For each ServerActiveObject:
|   |     SAO::step(dtime, send_recommended)
|   |     send_recommended = true every ~0.09s
|   |     Collect outgoing messages
|   |
|
|-- Object cleanup                              [every 0.5s]
|   |-- Remove dead/expired objects
|
|-- Particle spawner expiry                     [every 1.0s]
```

### How ABMs Work in Detail

ABMs are the mechanism behind grass spreading, lava cooling, fire, plant growth, etc.

```
Example: Grass spread ABM
  nodenames = {"group:spreading_dirt_type"}
  interval = 6          -- check every 6 seconds
  chance = 50           -- 1/50 chance per matching node
  action = function(pos, node)
      -- find nearby air, check light, convert dirt to grass
  end

Engine processing (every abm_interval = 1.0s):
  1. Has 6.0s elapsed since last run of this ABM? No -> skip
  2. For each active block:
     3. Scan all 4096 nodes (16^3) for group:spreading_dirt_type
     4. For each match: roll random(1, 50)
     5. If roll == 1: call action(pos, node)
  6. If total time > abm_time_budget (200ms): abort early
```

### How Node Timers Work

Node timers are per-node countdowns (used by furnaces, saplings, crops, etc.):

```
1. Mod calls minetest.get_node_timer(pos):set(timeout, elapsed)
2. Timer stored in MapBlock's NodeTimerList
3. Every 0.2s, ServerEnvironment scans active blocks:
   - For each expired timer: call on_timer(pos, elapsed)
   - Callback returns true -> restart timer
   - Callback returns false -> timer removed
```

---

## 7. Map System & Block Lifecycle

### MapBlock Structure

```
MapBlock = 16 x 16 x 16 nodes = 4096 nodes

Each MapNode = 4 bytes:
  +--------+--------+--------+--------+
  |  param0 (u16)   | param1 | param2 |
  |  content type ID | light  | facedir|
  +--------+--------+--------+--------+

Total block data: 4096 * 4 = 16,384 bytes (uncompressed)

World coordinate -> block coordinate:
  block_pos = floor(node_pos / 16)

  e.g. node (35, -12, 100) is in block (2, -1, 6)
       node (0, 0, 0) is in block (0, 0, 0)
       node (-1, 0, 0) is in block (-1, 0, 0)
```

### Block Lifecycle

```
                    +-------------+
                    |  Not loaded |
                    | (on disk or |
                    |  not exist) |
                    +------+------+
                           |
              getBlockOrEmerge() called
              (player enters active range)
                           |
                    +------v------+
                    |   Loading   |
                    | EmergeThread|
                    | reads DB or |
                    | runs mapgen |
                    +------+------+
                           |
                    +------v------+
                    |   Active    |<----- refGrab() by mesh workers,
                    | In memory,  |       etc. prevents unload
                    | ABMs run,   |
                    | timers tick |
                    +------+------+
                           |
              Player moves away,
              block leaves active set
                           |
                    +------v------+
                    |   Idle      |
                    | usageTimer  |
                    | counting up |
                    +------+------+
                           |
              usageTimer > 29s AND refcount == 0
              (checked every 2.92s)
                           |
                    +------v------+
                    |  Unloading  |
                    | Save if     |
                    | modified,   |
                    | free memory |
                    +------+------+
                           |
                    +------v------+
                    |  Not loaded |
                    +-------------+
```

### Active Block Range

```
        Player at block (5, 0, 3)
        active_block_range = 4 (default)

        Active blocks form a cube around the player:

        X: 1 to 9  (5 +/- 4)
        Z: -1 to 7 (3 +/- 4)
        Y: -4 to 4 (0 +/- 4)

        = 9 * 9 * 9 = 729 active blocks per player
        = 729 * 4096 = ~3 million active nodes per player

        Top-down view (Y=0 slice):

        Z=7  . . . . . . . . .
        Z=6  . . . . . . . . .
        Z=5  . . . . . . . . .
        Z=4  . . . . . . . . .
        Z=3  . . . . P . . . .    P = player
        Z=2  . . . . . . . . .
        Z=1  . . . . . . . . .
        Z=0  . . . . . . . . .
        Z=-1 . . . . . . . . .
             X=1 2 3 4 5 6 7 8 9
```

---

## 8. Map Generation (Emerge)

```
+------------------------------------------------------------------+
|                     Map Generation Pipeline                       |
+------------------------------------------------------------------+

  ServerEnvironment                    EmergeThread (separate thread)
  active block mgmt
       |
  getBlockOrEmerge(pos)
       |
       v
  EmergeManager::
  enqueueBlockEmerge(pos)
       |
       v
  +----------------+
  | Emerge Queue   |  signal()  +----> EmergeThread::run()
  | (max 1024)     | --------->        |
  +----------------+                   |
                                       v
                               getBlockOrStartGen()
                               [holds envlock briefly]
                                       |
                          +------------+------------+
                          |                         |
                    Block in DB?              Not in DB?
                          |                         |
                          v                         v
                    Load from DB            Mapgen::makeChunk()
                    [no envlock]            [no envlock]
                          |                         |
                          |                    Generates terrain:
                          |                    - Base terrain (noise)
                          |                    - Caves, dungeons
                          |                    - Biomes
                          |                    - Ores
                          |                    - Decorations (trees)
                          |                         |
                          |                    on_generated() Lua
                          |                    [emerge Lua state]
                          |                         |
                          +------------+------------+
                                       |
                                       v
                               finishGen()
                               [holds envlock briefly]
                               Commits block to map
                                       |
                                       v
                               Fire completion callbacks
                               Client gets block data
```

### Emerge Thread Counts

```
Auto-detection: max(1, min(4, num_CPUs - 2))
Also capped by RAM: 1 thread per GB available

Examples:
  4-core CPU, 8GB RAM  -> 2 emerge threads
  8-core CPU, 16GB RAM -> 4 emerge threads
  2-core CPU, 4GB RAM  -> 1 emerge thread

NOTE: Most mapgens are currently limited to 1 thread
      due to an engine bug (#9357). Only MAPGEN_SINGLENODE
      allows true parallel emerge.
```

---

## 9. Mesh Generation Pipeline

Converts voxel data (MapBlocks) to renderable triangle meshes, asynchronously.

```
  Map change detected              MeshUpdateWorkerThread(s)
  (block received/modified)        (1-3 threads)
       |                                  |
       v                                  |
  MeshUpdateQueue::addBlock()             |
       |                                  |
       | refGrab() block + 26 neighbors   |
       | (prevents unload during meshing) |
       |                                  |
       v                                  |
  +------------------+                    |
  | MeshUpdateQueue  |     pop()          |
  |                  | -----------------> |
  | - normal items   |                    v
  | - urgent items   |            fillDataFromMapBlocks()
  |   (prioritized)  |            Copy 18x18x18 voxel data
  +------------------+                    |
                                          v
                                   MapBlockMesh()
                                   Voxel-to-triangle conversion
                                   - Determine visible faces
                                   - Generate vertices/UVs
                                   - Apply lighting
                                   - Handle special drawtype
                                     (plantlike, liquid, mesh, etc.)
                                          |
                                          v
                               +---------------------+
                               | MeshUpdateResult     |
                               | (completed mesh)     |
                               +----------+----------+
                                          |
                        Main Thread       |
                        getNextResult() <-+
                               |
                               v
                        Apply mesh to scene graph
                        refDrop() blocks
```

### Why 26+1 Neighbors?

To generate a mesh for one block, the engine needs to check adjacent blocks for:
- Face culling (is there a solid block next door? don't draw that face)
- Lighting (light values from neighboring blocks)
- Connected nodeboxes (fences, glass panes connecting across block boundaries)

```
  Block being meshed: center
  Neighbors needed: 6 faces + 12 edges + 8 corners = 26

       +---+---+---+
      /   /   /   /|
     +---+---+---+ |
    /   / C /   /| +    C = center block
   +---+---+---+ |/|    All 26 surrounding blocks
   |   | C |   | + |    are also ref-grabbed
   +---+---+---+|/ +
   |   |   |   |+ /
   +---+---+---+/
```

---

## 10. Networking (MTP Protocol)

**MTP = Minetest Transport Protocol -- custom reliable UDP.**

```
UDP Packet Layout:
+------+------+------+------+------+------+------+------+---
| Protocol ID (4 bytes)     | Sender      | Ch   | Type-specific...
| 0x4f457403                | Peer ID (2) | (1)  |
+------+------+------+------+------+------+------+------+---
         Base header: 7 bytes

Packet Types:
+------+------------------+--------------------------------+
| Type | Name             | Usage                          |
+------+------------------+--------------------------------+
|  0   | CONTROL          | ACK, SET_PEER_ID, PING, DISCO  |
|  1   | ORIGINAL         | Plain unsequenced data         |
|  2   | SPLIT            | Large data in chunks           |
|  3   | RELIABLE         | Sequenced, ACK'd, in-order     |
+------+------------------+--------------------------------+

Max packet size: 512 bytes (larger data is split)
3 channels (no intrinsic meaning, used for priority)
Connection timeout: 30 seconds
```

### Reliable Packet Flow

```
Client                                          Server
  |                                               |
  |  RELIABLE(seqnum=1, TOSERVER_INIT)            |
  |---------------------------------------------->|
  |                                               |
  |              CONTROL(ACK, seqnum=1)           |
  |<----------------------------------------------|
  |                                               |
  |  RELIABLE(seqnum=2, TOSERVER_INIT2)           |
  |---------------------------------------------->|
  |                                               |
  |              CONTROL(ACK, seqnum=2)           |
  |<----------------------------------------------|
  |                                               |
  |         RELIABLE(seqnum=1, TOCLIENT_BLOCKDATA)|
  |<----------------------------------------------| (may be SPLIT
  |                                               |  if > 512 bytes)
  |  CONTROL(ACK, seqnum=1)                       |
  |---------------------------------------------->|

  If no ACK received: retransmit after timeout
  Reliable packets are delivered in-order per channel
```

### Split Packet Reassembly

```
Large block data (e.g. 20KB):

  Chunk 0: [seqnum|chunk_count=40|chunk_num=0 |data_0..511]
  Chunk 1: [seqnum|chunk_count=40|chunk_num=1 |data_0..511]
  ...
  Chunk 39:[seqnum|chunk_count=40|chunk_num=39|data_0..xxx]

  Receiver buffers chunks, assembles when all received
```

### Key Network Commands

```
TOSERVER_* (client -> server):
  INIT, INIT2               -- connection handshake
  PLAYERPOS                  -- position updates (~10/s)
  INTERACT                   -- dig, place, punch
  CHAT_MESSAGE               -- chat
  INVENTORY_ACTION           -- move/drop items
  RESPAWN, DAMAGE            -- death/respawn

TOCLIENT_* (server -> client):
  BLOCKDATA                  -- map block contents
  ADDNODE / REMOVENODE       -- single node changes
  INVENTORY                  -- full inventory sync
  ACTIVE_OBJECT_REMOVE_ADD   -- entity spawn/despawn
  ACTIVE_OBJECT_MESSAGES     -- entity updates
  CHAT_MESSAGE               -- chat
  MOVE_PLAYER                -- teleport/correction
  HP, BREATH                 -- vitals
  NODEDEF, ITEMDEF           -- content definitions (on join)
```

---

## 11. Object System (SAO/CAO)

Server Active Objects (SAOs) and Client Active Objects (CAOs) are the entity system.

```
+---------------------------------------------------------------+
|                    Object Lifecycle                             |
+---------------------------------------------------------------+

  Lua: minetest.add_entity(pos, name)
       |
       v
  Server creates LuaEntitySAO
  Assigns u16 object ID (1-65535)
  Adds to ActiveObjectMgr + spatial index
       |
       v
  Server sends TOCLIENT_ACTIVE_OBJECT_REMOVE_ADD
  to all clients in range
       |
       v
  Client creates GenericCAO
  (universal client-side representation)
  Renders mesh, applies textures
       |
       |  Every server tick:
       |    SAO::step(dtime, send_recommended)
       |    - Update physics, AI, etc.
       |    - If send_recommended (~11 Hz):
       |        Queue position/animation messages
       |
       |  Server batches messages:
       |    TOCLIENT_ACTIVE_OBJECT_MESSAGES
       |
       v
  Client receives updates
  GenericCAO interpolates position
  Updates animation, attachment, etc.

  When block leaves active range:
    SAO -> static data -> stored in MapBlock
    Client removes GenericCAO

  When block re-enters active range:
    Static data -> new SAO
    Client gets new GenericCAO
```

### Spatial Index

```
ActiveObjectMgr uses a spatial index for fast queries:

  getObjectsInsideRadius(pos, radius)
  getObjectsInArea(minpos, maxpos)

Used by:
  - Determining which objects each player can see
  - Lua: minetest.get_objects_inside_radius()
  - Collision detection
  - ABM-like entity processing
```

---

## 12. Lua Scripting Integration

### Hook Points in the Engine Loop

```
ServerEnvironment::step(dtime)
  |
  |-- [ABM processing]
  |     For each matching node:
  |       m_script->triggerABM(...)        -- calls ABM action(pos,node)
  |
  |-- [Node timers]
  |     For each expired timer:
  |       m_script->node_on_timer(pos,elapsed)  -- calls on_timer
  |
  |-- [Globalstep]
  |     m_script->environment_Step(dtime)  -- calls all registered_globalsteps
  |
  |-- [Object stepping]
  |     Each LuaEntitySAO::step()          -- calls on_step(dtime)
  |

EmergeThread (map generation):
  |-- m_script->on_generated(minp,maxp)    -- in emerge Lua state
  |-- then in server Lua state:
  |     environment_OnGenerated(minp,maxp) -- calls registered_on_generateds

Network packet handlers:
  |-- Player joins:    m_script->on_joinplayer(player)
  |-- Player leaves:   m_script->on_leaveplayer(player)
  |-- Chat:            m_script->on_chat_message(name, msg)
  |-- Place node:      m_script->node_on_place(...)
  |-- Dig node:        m_script->node_on_dig(...)
  |-- Punch entity:    m_script->luaentity_Punch(...)
  |-- etc.
```

### Lua Environment Isolation

```
+-------------------+     +-------------------+     +-------------------+
| ServerScripting   |     | EmergeScripting    |     | ClientScripting   |
| (1 instance)      |     | (1 per emerge      |     | (1 instance)      |
|                   |     |  thread)           |     |                   |
| All server-side   |     | Only:              |     | Client-side mods  |
| mod API:          |     |  - core.register_  |     | (SSCSM):          |
| - register_node   |     |    on_generated    |     | - Limited API     |
| - register_entity |     |  - VoxelManip      |     | - Sandboxed       |
| - register_abm    |     |  - PerlinNoise     |     |                   |
| - chat_send_all   |     |  - minetest.log    |     |                   |
| - etc.            |     |                   |     |                   |
+-------------------+     +-------------------+     +-------------------+
```

### How Mods Interact with the Engine

```
Mod registers a node:                    Engine processes it:

minetest.register_node("mymod:brick", {
    tiles = {"brick.png"},          -->  Stored in NodeDefManager
    groups = {cracky=3},            -->  Used by tool dig time calc
    on_dig = function(...)          -->  Called by engine on dig
    on_timer = function(pos, el)    -->  Called by node timer system
    sounds = default.node_sound_    -->  Played by engine on events
        stone_defaults(),
})

Mod registers an ABM:                    Engine processes it:

minetest.register_abm({
    nodenames = {"default:cobble"},
    neighbors = {"default:water_source"},  --> Engine scans active blocks
    interval = 16,                          --> Every 16 game-seconds
    chance = 200,                           --> 1/200 per matching node
    action = function(pos, node)            --> Engine calls this
        minetest.set_node(pos, {name="default:mossycobble"})
    end,
})

Mod registers a globalstep:             Engine calls it:

minetest.register_globalstep(function(dtime)
    -- Called every server tick (~11Hz dedicated, ~60Hz singleplayer)
    -- dtime = seconds since last call
    for _, player in ipairs(minetest.get_connected_players()) do
        -- update HUD, check conditions, etc.
    end
end)
```

---

## 13. Timing Reference

### All Intervals at a Glance

```
Frequency Scale (logarithmic):

60 Hz   |====| Client render (fps_max)
        |====| Singleplayer server step (1/fps_max)
        |
11 Hz   |==| Dedicated server tick (0.09s)
        |==| SAO send_recommended interval
        |
 5 Hz   |=| Node timer processing (0.2s)
        |=| Client active object lighting (0.21s)
        |
 2 Hz   |=| Object cleanup (0.5s)
        |
 1 Hz   |=| ABM processing (1.0s)
        |=| Liquid transform (1.0s)
        |=| Particle spawner cleanup (1.0s)
        |
0.5 Hz  |=| Active block management (2.0s)
        |
0.3 Hz  |=| Map block unload check - server (2.92s)
        |
0.2 Hz  |=| Time-of-day sync to clients (5.0s)
        |=| Map block unload check - client (5.25s)
        |=| Map/mod data save (5.3s)
        |
        |
0.03 Hz |=| Block idle unload timeout (29s)
        |
        |
0.003Hz |=| Masterserver announce (300s)
```

### Key Constants

| Constant | Value | File | Purpose |
|----------|-------|------|---------|
| `MAP_BLOCKSIZE` | 16 | `src/constants.h` | Nodes per block edge |
| `DTIME_LIMIT` | 2.5s | `src/constants.h` | Max allowed dtime |
| `CONNECTION_TIMEOUT` | 30s | `src/constants.h` | UDP peer timeout |
| `MAX_PACKET_SIZE` | 512 | `src/network/` | UDP max payload |
| `PROTOCOL_ID` | 0x4f457403 | `src/network/` | MTP magic number |
| `CHANNEL_COUNT` | 3 | `src/network/` | UDP channel count |

### Settings That Affect Timing

| Setting | Default | Effect |
|---------|---------|--------|
| `fps_max` | 60 | Client render cap; singleplayer server rate |
| `fps_max_unfocused` | 10 | Client render cap when unfocused |
| `dedicated_server_step` | 0.09 | Server tick interval (seconds) |
| `active_block_range` | 4 | Blocks from player to activate |
| `active_block_mgmt_interval` | 2.0 | How often active set is recalculated |
| `abm_interval` | 1.0 | How often ABMs are processed |
| `abm_time_budget` | 0.2 | Fraction of interval allowed for ABMs |
| `nodetimer_interval` | 0.2 | How often node timers are checked |
| `liquid_update` | 1.0 | Liquid simulation interval |
| `server_unload_unused_data_timeout` | 29 | Seconds before idle block unload |
| `server_map_save_interval` | 5.3 | Map/mod storage save interval |
| `num_emerge_threads` | 0 (auto) | Map generation threads |
| `mesh_generation_threads` | 0 (auto) | Mesh worker threads |
