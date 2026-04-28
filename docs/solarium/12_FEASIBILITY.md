# Solarium - Feasibility: C++ Server Embedding Hot-Reloadable Python

**Question**: Can a C++ server embed Python, run player-written Objects and Actions, and hot-reload Python code DURING gameplay without restarting?

**Answer**: Yes. This is a well-proven pattern. Here's how and why.

---

## 1. Proven Precedents

| Project | What It Does | How |
|---------|-------------|-----|
| **Blender** | C++ 3D engine, Python scripting, live reload | CPython embedded, `importlib.reload()` |
| **Unreal Engine** | C++ engine, Python editor scripting | CPython embedded via plugin |
| **Maya / Houdini** | C++ DCC tools, Python scripting | CPython embedded, hot-reload |
| **Civilization VI** | C++ engine, Python/Lua AI and game logic | CPython embedded |
| **OpenAI Gym** | C++ simulation, Python agent code | pybind11 bindings |
| **GIMP** | C image editor, Python-Fu scripting | CPython embedded |

This is not experimental. Major production software does exactly this.

---

## 2. Technical Stack

```
+-----------------------------------------------+
|  C++ Server Binary                             |
|                                                 |
|  Linked against: libpython3.12 (or 3.13+)     |
|  Binding layer:  pybind11                      |
|                                                 |
|  At startup:                                   |
|    Py_Initialize()                             |
|    Load solarium.api module                     |
|    Load built-in objects/actions from .py files|
|                                                 |
|  During gameplay:                              |
|    Receive new .py source from player          |
|    Validate (AST scan)                         |
|    exec() in sandboxed namespace               |
|    Register new class in registry              |
|    Existing instances pick up new class         |
|                                                 |
|  Each tick:                                    |
|    Acquire GIL                                 |
|    Call decide() on ActiveObjects              |
|    Call validate()/execute() on Actions        |
|    Release GIL                                 |
+-----------------------------------------------+
```

### pybind11

```cpp
// pybind11 makes C++ <-> Python seamless:

#include <pybind11/embed.h>
namespace py = pybind11;

// Expose C++ WorldView to Python
PYBIND11_EMBEDDED_MODULE(solarium_native, m) {
    py::class_<WorldView>(m, "WorldView")
        .def("get_block", &WorldView::get_block)
        .def("set_block", &WorldView::set_block)
        .def("get_entities_in_radius", &WorldView::get_entities_in_radius)
        .def("spawn_entity", &WorldView::spawn_entity)
        .def("emit_action", &WorldView::emit_action)
        .def("play_sound", &WorldView::play_sound);
}

// In the server:
py::scoped_interpreter guard{};  // start Python

// Load a player's object:
py::object scope = py::dict();
scope["__builtins__"] = restricted_builtins;  // sandbox
py::exec(player_source_code, scope);
py::object PlayerClass = scope["FlyingPig"];

// Create instance:
py::object pig = PlayerClass();

// Call decide():
py::list actions = pig.attr("decide")(world_view);
```

---

## 3. Hot-Reload: How It Works

### The Key Insight

Python classes are just objects. You can replace them at runtime.

```python
# Registry holds class references, not instances directly.
# When we hot-reload, we replace the class. Existing instances
# can be migrated to the new class.

# Step 1: Player uploads FlyingPig v1
registry["alice:flying_pig"] = FlyingPigV1  # class object
pig_instance = FlyingPigV1(hunger=0.8)      # instance

# Step 2: Player uploads FlyingPig v2 (added wing_speed attribute)
registry["alice:flying_pig"] = FlyingPigV2  # replace class

# Step 3: Migrate existing instance
old_data = pig_instance.__dict__            # {hunger: 0.8}
new_instance = FlyingPigV2(**old_data)      # new class, old data
# new attributes get defaults, removed attributes are dropped
```

### C++ Side

```cpp
bool PythonRuntime::hot_reload(const std::string& type,
                                const std::string& new_source) {
    // 1. Validate new source
    std::string error;
    if (!validate_source(new_source, error))
        return false;

    // 2. Compile and extract class
    py::object scope = create_sandbox_scope();
    py::exec(new_source, scope);
    py::object new_class = find_object_class(scope);

    // 3. Replace in registry
    py::object old_class = m_object_types[type];
    m_object_types[type] = new_class;

    // 4. Migrate existing instances
    for (auto& [id, obj] : m_world->entities()) {
        if (obj.attr("meta").attr("id").cast<std::string>() == type) {
            py::dict old_data = obj.attr("__dict__");
            py::object new_obj = new_class(**old_data);
            m_world->replace_entity(id, new_obj);
        }
    }

    return true;
}
```

### What Happens to Running Instances

```
Before hot-reload:
  pig_42: FlyingPig(v1) { hunger=0.8, age=120 }
  pig_43: FlyingPig(v1) { hunger=0.3, age=500 }

Player uploads FlyingPig v2 (adds wing_speed=5.0 attribute):

After hot-reload:
  pig_42: FlyingPig(v2) { hunger=0.8, age=120, wing_speed=5.0 }
  pig_43: FlyingPig(v2) { hunger=0.3, age=500, wing_speed=5.0 }
                                                ^^^^^^^^^^
                                        new attr gets default value

Player uploads FlyingPig v3 (removes age attribute, changes decide()):

After hot-reload:
  pig_42: FlyingPig(v3) { hunger=0.8, wing_speed=5.0 }
  pig_43: FlyingPig(v3) { hunger=0.3, wing_speed=5.0 }
                          age dropped, new decide() active immediately
```

---

## 4. Performance Considerations

### Will Python Be Too Slow?

**Short answer**: No, for this use case.

