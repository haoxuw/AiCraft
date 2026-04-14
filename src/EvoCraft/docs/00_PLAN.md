# EvoCraft — Master Plan

Status: **planning** — no code yet. This document is the contract. Every file we
write lives under `src/EvoCraft/` and is authored fresh. Nothing is copied from
`src/EvolveCraft/` or `prototypes/godot_fish/`. The Godot prototype exists as a
visual *reference* (shader math, scene composition, feel) — not a source.

---

## 1. Vision

A Spore-cell-stage-inspired sandbox: a side-scrolling slab of water where
microscopic cells swim, hunt, eat, mutate, and sprout new body parts.
Painterly underwater aesthetic — animated caustics on the floor, a shimmering
water ceiling, swaying kelp, rising bubbles, volumetric god-rays through teal
fog. Fish-wiggle tail motion. Every species, body part, mutation rule, and
visual asset is moddable without touching C++.

Success means:
1. **It looks like a screensaver you'd leave running.** The first screenshot
   stands on its own as beautiful.
2. **Modders can ship new species in Python** without a compile. Drop a file
   in `artifacts/species/`, restart the server, it's in the game.
3. **Modders can ship new visuals in Godot resources.** Drop a `.tscn` or
   `.gdshader` into `mods/`, the client picks it up.
4. The engine is separable: server and client can be swapped independently.

## 2. Non-goals (v1)

- No multiplayer networking beyond localhost (protocol is TCP-ready; we don't
  stress-test it).
- No Spore creature/tribal/space stages. Cell stage only.
- No voxel blocks, chunks, biomes. Fixed rectangular slab.
- No save/load. (Defer to a later milestone.)
- No mobile/web export. Linux desktop first.

## 3. Architecture

Three roles, TCP-separated — same boundary as ModCraft, different tech stack
on the client:

| Role   | Binary             | Language   | Responsibility                                                            |
|--------|--------------------|------------|---------------------------------------------------------------------------|
| Server | `evocraft-server`  | C++        | Authoritative sim, physics, TCP broadcast, embeds pybind11 for behaviors  |
| Client | `evocraft-client`  | Godot 4    | Renders scene from TCP stream, handles input, sends `ActionProposal`      |
| Agent  | (collapsed in v1)  | C++ + Py   | Per-species `decide_batch()` runner — **in-process with server for v1**   |

For v1 the agent is collapsed into the server process (one pybind11 embed,
called on the sim tick). We split it into a separate process only when the
GIL or sim budget forces us to. This keeps M1–M5 lean.

**Rule compliance (from root `CLAUDE.md`):**

- **Rule 0** — Only `TYPE_MOVE`, `TYPE_RELOCATE`, `TYPE_CONVERT`, `TYPE_INTERACT`
  actions. `Eat(food) → RELOCATE food to cell, then CONVERT food to energy`.
  `Split → CONVERT self into two cells (parent shrinks, child spawns)`.
  `Attack → CONVERT jaw-momentum to target HP loss`. No new action kinds.
- **Rule 1** — Species, body parts, DNA, mutations all live in Python artifacts.
  Zero gameplay constants in C++; every number is read from a Python config.
- **Rule 2** — Player is a cell with `playable=true`. Any cell can be hijacked.
- **Rule 3** — Server is sole owner of sim state. Godot client predicts player
  movement locally and reconciles against server snapshots.
- **Rule 4** — Server has zero decision logic. Python species drive all AI.
- **Rule 5** — Server never decides display. Particles, sounds, floating text
  are derived client-side from the TCP event stream.

## 4. Coordinate system

Side-scroll slab. This is set in stone from day one — we do not repeat the
XZ→XY refactor pain of the previous codebase.

```
         +Y (surface at +7)
          │
          │      · · · · · (water ceiling plane)
          │
          │     ●   ●         (cells swim in XY)
          │        ●
          │  ●        ●
  ────────┼───────────────── +X  (floor at -4, width ±18)
          │ ▲  ▲  ▲  ▲  ▲    (caustics plane)
```

- `X ∈ [-18, +18]` horizontal screen axis.
- `Y ∈ [-4, +7]` vertical. Floor at `y=-4`, water surface at `y=+7`.
- `Z ∈ [-2, +2]` thin depth — small parallax only.
- Camera fixed at `(0, 0, 11)`, FOV 60°, looking `-Z`.
- Heading `θ = atan2(vy, vx)`. Sprites/meshes face along `+X` at `θ=0`.

