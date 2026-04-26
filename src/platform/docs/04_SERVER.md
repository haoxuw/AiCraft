# Solarium - C++ Server

The server is the single source of truth. It owns the World, runs the tick loop, embeds Python for hot-loading Objects and Actions, and synchronizes state to all clients.

---

## 1. Architecture Overview

```
+====================================================================+
|                        Solarium C++ Server                           |
+====================================================================+
|                                                                      |
|  +--------------------+   +--------------------+   +--------------+ |
|  |   Main Thread      |   |   Network Thread   |   | DB Thread    | |
|  |                    |   |                    |   |              | |
|  |  ServerLoop::run() |   |  UDPSocket recv/  |   | SQLite/      | |
|  |  - tick world      |   |  send              |   | LevelDB      | |
|  |  - process actions |   |  - packet parse    |   | - chunk I/O  | |
|  |  - sync clients    |   |  - packet queue    |   | - entity I/O | |
|  +--------------------+   +--------------------+   +--------------+ |
|           |                        |                      |         |
|           |     +------------------+----------------------+         |
|           |     |                                                   |
|  +--------v-----v-----+   +--------------------+                    |
|  |  World State        |   |  Python Runtime    |                   |
|  |                     |   |  (embedded CPython) |                   |
|  |  ChunkManager       |   |                    |                   |
|  |  EntityManager      |   |  ObjectRegistry    |                   |
|  |  PlayerManager      |   |  ActionRegistry    |                   |
|  |  ActionQueue        |   |  Sandbox           |                   |
|  |  WorldTime          |   |  Hot-reloader      |                   |
|  +---------------------+   +--------------------+                   |
|                                                                      |
|  +--------------------+   +--------------------+                    |
|  |  Emerge Threads     |   |  Worker Pool       |                   |
|  |  (1-4 threads)      |   |  (CPU-bound tasks)  |                   |
|  |                     |   |                    |                   |
|  |  Terrain generation |   |  Physics batches   |                   |
|  |  Chunk loading      |   |  Pathfinding       |                   |
|  +--------------------+   |  Light propagation  |                   |
|                           +--------------------+                    |
+====================================================================+
```

---

## 2. Server Tick Loop

The server runs at a configurable tick rate (default: 20 Hz = 50ms per tick).

```cpp
// Pseudocode: src/server/server_loop.cpp

void ServerLoop::run() {
    const double tick_interval = 1.0 / config.tick_rate;  // 0.05s at 20Hz

    while (!m_shutdown_requested) {
        auto tick_start = steady_clock::now();

        // 1. Receive all pending network packets
        m_network->receive_all(m_packet_queue);

        // 2. Process client packets -> queue Actions
        process_packets(m_packet_queue);

        // 3. Compute dtime
        double dtime = compute_dtime(tick_start);
        dtime = std::min(dtime, DTIME_LIMIT);  // cap at 2.5s

        // 4. World step (the big one)
        m_world->step(dtime);

        // 5. Collect state deltas
        auto deltas = m_world->collect_deltas();

        // 6. Broadcast deltas to clients
        m_network->broadcast_deltas(deltas);

        // 7. Periodic maintenance
        if (m_maintenance_timer.tick(dtime, 5.0)) {
            m_world->save_modified_chunks();
            m_python->collect_garbage();
        }

        // 8. Sleep for remainder of tick
        auto elapsed = steady_clock::now() - tick_start;
        auto remaining = tick_interval - elapsed;
        if (remaining > 0)
            sleep(remaining);
    }
}
```

### Tick Timeline

```
|<-------------- 50ms (at 20 Hz) ---------------->|
|                                                   |
|  Recv   |  Process  |  World::step  | Broadcast | Sleep
|  pkts   |  packets  |  (the sim)    | deltas    |
|  ~2ms   |  ~3ms     |  ~15-25ms     | ~5ms      | ~15ms
|                                                   |
|  Network    Input       Simulation     Output     Idle
```