```
Benchmark: what does Python need to do per tick?

  At 20 Hz server tick rate, we have 50ms per tick.

  Per-tick Python work:
    - Call decide() on ~200 active entities:  200 * 0.05ms = 10ms
    - Call validate()+execute() on ~50 actions: 50 * 0.1ms = 5ms
    - Total Python time: ~15ms out of 50ms budget = 30%

  Remaining 35ms for C++ native work:
    - Physics, collision: ~5ms
    - Chunk management: ~3ms
    - Network I/O: ~5ms
    - Idle/sleep: ~22ms

  This is comfortable. Even 500 entities would fit in budget.
```

### What If Python IS Too Slow?

```
Escape hatches (in order of complexity):

  1. Budget enforcement:
     Each decide()/execute() has a timeout (10ms/50ms).
     If exceeded, skip and log warning.

  2. Tick spreading:
     Don't step ALL entities every tick.
     Step 1/4 of entities per tick (round-robin).
     Each entity steps at 5 Hz instead of 20 Hz.
     4x less Python work per tick.

  3. C++ fast-paths:
     Common built-in objects (dirt, stone, water) can have
     C++ implementations that bypass Python entirely.
     Python versions exist for modding reference,
     but the server uses the C++ version for base content.

  4. Subinterpreters (Python 3.12+):
     Run entity groups in separate subinterpreters
     on different threads. True parallelism without GIL.
     https://peps.python.org/pep-0554/

  5. Free-threaded Python (3.13+):
     No-GIL mode. Multiple threads call Python concurrently.
     https://peps.python.org/pep-0703/
```

### GIL Management

```
The GIL (Global Interpreter Lock) is Python's threading limitation.
Only one thread can execute Python at a time.

Our approach:
  - C++ server is multi-threaded (physics, network, emerge, mesh)
  - Python runs ONLY on the main server thread during Phase 1 and 3
  - GIL is acquired at start of Python work, released after
  - C++ threads (physics, network) run freely while GIL is held
    because they don't call Python

  Timeline of one tick:
  |<-- C++ native (no GIL) -->|<-- Python (GIL held) -->|<-- C++ -->|
  |  recv packets, physics    |  decide(), execute()    |  broadcast|
  |  ~15ms                    |  ~15ms                  |  ~5ms     |

  Python 3.13+ free-threading: eliminates this constraint entirely.
  But even with GIL, the design works fine at 20Hz.
```

---

## 5. Sandbox Safety

### Can Players Break the Server?

```
Threat                          Defense
-----------                     -------
Import os, subprocess           AST validator blocks forbidden imports
Infinite loop in decide()       CPU timeout (10ms), killed + logged
Allocate 10GB memory            Memory limit per player (50MB)
Access server filesystem        No `open()`, no `os.*`, no `pathlib`
Modify other players' objects   WorldView is scoped to radius
Crash the interpreter           try/except in C++ around all py calls
Escape sandbox via __class__    Forbidden dunder access in AST scan
eval/exec arbitrary code        Blocked in AST scan
Import C extensions (ctypes)    Only whitelisted imports allowed
```

### Defense in Depth

```
Layer 1: AST scan (before compilation)
  |  Reject source with forbidden imports, builtins, dunders
  v
Layer 2: Restricted builtins (at exec time)
  |  Only safe builtins available in namespace
  v
Layer 3: Resource limits (at runtime)
  |  CPU timeout, memory cap, mutation rate limit
  v
Layer 4: WorldView scoping (at API level)
  |  Can only see/modify within radius of origin
  v
Layer 5: try/except in C++ (at integration level)
     Any Python exception is caught, logged, entity is paused
```

---

## 6. Architecture Validation

### Does Each Piece Exist?

| Component | Technology | Maturity |
|-----------|-----------|----------|
| C++ server binary | CMake + standard C++17 | Production-ready |
| Embed Python in C++ | pybind11 (or nanobind) | Mature, widely used |
| Python hot-reload | `exec()` + class replacement | Standard Python feature |
| AST-based sandbox | `ast` module | Standard library |
| CPU timeout for Python | `signal.alarm` or C++ thread timeout | Standard |
| Memory limit | `resource.setrlimit` or cgroup | Standard |
| Python client rendering | moderngl + pyglet | Mature |
| TCP networking | BSD sockets / asyncio | Standard |
| Voxel chunk storage | Custom + SQLite/LevelDB | Proven by Luanti |
| Greedy meshing | Well-documented algorithm | Textbook |

### Risk Assessment

```
Risk                              Likelihood  Impact  Mitigation
----                              ----------  ------  ----------
Python too slow for many entities Low         Med     Tick spreading, C++ fast-paths
Sandbox escape                    Low         High    Multi-layer defense, audit
GIL bottleneck                    Low         Med     Python 3.13+ free-threading
Hot-reload corrupts state         Med         Med     Instance migration with defaults
Player writes bad code            High        Low     Validation, timeout, error recovery
Large world overwhelms server     Med         Med     Active chunk range, entity budget
```

---

## 7. Summary

```
+------------------------------------------------------------------+
|                        IS THIS DOABLE?                            |
+------------------------------------------------------------------+
|                                                                    |
|  C++ server embedding Python:       YES (pybind11, proven)        |
|  Hot-reload Python during gameplay: YES (exec + class replace)    |
|  Sandbox player code:               YES (AST + resource limits)   |
|  Python client with 3D rendering:   YES (moderngl + pyglet)       |
|  Performance at 20Hz with 200 entities: YES (15ms Python budget)  |
|  Player writes code in-game:        YES (text editor + upload)    |
|                                                                    |
|  All components use mature, production-tested technology.         |
|  No research breakthroughs needed.                                |
|                                                                    |
+------------------------------------------------------------------+
```