Background props (kelp BG, distant rocks) scatter at `z ∈ [-22, -4]`;
foreground kelp at `z ∈ [3.5, 7]` for out-of-focus parallax.

## 5. Directory layout

```
src/EvoCraft/
├─ README.md
├─ Makefile                     # forwards to root with GAME=evocraft
├─ CMakeLists.txt               # included by root CMakeLists.txt
├─ docs/
│  ├─ 00_PLAN.md                # this file
│  ├─ 01_PROTOCOL.md            # TCP wire format (to be written in M1)
│  ├─ 02_MODDING.md             # Python + Godot mod contract (M4)
│  └─ 03_GODOT_CLIENT.md        # scene structure, shaders (M2)
├─ server/
│  ├─ main.cpp                  # evocraft-server entry
│  ├─ sim.h                     # tick loop, physics
│  ├─ slab.h                    # SwimSlab dims, boundary math
│  ├─ cell.h                    # Cell POD
│  ├─ food.h                    # Food POD
│  ├─ dna.h                     # DNA trait vector + mutation
│  ├─ part.h                    # body part attachments
│  ├─ net_protocol.h            # EvoCraft TCP message types
│  ├─ net_server.h              # listen/accept/broadcast
│  ├─ python_bridge.h           # pybind11 embed wrapper
│  └─ artifact_loader.h         # scans artifacts/, registers species
├─ shared/
│  └─ action.h                  # ActionProposal (the 4 types)
├─ artifacts/
│  ├─ species/
│  │  └─ base/
│  │     ├─ wanderer.py
│  │     ├─ predator.py
│  │     └─ prey.py
│  ├─ parts/
│  │  └─ base/
│  │     ├─ flagellum.py
│  │     ├─ jaw.py
│  │     ├─ eye.py
│  │     └─ spike.py
│  ├─ mutations/base.py
│  └─ worlds/base.py            # SlabTemplate: lighting, kelp density, species mix
├─ python/
│  ├─ behavior_base.py          # SpeciesDef + decide_batch contract
│  ├─ dna.py                    # DNA trait schema
│  └─ action.py                 # Python-side action builders
├─ godot/                       # self-contained Godot 4 project
│  ├─ project.godot
│  ├─ scenes/
│  │  └─ main.tscn
│  ├─ scripts/
│  │  ├─ net_client.gd          # TCP reader
│  │  ├─ world_view.gd          # holds MultiMeshes, updates per tick
│  │  ├─ cell_renderer.gd
│  │  ├─ kelp_renderer.gd
│  │  ├─ bubble_renderer.gd
│  │  ├─ input_controller.gd    # player cell control
│  │  └─ mod_loader.gd          # scans mods/, applies overrides
│  ├─ shaders/
│  │  ├─ caustics.gdshader
│  │  ├─ water_surface.gdshader
│  │  ├─ kelp.gdshader
│  │  └─ fish_wiggle.gdshader
│  └─ assets/                   # meshes, textures baked for Godot
├─ mods/
│  └─ README.md                 # how to drop in a mod
├─ tests/
│  ├─ cpp/
│  │  ├─ test_slab_boundary.cpp
│  │  └─ test_artifact_loader.cpp
│  └─ scenarios/                # headless e2e (server in --log-only mode)
│     └─ test_wanderer.py
└─ tools/
   └─ proto_dump.py             # TCP sniffer for debugging
```

## 6. Server data model (C++ sketches)

All structs are POD-ish, value types. No inheritance.

```cpp
// server/slab.h
struct SwimSlab {
    float halfWidth = 18.f;
    float floorY    = -4.f;
    float surfaceY  =  7.f;
    float halfDepth =  2.f;
};

// server/cell.h
struct Cell {
    uint32_t   id;
    uint16_t   speciesId;
    glm::vec3  pos;         // xy + small z
    glm::vec3  vel;
    float      heading;     // atan2(vy, vx), smoothed
    float      size;        // radius
    float      hp;
    float      energy;
    DNA        dna;
    std::vector<PartAttach> parts;
    bool       playable = false;
};

// server/dna.h — fixed-schema trait vector; mutations perturb floats in [0,1]
struct DNA {
    float speed;
    float turnRate;
    float senseRadius;
    float aggression;
    float bodyHue;
    float bodySat;
    float fatness;
    // Expanded via artifacts/mutations/*.py — schema is declared once in
    // python/dna.py, mirrored here by codegen or keyed access.
};

// server/part.h
struct PartAttach {
    uint16_t partId;       // indexes artifacts/parts/*
    glm::vec2 localOffset; // in cell-local space
    float     localAngle;
};

// server/food.h
struct Food { uint32_t id; glm::vec3 pos; float energy; };
```