---

## 3. World Step (C++ side)

The C++ server orchestrates the step, but delegates object/action logic to Python:

```cpp
// Pseudocode: src/server/world.cpp

void World::step(double dtime) {
    // 1. Advance time
    m_time.advance(dtime);

    // 2. Chunk management (load/unload based on player positions)
    m_chunk_manager.update_active_chunks(m_players.get_positions());

    // 3. Process action queue
    //    Actions are Python objects -- we call into Python here
    m_python->acquire_gil();
    {
        auto& queue = m_action_queue;
        queue.sort_by_priority();

        int processed = 0;
        while (!queue.empty() && processed < MAX_ACTIONS_PER_TICK) {
            auto action = queue.pop();

            // Create sandboxed WorldView scoped to action's region
            WorldView view(this, action.origin(), ACTION_VIEW_RADIUS);

            // Call Python: action.validate(view)
            if (!m_python->call_validate(action, view))
                continue;

            // Call Python: action.execute(view)
            m_python->call_execute(action, view);

            // Collect mutations from the WorldView
            apply_mutations(view.get_mutations());

            // Collect chained actions
            for (auto& chained : view.get_emitted_actions())
                queue.push(chained, PRIORITY_CHAIN);

            processed++;
        }
    }

    // 4. Step all active objects (Python)
    for (auto& [id, obj] : m_entities.active_in_range()) {
        WorldView view(this, obj.pos(), ENTITY_VIEW_RADIUS);
        m_python->call_step(obj, dtime, view);
        apply_mutations(view.get_mutations());
    }
    m_python->release_gil();

    // 5. C++ native systems (no Python needed)
    m_physics.step(dtime);        // collision detection, gravity
    m_light.propagate();          // light level updates
    m_liquid.flow(dtime);         // basic liquid spreading (optimized in C++)

    // 6. Track deltas for network sync
    m_delta_tracker.finalize_tick();
}
```

---

## 4. Python Embedding

### 4.1 Why Embedded CPython?

```
Options considered:
  +-----------------+-----------+------------+----------+
  | Approach        | Speed     | Ecosystem  | Safety   |
  +-----------------+-----------+------------+----------+
  | Lua (like MT)   | Fast      | Small      | Good     |
  | CPython embed   | Moderate  | Huge       | Moderate |
  | PyPy sandbox    | Fast      | Huge       | Good     |
  | WASM            | Fast      | Growing    | Excellent|
  +-----------------+-----------+------------+----------+

  Choice: Embedded CPython via pybind11
  - Players know Python (most popular language)
  - Pydantic is native Python
  - pybind11 makes C++ <-> Python calls zero-copy for buffers
  - Sandbox security handled by AST restriction + process isolation
```

### 4.2 C++ <-> Python Interface

```cpp
// src/server/python_runtime.h

class PythonRuntime {
public:
    // Lifecycle
    void init(const std::string& artifacts_path);
    void shutdown();

    // Object Registry
    bool load_object_type(const std::string& source_code, std::string& error);
    bool unload_object_type(const std::string& object_id);
    py::object create_instance(const std::string& object_id, py::dict attrs);

    // Action Registry
    bool load_action_type(const std::string& source_code, std::string& error);
    bool call_validate(py::object action, WorldView& view);
    void call_execute(py::object action, WorldView& view);

    // Object stepping
    void call_step(py::object obj, double dtime, WorldView& view);

    // Hot-reload
    bool hot_reload(const std::string& type, const std::string& new_source);

    // GIL management
    void acquire_gil();
    void release_gil();

    // Sandbox
    bool validate_source(const std::string& source, std::string& error);

private:
    py::scoped_interpreter m_interpreter;
    py::module_ m_solarium_module;
    std::unordered_map<std::string, py::object> m_object_types;
    std::unordered_map<std::string, py::object> m_action_types;
    SandboxValidator m_sandbox;
};
```

