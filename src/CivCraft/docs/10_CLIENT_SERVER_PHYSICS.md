# Client–Server Physics Architecture

> **Mandatory.** All client and server code must conform to this design.
> This document supersedes any older patterns in the codebase that contradict it.

## Core Principles

```
┌─────────────────────────────────────────────────────────────┐
│                        CLIENT PROCESS                       │
│                                                             │
│   ┌───────────────┐    shared     ┌──────────────────────┐  │
│   │  LocalWorld   │◄─────────────►│  Unified Entity Tick │  │
│   │  (ChunkSource │    physics    │                      │  │
│   │   incomplete) │    code       │  ALL entities:       │  │
│   └───────────────┘               │   • player (WASD)    │  │
│         ▲                         │   • NPCs (agent AI)  │  │
│         │ S_BLOCK,                │                      │  │
│         │ S_CHUNK                 │  Same loop.          │  │
│         │                         │  Same variables.     │  │
│         │                         │  Same moveAndCollide. │  │
│         │                         └──────────┬───────────┘  │
│         │                                    │              │
│         │                           ActionProposals         │
│         │                                    │              │
├─────────┼────────────────────────────────────┼──────────────┤
│         │              TCP                   │              │
├─────────┼────────────────────────────────────┼──────────────┤
│         │                                    ▼              │
│   ┌─────┴─────────┐              ┌──────────────────────┐  │
│   │    World       │◄────────────►│  Server Tick         │  │
│   │   (complete,   │   same       │                      │  │
│   │ authoritative) │   physics    │  Validates proposals │  │
│   └───────────────┘   code       │  Runs moveAndCollide │  │
│                                   │  Broadcasts state    │  │
│                       SERVER PROCESS                        │
└─────────────────────────────────────────────────────────────┘
```

### 1. One Source of Truth Per Process

| Process | Source of Truth | Class | Complete? |
|---------|----------------|-------|-----------|
| **Server** | `World` | `GameServer` + `EntityManager` | Yes — all chunks, all entities |
| **Client** | `LocalWorld` | New class (replaces NetworkServer chunk storage) | No — only streamed chunks within view radius |

The client **never** reads entity positions from the server's entity map.
The server **never** reads from LocalWorld. Each process owns its truth.

### 2. One Tick, All Entities

The client runs **one unified tick loop** for every entity it controls:

```cpp
// Pseudocode — the real implementation lives in the unified tick module.
for (auto& [id, entity] : localEntities) {
    // 1. Determine desired velocity (input source varies, entity is the same)
    glm::vec3 desiredVel;
    if (id == localPlayerId)
        desiredVel = readKeyboardInput();      // WASD / click-to-move
    else
        desiredVel = agentDecide(id);          // Python behavior

    // 2. Run physics — SAME moveAndCollide, SAME LocalWorld
    auto result = moveAndCollide(localWorld.solidFn(),
                                 entity.position, desiredVel, dt, params,
                                 entity.onGround);
    entity.position = result.position;
    entity.velocity = result.velocity;
    entity.onGround = result.onGround;

    // 3. Submit intent to server
    ActionProposal a;
    a.type       = ActionProposal::Move;
    a.actorId    = id;
    a.desiredVel = desiredVel;
    a.clientPos  = entity.position;   // prediction
    server.sendAction(a);
}
```

**There is no `tickPlayer()` vs `tickNPC()`.** There is `tickEntities()`.
The player is just an entity whose input comes from the keyboard.

### 3. Shared Physics Code

`moveAndCollide()` in `logic/physics.h` is the **single implementation** of
collision detection and resolution. It is called by:

- The **client** tick loop (for all locally-controlled entities)
- The **server** tick loop (for validation and authoritative physics)

Both use the same `BlockSolidFn` interface. The only difference is what
backs it: `LocalWorld` (client, incomplete) vs `World` (server, complete).

### 4. LocalWorld

`LocalWorld` is a standalone class that owns the client's chunk storage.
It implements `ChunkSource` and is the single place chunks live on the client.