Nothing about rendering leaks into these. Kelp clumps, rocks, bubble streams,
and god-rays are **visual-only seeds** — the server ships them once at connect
time via `S_SLAB_INIT` and never references them again.

## 7. TCP protocol

Separate namespace from ModCraft — `evocraft::net::`. Fresh wire format. New
docs file `docs/01_PROTOCOL.md` written during M1 as we implement.

```
S_SLAB_INIT        slab dims, kelp clumps[], rocks[], bubble streams[], lighting seed
S_CELL             per-cell delta (id, species, pos, vel, heading, hp, dna packed, parts diff)
S_CELL_REMOVE      id
S_FOOD             id, pos, energy
S_FOOD_REMOVE      id
S_EVENT            typed events for client-side FX: EAT, SPLIT, DEATH, MUTATE, BITE
C_HELLO            client version + desired playable species
C_ACTION           ActionProposal (one of 4 types)
C_SET_GOAL         click-to-move target (server computes greedy steering)
C_SELECT           camera-follow cell id
```

Every message is length-prefixed, little-endian, versioned. Client buffers
deltas and applies them on a fixed render tick.

## 8. Python modding contract

**Species**:

```python
# artifacts/species/base/wanderer.py
from evocraft import SpeciesDef, DNA, Action, MoveTo

class WandererSpecies(SpeciesDef):
    id = "base:wanderer"
    base_dna = DNA(speed=0.3, turnRate=0.5, senseRadius=4.0,
                   aggression=0.0, bodyHue=0.55, bodySat=0.6, fatness=0.4)
    spawn_parts = ["base:flagellum", "base:eye"]

    def decide_batch(self, cells, world, dt):
        actions = []
        for c in cells:
            target = wander_target(c, world, dt)
            actions.append(MoveTo(c.id, target))
        return actions

SPECIES = WandererSpecies()
```

**Parts**:

```python
# artifacts/parts/base/flagellum.py
from evocraft import PartDef

class Flagellum(PartDef):
    id = "base:flagellum"
    visual = "flagellum.tres"         # Godot resource path, resolved by client
    stat_boost = {"speed": +0.4, "turnRate": +0.1}
    energy_cost_per_tick = 0.002

PART = Flagellum()
```

**Mutations**: declarative — `artifacts/mutations/base.py` lists allowed
trait perturbations (delta range, probability). Server applies on `Split`.

**AST allowlist**: same pattern as ModCraft — `python_bridge.h` imports
artifact modules inside a pybind11 scope with a restricted `__builtins__`.
No filesystem, subprocess, or socket access from artifact code.

## 9. Godot client

Self-contained Godot 4 project at `src/EvoCraft/godot/`. Opens a TCP socket
on launch (from `scripts/net_client.gd`), reads the state stream, drives
MultiMesh instance buffers for cells/kelp/food.

### Scene tree (main.tscn)

```
Main (Node3D)
├─ Environment                 ACES tonemap, SSAO, glow, volumetric fog
├─ SunLight  DirectionalLight3D warm, upper-left
├─ FillLight DirectionalLight3D cool cyan, bottom-right
├─ Camera3D                    pos (0,0,11), fov 60, looking -Z
├─ SkyGradient  MeshInstance3D quad behind, vertex-gradient teal→dark
├─ FloorPlane   MeshInstance3D 120×100 at y=-4, caustics.gdshader
├─ WaterSurface MeshInstance3D 120×80  at y=+7, water_surface.gdshader (cull_front)
├─ KelpBG       MultiMeshInstance3D    kelp.gdshader, z ∈ [-22,-4]
├─ KelpFG       MultiMeshInstance3D    kelp.gdshader, z ∈ [3.5,7]
├─ Rocks        MultiMeshInstance3D
├─ CoralFans    MultiMeshInstance3D
├─ Cells        MultiMeshInstance3D    fish_wiggle.gdshader (per species)
├─ Food         MultiMeshInstance3D
├─ Bubbles      GPUParticles3D[]       per stream, emission SPHERE, gravity +Y
├─ Godrays      SpotLight3D[]          light_volumetric_fog_energy=45, spot_angle=5
└─ NetClient    Node                   owns TCP, feeds MultiMesh buffers
```

### Mods folder