### 4.3 WorldView: C++ Object Exposed to Python

```cpp
// src/server/world_view.h
// Bound to Python via pybind11

class WorldView {
public:
    WorldView(World* world, Vec3 center, float radius);

    // Block operations (bounds-checked to radius)
    py::object get_block(BlockPos pos);
    void set_block(BlockPos pos, py::object block);
    py::list get_blocks_in_radius(BlockPos center, int radius);

    // Entity operations
    py::object get_entity(EntityId id);
    py::list get_entities_in_radius(Vec3 center, float radius);
    EntityId spawn_entity(Vec3 pos, const std::string& type, py::kwargs attrs);
    void remove_entity(EntityId id);

    // Effects (queued, applied after action completes)
    void play_sound(const std::string& sound, Vec3 pos, float gain);
    void emit_particles(py::object spec);
    void emit_action(const std::string& action_type, py::kwargs inputs);

    // Queries
    py::object get_time();
    py::object get_weather();
    py::object find_nearest(Vec3 pos, const std::string& type, float radius);
    py::object raycast(Vec3 origin, Vec3 direction, float max_dist);
    int get_light_level(BlockPos pos);

    // Mutation tracking (used by C++ after Python returns)
    const std::vector<Mutation>& get_mutations() const;
    const std::vector<QueuedAction>& get_emitted_actions() const;

private:
    World* m_world;
    Vec3 m_center;
    float m_radius;
    std::vector<Mutation> m_mutations;
    std::vector<QueuedAction> m_emitted_actions;

    bool in_bounds(Vec3 pos) const;
    void check_bounds(Vec3 pos) const;  // throws if out of bounds
};
```

---

## 5. Hot-Loading Pipeline

```
+------------------------------------------------------------------+
|                     Hot-Load Flow                                  |
+------------------------------------------------------------------+

  Player writes code in-game editor
       |
       v
  Client sends TOSERVER_UPLOAD_ARTIFACT packet
       |  Contains: source_code (str), type ("object" | "action"),
       |            assets (textures, models, sounds as binary)
       v
  Server receives packet
       |
       v
  +---------------------------+
  | 1. Sandbox Validation     |
  |    (AST analysis)         |
  |                           |
  |  ALLOW:                   |
  |    - solarium.api imports  |
  |    - math, random         |
  |    - typing, dataclasses  |
  |    - Pydantic             |
  |                           |
  |  DENY:                    |
  |    - os, sys, subprocess  |
  |    - socket, http         |
  |    - __import__, eval,    |
  |      exec, compile        |
  |    - open(), file I/O     |
  |    - ctypes, cffi         |
  |    - globals(), locals()  |
  +---------------------------+
       |
       v (pass)
  +---------------------------+
  | 2. Compile & Load         |
  |                           |
  |  compile(source, ...)     |
  |  exec in sandboxed ns     |
  |  Find class extending     |
  |    Object or Action       |
  |  Validate ObjectMeta /    |
  |    ActionMeta             |
  |  Check id follows format: |
  |    "playername:name"      |
  +---------------------------+
       |
       v (pass)
  +---------------------------+
  | 3. Test Instantiation     |
  |                           |
  |  Create test instance     |
  |  Call step() with mock    |
  |    WorldView (timeout 1s) |
  |  Verify no crash/hang     |
  +---------------------------+
       |
       v (pass)
  +---------------------------+
  | 4. Register               |
  |                           |
  |  Add to ObjectRegistry    |
  |    or ActionRegistry      |
  |  Save .py to artifacts/   |
  |  Save assets to assets/   |
  +---------------------------+
       |
       v
  +---------------------------+
  | 5. Broadcast              |
  |                           |
  |  Send TOCLIENT_NEW_CONTENT|
  |  to all connected clients |
  |  Clients download assets  |
  +---------------------------+
       |
       v
  Object/Action is now live in the world!
```