```cpp
class LocalWorld : public ChunkSource {
    // Chunk storage — populated by server S_CHUNK / S_BLOCK messages
    std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, ChunkPosHash> m_chunks;
    BlockRegistry m_blocks;

public:
    // ChunkSource interface
    Chunk* getChunk(ChunkPos pos) override;
    uint16_t getBlock(int x, int y, int z) override;
    const BlockRegistry& blockRegistry() const override;

    // Server message handlers
    void applyChunk(ChunkPos pos, std::unique_ptr<Chunk> chunk);
    void applyBlockChange(int x, int y, int z, uint16_t blockId);

    // Physics helper — returns the BlockSolidFn for moveAndCollide
    BlockSolidFn solidFn() const;
};
```

**Shared by:** the player tick, all agent ticks, the chunk mesher, raycasting.
Everyone reads from the same LocalWorld. Nobody else stores chunks.

### 5. Entity Reconciliation

The server broadcasts entity state at 20 Hz. The client uses this to
**correct** its local predictions:

- **Locally-controlled entities** (player + owned NPCs): client runs
  physics and predicts position. When the server broadcast arrives, the
  client compares prediction vs server truth. Small drifts are tolerated
  (anti-jitter deadband). Large drifts trigger a snap to server position.
  The reconciliation logic is **identical** for the player and for NPCs.

- **Foreign entities** (other players' NPCs, unowned entities): the client
  does NOT run physics. It mirrors the server's broadcast position and
  extrapolates by `velocity × age` to smooth 20 Hz stagger. These entities
  are read-only on this client.

The distinction is **locally-controlled vs foreign**, not **player vs NPC**.

### 6. What the Server Does

The server receives `ActionProposals` and validates them:

1. **Move**: Accept `clientPos` if within tolerance and not in a wall.
   Otherwise, run server-authoritative `moveAndCollide` with the same
   physics code. Broadcast the result.

2. **Convert / Relocate / Interact**: Validate and execute. Broadcast
   state changes (`S_BLOCK`, `S_INVENTORY`, etc.).

The server has **zero intelligence**. It validates physics and conserves
value. All decision-making (player input, NPC AI) happens on clients.

### 7. Rules

1. **No dual state.** Entity position lives in `entity.position`. There is
   no parallel `m_player.pos`. The camera reads `entity.position`.

2. **No special-casing the player.** The player is an entity with
   `inputSource == Keyboard`. NPCs are entities with `inputSource == Agent`.
   The tick loop doesn't `if (id == localPlayerId)`.

3. **No client-side entity physics for foreign entities.** Extrapolate
   only. Running moveAndCollide on entities you don't control will diverge
   from the server and create ghost-collision artifacts.

4. **LocalWorld is the only chunk store on the client.** Not NetworkServer,
   not Game, not a renderer cache. One place.

5. **Physics code is never duplicated.** `moveAndCollide()` in
   `logic/physics.h` is the one implementation. If you need different
   behavior, add a parameter to `MoveParams`, not a second function.

## File Layout

```
src/platform/
  logic/           Shared simulation types (entity, action, physics, inventory)
    physics.h         moveAndCollide — THE physics implementation
    entity_physics.h  stepEntityPhysics — thin wrapper
    entity.h          Entity struct — single state representation
    action.h          ActionProposal — the 4 types

  net/             Networking (protocol, sockets, server interface)
    server_interface.h   Abstract interface to a game server
    net_protocol.h       Wire format
    net_socket.h         TCP wrapper

  client/          Player client (rendering + input + local simulation)
    local_world.h     LocalWorld — client's chunk store (ChunkSource)
    entity_tick.h     Unified entity tick loop (player + NPCs)
    network_server.h  TCP connection to server (no chunk storage — uses LocalWorld)
    camera.h, game.h, ...

  server/          Authoritative server (validation + broadcast)
    server.h          GameServer — owns World, validates proposals
    entity_manager.h  Entity lifecycle + server-side physics

  agent/           Agent AI (Python behaviors → ActionProposals)
    agent_client.h    Runs Python decide() for owned NPCs
```