At startup `scripts/mod_loader.gd`:
1. Lists `src/EvoCraft/mods/*/manifest.json`.
2. Resolves overrides: any mod-provided `.tres` / `.tscn` / `.gdshader` whose
   path matches a core resource (e.g. `shaders/fish_wiggle.gdshader`) replaces
   the core version.
3. For net-driven references (`PartDef.visual = "flagellum.tres"`), the loader
   checks `mods/` first, then falls back to `godot/assets/`.

Mods ship as a folder:

```
mods/glowing_jellies/
├─ manifest.json
├─ shaders/fish_wiggle.gdshader     # overrides fish shader
└─ assets/parts/flagellum.tres      # overrides default flagellum mesh/mat
```

Python-side mods (new species) live under `artifacts/species/<mod>/`.

## 10. Build system

### Root `CMakeLists.txt` additions

```cmake
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter Development)
find_package(pybind11 CONFIG REQUIRED)

add_executable(evocraft-server
    src/EvoCraft/server/main.cpp
    # ... other .cpp files
)
target_include_directories(evocraft-server PRIVATE
    src/platform
    src/EvoCraft
)
target_link_libraries(evocraft-server PRIVATE
    platform_shared
    platform_server_base   # TCP server primitives, no modcraft-specific code
    pybind11::embed
    Python3::Python
)
```

We may need to promote ModCraft's TCP server machinery into `platform/`
first (currently some of it leaks ModCraft types). Do this as a prep commit
before M1.

### Godot project

`src/EvoCraft/godot/` is a standalone Godot 4 project. Editor iteration uses
`godot --path src/EvoCraft/godot/`. For release, `godot --export-release` via
a Makefile target emits a binary next to `evocraft-server`.

### Makefile

Root `Makefile` gains:

```makefile
ifeq ($(GAME),evocraft)
game:
	$(MAKE) -C $(shell pwd) build
	$(GAME_BUILD_DIR)/evocraft-server --port 7888 &
	godot --path src/EvoCraft/godot/ --
endif
```

`src/EvoCraft/Makefile` forwards with `GAME=evocraft` so `cd src/EvoCraft &&
make game` works.

## 11. Milestone plan

Each milestone ends with a single concrete artifact — either a screenshot or
a log dump — that proves the step works.

### M1 — Skeleton + TCP handshake  *(~1 day)*

- Empty `src/EvoCraft/` skeleton matches §5 layout.
- `evocraft-server` binary builds, listens on `--port 7888`, broadcasts tick
  counter once per second.
- Godot client `main.tscn` opens, `net_client.gd` connects, prints received
  tick counter to Godot console.
- `make game GAME=evocraft` launches both cleanly, `make stop` kills both.
- **Artifact:** log dump showing 10 ticks received by Godot client.

### M2 — Scene composition (no gameplay)  *(~2 days)*

- Server ships `S_SLAB_INIT` with a static list of kelp clumps (seeded RNG).
- Godot renders: sky gradient, floor plane w/ caustics, water ceiling, kelp
  BG + FG with sway shader, fog, ACES tonemap, one sun + fill light.
- No cells yet. No particles yet.
- **Artifact:** screenshot `docs/screenshots/m2.png` matching the Godot
  prototype's first-frame aesthetic.

### M3 — Cells + physics + TCP streaming  *(~2 days)*

- Server spawns N cells in slab. Physics: drift + boundary forces on 6 faces
  of the slab. Cells broadcast via `S_CELL` deltas at 30Hz.
- Godot renders cells as a MultiMeshInstance3D with fish-wiggle shader.
  Heading drives per-instance yaw.
- No AI — cells have random initial velocities that decay.
- Port ModCraft's `[Perf]` rolling-window instrumentation (see
  `src/ModCraft/main_server.cpp:395–416`): log slow-tick count + per-stage
  breakdown (`physics/boundary/broadcast/...`) every ~5s against a tick
  budget. Keep the same `[Perf]` tag so tooling can grep both games.
- **Artifact:** screenshot + log showing cell count, positions updating.

### M4 — Embedded Python brain  *(~3 days)*

- Server links pybind11, `python_bridge.h` boots an interpreter, `artifact_
  loader.h` scans `artifacts/species/base/wanderer.py` and registers the
  species via an AST-allowlisted import.
- Every sim tick, server calls `species.decide_batch(cells, world, dt)`,
  receives `Action[]`, validates each as one of the 4 types, executes.
- Cells now wander coherently, approach food.
- Food pellets: `artifacts/species/base/wanderer.py` emits `RELOCATE(food →
  cell)` + `CONVERT(food → energy)` on overlap.