### Hot-Reload vs. Hot-Load

```
Hot-LOAD:  First time registering a new type.
           Server adds to registry, broadcasts to clients.

Hot-RELOAD: Updating an existing type.
            Server replaces class in registry.
            Existing instances get their class pointer updated.
            No server restart needed.

Example:
  Player uploads "wizardMike:magic_ore" (v1).
  50 instances exist in the world.
  Player uploads updated "wizardMike:magic_ore" (v2).
  All 50 instances now use v2's step() and attributes.
  New attributes get default values.
  Removed attributes are dropped.
```

---

## 6. Sandbox Security

### 6.1 Layers of Defense

```
Layer 1: AST Restriction
  - Parse source code into AST
  - Walk tree, reject forbidden nodes:
    Import (only whitelisted modules)
    Call to forbidden builtins
    Attribute access to __dunder__ methods
    While loops without break (potential infinite loops)

Layer 2: Resource Limits
  - step() has a 10ms CPU timeout (per call)
  - Action.execute() has a 50ms CPU timeout
  - Memory limit: 50MB per player's objects
  - Max objects per player: 100 types
  - Max actions per player: 50 types

Layer 3: WorldView Scoping
  - Objects can only see/modify within their radius
  - No direct World access, only through WorldView API
  - WorldView enforces rate limits on mutations

Layer 4: Process Isolation (future)
  - Each player's code runs in a subprocess
  - communicate via shared memory / IPC
  - Process crash doesn't take down server
```

### 6.2 Sandbox Validator

```python
# src/server/sandbox/validator.py (runs inside embedded Python)

ALLOWED_IMPORTS = {
    "solarium.api",          # the game API
    "math",
    "random",
    "typing",
    "dataclasses",
    "enum",
    "collections",
    "functools",
    "itertools",
    "pydantic",
}

FORBIDDEN_BUILTINS = {
    "eval", "exec", "compile", "__import__",
    "open", "input", "breakpoint",
    "globals", "locals", "vars", "dir",
    "getattr", "setattr", "delattr",     # only on own objects
    "type", "super",                      # restricted usage
}

FORBIDDEN_ATTRIBUTES = {
    "__class__", "__bases__", "__subclasses__",
    "__code__", "__globals__", "__builtins__",
    "__import__", "__loader__", "__spec__",
}
```

---

## 7. C++ Core Systems (Not in Python)

These systems run in optimized C++ for performance:

```
+------------------------------------------------------------------+
|                    C++ Native Systems                              |
+------------------------------------------------------------------+
|                                                                    |
|  Physics Engine                                                   |
|  - AABB collision detection between entities and blocks           |
|  - Gravity (configurable per entity via gravity_scale)            |
|  - Velocity integration (semi-implicit Euler)                     |
|  - Spatial hash for broad-phase collision                         |
|  - No Python involvement -- pure C++                              |
|                                                                    |
|  Light Propagation                                                |
|  - BFS flood-fill from light sources                              |
|  - Sunlight column propagation                                    |
|  - Incremental updates (only recalc changed regions)              |
|  - Per-block light level stored in block_params                   |
|                                                                    |
|  Liquid Flow (optimized path)                                     |
|  - BFS from source blocks                                         |
|  - Level-based flow (8 levels, source = 8)                        |
|  - Python Water.step() is the authoritative logic,                |
|    but C++ can run a fast approximation for bulk flow              |
|                                                                    |
|  Terrain Generation                                               |
|  - Noise functions (Perlin, Simplex) in C++                       |
|  - Biome selection based on heat/humidity                         |
|  - Ore distribution                                               |
|  - Cave carving                                                    |
|  - Python hooks for custom decoration/structure placement         |
|                                                                    |
|  Chunk I/O                                                        |
|  - Compression (zstd) for chunk data                              |
|  - Async DB reads/writes on separate thread                       |
|  - Write coalescing (batch nearby chunk saves)                    |
|                                                                    |
|  Network Protocol                                                 |
|  - Packet serialization (protobuf or custom binary)               |
|  - Delta compression (only send changed fields)                   |
|  - Reliable/unreliable channels                                   |
|  - Connection management, timeout, keepalive                      |
|                                                                    |
+------------------------------------------------------------------+
```