- **Artifact:** `/tmp/evocraft_game.log` showing `DECIDE` + `ACTION` events
  for 30 seconds of sim.

### M5 — Species differentiation + parts  *(~3 days)*

- Add `predator.py`, `prey.py`. Predator chases prey, prey flees.
- Body parts from `artifacts/parts/base/*` attach to cells. Each part has a
  Godot visual resource; client reads `S_CELL.parts[]` and builds child
  meshes.
- Attack = predator with `jaw` part collides with prey → `CONVERT(prey.hp →
  -N)`. On prey.hp==0, server emits `S_EVENT(DEATH)`, client plays particle.
- **Artifact:** screenshot showing three distinct species with visible parts.

### M6 — DNA + mutation + split  *(~2 days)*

- `Cell.dna` (7–12 floats) broadcast in `S_CELL`. Client derives hue/scale/
  fatness from DNA for visual variety.
- Split: cell with `energy > threshold` emits `CONVERT(self → 2 cells)`.
  Server applies mutation rules from `artifacts/mutations/base.py`.
- **Artifact:** log showing lineage: parent#12 split → child#45 with DNA
  delta `speed+0.08, aggression+0.02`.

### M7 — Polish  *(~3 days)*

- Godrays via SpotLight3D volumetric fog. Bubble streams via GPUParticles3D.
- Music (port `prototypes/godot_fish/scripts/music.gd` idea — reactive synth).
- Input controller: player selects + drives one cell (WASD / click-to-move
  with server reconciliation).
- Mod loader reads `mods/` folder and swaps resources.
- **Artifact:** a 30s screen recording.

Total: ~2 weeks of focused work. Each milestone is independently shippable.

## 12. Testing

- **Unit (C++, GoogleTest):** slab boundary math, DNA mutation determinism,
  artifact loader safety (malicious Python rejected by AST allowlist).
- **Integration (headless):** server runs with `--log-only` (no Godot client).
  Python scenarios spawn N cells, assert population invariants after T ticks.
  Events derived from TCP stream, mirroring ModCraft's log-only mode.
- **Visual QA (Godot):** screenshots at each milestone checked into
  `docs/screenshots/`. Use `godot --headless --script tools/screenshot.gd` for
  automated capture where possible.

## 13. What's explicitly *not* inherited from EvolveCraft

To enforce the "rewrite, don't copy" rule:

| Artifact                           | Decision                                                   |
|------------------------------------|------------------------------------------------------------|
| `src/EvolveCraft/server/*.h`       | NOT copied — rewrite `slab.h`, `sim.h`, `cell.h` fresh     |
| `src/EvolveCraft/shared/*.h`       | NOT copied — DNA/part/food rewritten with new schema       |
| `src/EvolveCraft/main.cpp`         | NOT copied — server has its own `server/main.cpp` entry    |
| `src/EvolveCraft/artifacts/`       | NOT copied — species files rewritten against new contract  |
| `prototypes/godot_fish/**.gdshader`| **Reference only** — rewrite w/ our naming + uniforms      |
| `prototypes/godot_fish/main.gd`    | **Reference only** — new `net_client.gd` + `world_view.gd` |
| OpenGL shaders (.vert/.frag)       | Gone. Godot uses `.gdshader` / `.gdshaderinc`              |

The Godot prototype is a visual spec: match its first-frame aesthetic. The
script and scene structure are being redesigned for a TCP-driven client.

## 14. Decisions deferred

- **Agent split vs. in-process Python.** In-process for v1 (M4). Revisit
  when GIL cost > 2ms/tick.
- **Save/load.** Defer past M7. Format likely a snapshot of all `S_CELL`
  deltas + RNG state.
- **Windows / macOS builds.** Linux-first. Godot exports cross-platform;
  C++ server needs CMake hygiene we haven't tested off-Linux.
- **Web export.** Out of scope for v1 — C++ server + Godot web-export is
  possible but requires WASM networking plumbing.

## 15. Retirement of `src/EvolveCraft/`

Once `src/EvoCraft/` reaches M2 (scene renders something better than the old
pond), we delete `src/EvolveCraft/` in a single commit. Until then, the old
tree is left in place but unbuilt (remove its target from root `CMakeLists.txt`
at the M1 commit so nothing stale can link).

---

**Next action after approval of this plan:** execute M1 — create the empty
directory skeleton (§5), wire `evocraft-server` into root `CMakeLists.txt`,
write the TCP handshake, create an empty Godot project with a `net_client.gd`
that prints received ticks, and land a `make game GAME=evocraft` target.