---

## 8. Server Configuration

```toml
# server.toml

[server]
name = "My Solarium World"
port = 30000
max_players = 32
tick_rate = 20                    # Hz (ticks per second)
dtime_limit = 2.5                # max dtime cap (seconds)

[world]
seed = 12345
day_length = 1200                # real seconds per game day
active_block_range = 4           # chunks from player
load_range = 8                   # chunks rendered (sent to client)
unload_timeout = 30              # seconds before idle chunk unloads

[python]
artifacts_path = "./artifacts"
sandbox_enabled = true
step_timeout_ms = 10             # max ms per object step()
action_timeout_ms = 50           # max ms per action execute()
max_memory_per_player_mb = 50
max_object_types_per_player = 100
max_action_types_per_player = 50

[emerge]
threads = 0                      # 0 = auto
queue_limit = 512

[network]
max_packet_size = 65536          # bytes (TCP, not limited like UDP)
protocol = "tcp"                 # tcp or udp+reliable
compression = "zstd"

[database]
backend = "sqlite"               # sqlite, postgres
path = "./world/world.db"

[performance]
action_budget_per_tick = 1000    # max actions processed per tick
entity_budget_per_tick = 5000    # max entity steps per tick
abm_interval = 1.0              # seconds
abm_time_budget = 0.2           # fraction of interval
```

---

## 9. Directory Layout

```
solarium-server/
  src/
    server/
      main.cpp                   # Entry point
      server_loop.cpp            # Main tick loop
      world.cpp                  # World state management
      world_view.cpp             # Sandboxed view for Python
      chunk_manager.cpp          # Chunk load/unload/generate
      entity_manager.cpp         # Active object tracking
      player_manager.cpp         # Player connections
      action_queue.cpp           # Action processing
      delta_tracker.cpp          # State change tracking for sync
      python_runtime.cpp         # CPython embedding via pybind11
      sandbox/
        validator.py             # AST-based source validation
        builtins.py              # Restricted builtins whitelist
    physics/
      physics.cpp                # AABB collision, gravity
      spatial_hash.cpp           # Broad-phase spatial index
    mapgen/
      mapgen.cpp                 # Terrain generation
      noise.cpp                  # Perlin/Simplex noise
      biome.cpp                  # Biome selection
    network/
      protocol.cpp               # Packet format, serialization
      connection.cpp             # Per-client connection state
      delta_compress.cpp         # Delta encoding for state sync
    database/
      database.cpp               # Abstract DB interface
      sqlite_backend.cpp         # SQLite implementation
    common/
      types.h                    # Vec3, BlockPos, EntityId, etc.
      constants.h                # MAP_BLOCKSIZE, DTIME_LIMIT, etc.
      chunk.h                    # Chunk data structure
  python/
    solarium/
      api/
        __init__.py              # Public API re-exports
        objects.py               # PassiveObject, ActiveObject, LivingObject, ...
        actions.py               # Action base class and decorator
        world_view.py            # WorldView type stubs (impl in C++)
        types.py                 # Vec3, BlockPos, EntityId, etc.
        attributes.py            # Attribute descriptor
        meta.py                  # ObjectMeta, ActionMeta
        inventory.py             # Inventory, ItemStack
        sounds.py                # SoundSet
        loot.py                  # LootTable, LootEntry
        particles.py             # ParticleSpec
  artifacts/
    objects/                     # Built-in object definitions
    actions/                     # Built-in action definitions
    assets/
      textures/
      models/
      sounds/
  CMakeLists.txt
  pyproject.toml                 # For the Python API package
```
