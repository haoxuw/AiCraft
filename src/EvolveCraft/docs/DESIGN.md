# EvolveCraft — Design Document

Status: design-only, nothing implemented. Implementation tracks the staged
plan in §16. Change this doc first, then code.

---

## 1. Vision

**You are a species director, not a single cell.** You seed a founder
organism into a shared pond, compose its body plan from evolving parts
(mouths, flagella, eyes, spikes, shells), write the Python AI its descendants
run, and author the mutation rules that shape how DNA and body plans drift
across generations. Hundreds of other directors share the pond. Thousands of
AI-controlled cells swim, hunt, flee, split, and die in real time. You win by
building a species that outcompetes the others — by strategy, by AI
authorship, by evolutionary engineering, or all three.

Spore's cell stage is the surface metaphor. The spine is ModCraft's
server-authoritative + Python-moddable architecture, rebuilt for a world
where the entity count is **two orders of magnitude higher** than ModCraft's.

---

## 2. Scale targets

These numbers drive every architectural choice below. Flag any deviation.

| Target | Value | Why it matters |
|---|---|---|
| Concurrent players per match | **200** | Hundreds of directors, not thousands; one shard, not MMO |
| Total concurrent cells | **5,000** | ~25 cells/player average; peaks at 20,000 in bloom events |
| Python AI decisions per second | **100,000** | 5,000 cells × 20Hz decide rate; unachievable with per-cell processes |
| Tick rate (server) | **30 Hz** | Half of ModCraft — cells are less reactive than players |
| Broadcast rate (to clients) | **10 Hz** | Delta-compressed; clients interpolate between broadcasts |
| Network budget per client | **100 KB/s steady, 500 KB/s burst** | Supports residential broadband |
| Server hardware assumption | **16 cores, 32 GB RAM, 10 Gbit NIC** | Single-box; no horizontal scaling v1 |
| Player hardware assumption | **GPU ≥ GTX 1060, 8 GB RAM** | Same as ModCraft |

If we miss a target, we cut scope — not scale. A 30-player match that plays
well beats a 200-player match that stutters.

---

## 3. Architecture overview

```
                  ┌────────────────────┐
                  │    Game Server     │
                  │  (authoritative)   │
                  │                    │
                  │  Region shards ─┐  │
                  │  Physics ───────┤  │
                  │  Broadcast ─────┤  │
                  │                 │  │
                  │  ┌──────────────▼──┐
                  │  │ BrainRuntime    │ ← species-batched Python
                  │  │  (pybind11)     │   runs N species × batch of cells
                  │  └─────────────────┘
                  └──────────┬─────────┘
                             │ TCP (platform protocol)
           ┌─────────────────┼─────────────────┐
           ▼                 ▼                 ▼
     ┌──────────┐      ┌──────────┐      ┌──────────┐
     │ Player 1 │      │ Player 2 │ ...  │ Player N │
     │  Client  │      │  Client  │      │  Client  │
     │          │      │          │      │          │
     │ Renders  │      │ Renders  │      │ Renders  │
     │ Authors  │      │ Authors  │      │ Authors  │
     │ species  │      │ species  │      │ species  │
     └──────────┘      └──────────┘      └──────────┘
```

**Three deliberate differences from ModCraft:**

1. **No per-entity agent client processes.** ModCraft spawns one
   `modcraft-agent` process per NPC. At 5,000 cells that's 5,000 processes —
   infeasible. EvolveCraft embeds Python in the server via `BrainRuntime`
   (pybind11) and runs species-batched decide() calls. See §4.
2. **Regions shard the world at the server.** A single GIL-bound Python
   runtime can't serve 5,000 cells at 20Hz. The world is carved into
   spatial regions; each region runs on its own OS thread with its own
   `BrainRuntime` instance. See §5.
3. **Clients never run Python.** They never did in ModCraft either — the
   rule is reinforced here because "send my decide() to a peer" would be a
   modding backdoor into arbitrary code execution.

---

## 4. AI runtime: species-batched Python

**The most important design decision in the entire game.**

### Why not per-cell decide()

ModCraft's pattern — one `decide(SelfEntity, LocalWorld) → Action` call per
NPC per tick — is clean and moddable but has O(n) Python-FFI overhead per
tick. Measured in ModCraft: ~50µs per call (pybind marshaling + decide body).
At 5,000 cells × 20Hz, that's **5 seconds of CPU per wall-clock second.**
Before the decide body even does anything.

### The species-batched API

Each species defines a single decide function that receives **all its cells at
once** and returns **all their actions at once**:

```python
# artifacts/species/base/hunter.py
def decide_batch(self_state, world_state):
    # self_state: array-of-struct for every hunter-species cell in this region
    # world_state: query API for nearby cells, food, walls
    #
    # Returns: parallel array of Actions
    actions = []
    for i in range(len(self_state)):
        me = self_state[i]
        prey = world_state.nearest_cell(me.pos, species="prey", radius=me.sense_range)
        if prey:
            actions.append(MoveTo(prey.pos))
        else:
            actions.append(Wander())
    return actions
```

This reduces Python-FFI crossings from `N_cells` per tick to `N_species` per
tick. At 10 species × 30Hz × 16 regions = 4,800 crossings/sec instead of
150,000. Measured headroom: ~30x.

Authors who want per-cell logic just write a loop inside `decide_batch` —
it's no more work for them, but the FFI cost is amortized.

### Vectorized world queries

`world_state` exposes fast C++ spatial queries:

```python
world_state.nearest_cell(pos, species=?, radius=?)         # O(1) via hash grid
world_state.cells_in_range(pos, radius)                    # O(k) with k = result count
world_state.ray_hit(from, dir, max_dist)                   # obstacle/boundary
world_state.food_in_range(pos, radius)                     # same hash grid
```

All queries are read-only snapshots of the region at tick start. No mutation
from Python. Python returns actions; the server validates and applies.

### Numpy interop

For advanced modders: `self_state.to_numpy()` gives a structured ndarray for
vectorized logic (`neighbor_count_within(50) < threshold` becomes one line
and runs in C). Opt-in; the for-loop API is the default and easier.

### Sandboxing & safety

Modded Python runs in the server process. This is a trust boundary.
- Matches are private (invite code or LAN). No public servers running
  arbitrary Python v1.
- Per-call timeout (5ms budget for a species). Overrun → that species' cells
  fall through to a C++ default behavior this tick and a warning fires.
- AST allowlist on loaded behaviors: no `import os`, `subprocess`, `open`,
  `__import__`. Whitelist `math`, `random`, `numpy`, plus the engine module.
- CPU+memory overruns across K ticks auto-disable the species for that match.

The alternative — a wasm Python sandbox or a DSL — is enormous work. Private
matches + AST allowlist is the pragmatic v1. Public matchmaking is a v2
problem.

---

## 5. World model & region sharding

### Physical model

2.5D swim field: entities move on the XZ plane (Y pinned near 0, small
visual bobbing for depth cues). Bounded circular pond, radius configurable
(default 500m, area ~785,000 m²).

Physics per tick:
- Velocity integration (`pos += vel * dt`)
- Drag (`vel *= exp(-drag * dt)`)
- Soft boundary push-back at pond edge
- Current field (optional, modded): `vel += world.currentAt(pos) * dt`
- No cell-vs-cell hard collision (they overlap visually); overlap triggers
  combat/eating actions instead

### Region sharding

The pond is gridded into 64×64m region cells. A player's view range is 128m,
so a client subscribes to its current region + 8 neighbors (max 9 active
subscriptions).

Each region runs on its own server thread:
- Owns entities currently in its AABB
- Owns its own `BrainRuntime` (one pybind11 interpreter per thread)
- Ticks independently; cross-region reads use a double-buffered snapshot

Entity crossing a region boundary: end-of-tick transfer. The source region
publishes the handoff; the destination region claims it on its next tick.
One-tick latency at boundaries is acceptable (cells drift at ~5 m/s, so a
crossing takes ~12 ticks anyway).

This is the only way to get 5,000 cells × 30Hz with GIL-bound Python. 16
regions × ~300 cells/region × Python cost = 5,000 cells total at tick budget.

### Entities & food distribution

Food and cells are stored per-region. A global spatial hash (thread-safe,
read-only during tick) lets regions query across boundaries for sense
checks. Mutations to the hash happen only at end-of-tick.

---

## 6. Entity model

One entity kind matters: `Creature`. Inherits from platform's `Living`
(position, velocity, hp, yaw). No inventory. No player-is-not-special rule
change — players don't even *have* an entity; they have a **species**.

```cpp
struct Creature : Living {
    SpeciesId       species;        // "base:hunter", "player42:my_swarm"
    OwnerId         owner;          // which player's species this is
    DNA             dna;            // trait values — see §10
    BodyPlan        bodyPlan;       // part assembly — see §7
    float           age;            // seconds alive
    float           energy;         // food reserve; splits when full
    std::vector<CreatureId> parents;// lineage, for evolution analysis
};
```

Two important notes:
- **The "player cell" is just a Creature owned by a player.** Same struct.
  The player doesn't inhabit it; they direct the species.
- **Species is data.** `SpeciesId` keys into a per-match registry that holds
  the player-authored `decide_batch`, DNA defaults, body plan, mutation fn.
  Changing a species's code re-binds its cells' behavior next tick.

`Food` is a separate kind: `Item`-shaped, no AI, nutritional value, decays
after T seconds.

---

## 7. Parts system — the body plan

**This is what gives each species identity, and what makes the 3D model
evolve visibly.** Equal in importance to the AI modding surface.

### Concept

A creature's body is a **plan**: a list of parts attached to **slots** on a
base torso. Each part has:
- A 3D mesh (authored in Blender or procedurally generated; see §8).
- Stat modifiers (speed, attack, defense, sense_range, energy_cost, …).
- Behavioral affordances (e.g. "can attack", "can absorb food at range",
  "emits light pulse"). These expose Python-callable actions to the AI.

A base torso has N slots at fixed positions + orientations (head, tail,
left/right sides, underbelly). Slots accept specific part types.

### PartDef — the modding unit

```python
# artifacts/parts/base/flagellum.py
PartDef(
    id="base:flagellum",
    display_name="Flagellum",
    slot_type="propulsion",              # can only attach to propulsion slots
    mesh="meshes/flagellum.glb",
    attach_point=(0, 0, -1),             # in mesh-local space
    attach_orientation=(0, 0, 0),
    stat_mods={
        "speed": +1.5,                   # adds 1.5 m/s max speed
        "energy_cost": +0.3,             # costs energy/sec to keep
    },
    actions=[],                          # no new AI actions; passive
    evolution={
        "mutable_traits": ["length", "beat_rate"],   # per-instance knobs
        "compatible_mutations": ["base:flagellum_long", "base:cilia"],
    },
)
```

```python
# artifacts/parts/base/jaw.py
PartDef(
    id="base:jaw",
    slot_type="head",
    mesh="meshes/jaw.glb",
    stat_mods={"attack": +5, "energy_cost": +0.1, "speed": -0.2},
    actions=["bite"],                    # exposes Bite action to AI
    evolution={"mutable_traits": ["tooth_count", "jaw_strength"]},
)
```

### BodyPlan — per-species, mutable across generations

```python
BodyPlan(
    torso="base:ovoid_medium",
    slots={
        "head":       ("base:jaw",       {"tooth_count": 8}),
        "propulsion": ("base:flagellum", {"length": 2.1, "beat_rate": 4.0}),
        "left_side":  None,                        # empty slot
        "right_side": ("base:spike",     {"length": 1.0}),
    },
)
```

`DNA` (§10) includes the BodyPlan. Mutation can:
- Swap a part for a `compatible_mutations` alternative
- Add a part to an empty slot
- Remove a part from a filled slot (regressive evolution)
- Drift the per-instance numeric traits (tooth_count, length, etc.)

### Stat aggregation

At spawn time, server computes effective stats: `torso.base_stats + Σ part.stat_mods`.
Speed caps, HP formulas, attack dice — all deterministic from the body plan.
The client gets the same body plan over the wire and computes the same
stats for display purposes.

---

## 8. 3D model pipeline

### Authoring parts (artist workflow)

- One `.glb` per part, authored in Blender.
- Each .glb has a single named "Attach" empty marking where it joins the
  torso. No bones required for v1; optional single-axis rotation animation
  (flagellum beat, jaw bite) baked into the mesh.
- Part mesh budget: **≤500 triangles.** Even a creature with 8 parts is
  under 5k tris, so a 5,000-cell scene fits in a modern GPU budget.
- Texture budget: single 256×256 albedo per part. Species tint applied as
  a shader uniform so player-color-coding costs nothing.
- Procedural scaling: every part mesh supports `scale_along_axis` for a
  handful of mutable traits (e.g. flagellum length, jaw tooth count shown
  as mesh instances along the jaw ridge).

### Runtime assembly

- Client loads all `PartDef` meshes at match start. ~50 part meshes × ~300
  KB each = 15 MB resident. Acceptable.
- For each creature: walk BodyPlan, instance torso mesh + one instance per
  attached part at its slot transform.
- GPU-instanced draw: one draw call per (part, LOD-tier) pair across all
  creatures in view. A pond with 1,000 visible cells using 4–8 parts each
  issues ~50 draw calls total, not 5,000.

### LOD tiers

| Tier | When | What's rendered |
|---|---|---|
| **Near** (≤20m) | player's inner camera | Full body plan, all parts, per-pixel shading |
| **Mid** (20–80m) | crowd | Torso mesh tinted by species + a single silhouette quad summarizing parts |
| **Far** (>80m) | horizon | 8×8 billboard dot, species color |

Tier transitions are per-tick, hysteretic (switch boundaries differ for
upgrade/downgrade to avoid popping).

### Model icons (species cards, leaderboards)

For every unique body plan currently in play, the server periodically
broadcasts a cached PNG thumbnail. Clients render it in the menu, not
per-frame.

### Procedural part generation (stretch)

A v2 mode: players describe a part in text ("a serrated spike with
curvature 0.3") → a preprocessing pass generates the .glb via a deterministic
procedural pipeline (parameterized SDF → mesh). Out of scope for v1; design
the `PartDef.mesh` field to accept either a file path or a generator spec so
v2 doesn't need a schema change.

---

## 9. Combat

Combat is a **collision-triggered CONVERT action** (Rule 0 holds):

1. Two cells overlap AABB.
2. Server checks: either has an offensive part? Either has "combat"
   permission in its species config?
3. If yes: `CONVERT attacker.energy + target.hp → damage`. Damage formula
   is part-derived (jaw.attack - target.defense) clamped ≥ 0.
4. If target.hp ≤ 0: `CONVERT target → food` (dead cell becomes food pellet
   of value = target's accumulated energy). Value-conserving.

This means:
- Combat costs the attacker energy. Can't infinite-attack.
- Winning a fight yields food. Tight feedback loop.
- No-parts cells can't attack — you must evolve aggression.

The AI decides when to attack via an action exposed by offensive parts:
```python
if jaw_equipped and world_state.nearest_cell(me.pos, hostile=True, radius=me.reach):
    return Attack(target_id)
```

---

## 10. Evolution system

### DNA schema

```python
DNA = {
    "body_plan": BodyPlan(...),          # structural — see §7
    "traits": {                          # numeric — drive per-cell stats
        "size": 1.0,                     # multiplier on torso scale
        "base_speed": 2.0,
        "base_sense": 25.0,
        "metabolism": 0.1,               # energy burn/sec
        "split_threshold": 10.0,         # energy to split
        "aggression": 0.4,               # AI decision weight
        "caution": 0.6,                  # AI decision weight
        "color_hue": 0.6,
    },
    "behavior_id": "player42:hunter_v3", # which species brain controls me
}
```

`aggression` and `caution` aren't hard rules — they're inputs the brain can
read to tune its decisions. This lets evolution affect behavior without the
brain being rewritten.

### Mutation

Two mutation operators, both player-moddable:

**Trait drift:**
```python
# artifacts/mutations/base/gaussian.py
def mutate_traits(parent):
    return {k: clamp(v * (1 + random.gauss(0, 0.05)), *TRAIT_BOUNDS[k])
            for k, v in parent.items()}
```

**Body plan drift:**
```python
# artifacts/mutations/base/body_drift.py
def mutate_body(parent_plan):
    if random.random() < 0.02:   # 2% chance per reproduction
        return swap_random_part(parent_plan)
    if random.random() < 0.01:
        return add_part_to_empty_slot(parent_plan)
    if random.random() < 0.005:
        return remove_random_part(parent_plan)
    return tweak_part_traits(parent_plan)  # numeric knobs inside parts
```

Players write their own mutate_* functions. This is core modding surface.

### Speciation

When does a lineage become a new species?
- v1: **never automatically.** A species is what the player declares at
  match start; mutation drifts within that species.
- v2: **trigger speciation** when lineage DNA distance exceeds threshold;
  spawn a new `SpeciesId`, letting it diverge behaviorally.

### Inheritance

Reproduction is asexual in v1 (matches Spore cell stage). Offspring =
mutate(parent_DNA). Parent retains original DNA (binary fission without
crossover is cleaner for balance — two parallel lineages diverge
independently).

Sexual reproduction (DNA crossover between two nearby compatible cells) is
a v2 feature. The BodyPlan data model already supports it — you pick parts
50/50 from each parent at mutation time — but the matchmaking logic (which
cells reproduce together) is non-trivial.

---

## 11. Networking at scale

### Interest management

Each client subscribes to: its own species (always, wherever they are) +
the 9 regions around its camera. Broadcast is per-region, so clients don't
receive updates for cells they can't see.

Back-of-envelope:
- 200 clients × 9 regions × ~300 cells/region avg = 540,000 cell-updates/tick
- Per cell update: 20 bytes (id, pos, vel, hp-delta, flags)
- At 10 Hz broadcast: 108 MB/sec total, 540 KB/sec/client
- Over budget by 5x.

Fixes:
1. **Delta encoding.** Only send fields that changed since last broadcast.
   pos/vel update every tick; hp/bodyplan rarely. Typical delta = 8 bytes.
   → 216 KB/sec/client. Still over.
2. **Quantize positions** to 16-bit fixed-point within region. Pos = 4 bytes
   not 12. → 108 KB/sec/client. On budget.
3. **Subsample distant cells** (mid-LOD cells broadcast at 3Hz, not 10Hz;
   far-LOD at 1Hz). Client interpolates. → 50 KB/sec steady.

### Owner-sensitive state

Your own species' cells broadcast at full rate and with full fields (you
want your DNA accurate). Others broadcast at reduced precision.

### Action channel

Client → server remains full rate (TCP, reliable). Player actions are:
- Select/command group (MoveTo, Attack)
- Species edit (push new decide, DNA, body plan)
- Match-level (spawn founder, declare extinction, vote end)

Volume tiny relative to state broadcast.

### Transport

TCP only. Platform already has it. Reliable ordering makes correctness
simple and 100 KB/sec easily fits in TCP's window. Add UDP only if
measurement proves it necessary.

---

## 12. Client rendering at scale

Worst case: 2,000 cells in view. GPU is easy (§8 LOD + instancing); the
hard part is the CPU side of updating transforms.

- Per-entity `transform` updates live in a `std::vector<glm::mat4>` updated
  in-place when `S_ENTITY` arrives. Re-upload the whole buffer once per
  frame (SSBO). No per-cell GL state changes.
- Cell billboards (far LOD) use a single point-sprite draw call with an
  instanced buffer of (pos, color, size).
- Selection UI (box-drag) operates on a spatial hash maintained client-side
  from the broadcast stream. Picks in O(log n).

Target: **60 FPS at 2,000 visible cells on a GTX 1060.** Back of envelope
says we clear this by 3x.

---

## 13. Modding surfaces (the player-facing API)

Everything a player can author is a Python file in their match's artifact
tree:

| Artifact kind | Purpose | Example file |
|---|---|---|
| `species/`        | Species definition: name, founder DNA, brain reference | `my_swarm.py` |
| `behaviors/`      | The `decide_batch` implementing the brain | `hunt_swarm.py` |
| `parts/`          | New body parts (mesh + stats + actions) | `razor_fin.py` |
| `mutations/`      | Mutation operators (trait drift, body drift) | `my_mutate.py` |
| `environments/`   | Match rules: boundary, food spawn, currents | `tidal_pool.py` |

A match loads base artifacts + each player's authored overrides. Naming is
namespaced: `player42:hunt_swarm` never collides with `player8:hunt_swarm`.

### Hot-reload

Editing a behavior or mutation function during a match rebinds it on next
tick. DNA changes only take effect on new offspring. Body-plan changes are
editor-only (not mid-match).

### Editor UI

Reuses ModCraft's `code_editor.h` + `behavior_editor.h`. Adds:
- Part catalog browser with 3D preview
- Body-plan assembler (drag parts into slots, live stat readout)
- DNA trait sliders with a population histogram
- Species brain console (print() statements from decide_batch route here)

---

## 14. Persistence

Per-match:
- Match log (JSONL): one entry per S_EVENT — births, deaths, attacks,
  mutations. Enables post-match analysis and evolution visualization.
- Species archive (zip): all authored .py + final DNA/bodyplan state.
  Re-loadable into future matches ("continue evolving my species").

Per-player:
- Species library: accumulated archives. Shareable as single-file mods.
- Rankings and cumulative stats.

No world save mid-match (matches are short, 10–30 min). If server crashes
mid-match, the match is void.

---

## 15. Security posture

The server runs modded Python from every connected player. **This is
the riskiest part of the design.**

v1 policy — private matches only:
- Matches are invite-only (match code) or LAN-only.
- AST allowlist on loaded .py (§4).
- Per-species CPU and memory quotas.
- No file I/O, no network, no subprocess from Python.
- Host can kick a player whose code misbehaves.

v2 policy — public matchmaking:
- Either: wasm-Python sandbox (CPython-emscripten-in-wasm), isolating each
  brain in its own memory+time-bounded VM.
- Or: restrict public play to a curated behavior library (no arbitrary
  code).
- Or: player ratings + reports; misbehaving code loses trust.

All options are hard. Ship v1 with private-match-only.

---

## 16. Build stages

Each stage:
- Has a concrete deliverable (code + assets)
- Has an automated verification (test green, or metric met)
- Leaves `make game GAME=modcraft` and its 65/65 tests passing

### Stage 0 — Platform contracts

*Prerequisite for everything else. Extracted from the existing platform to
remove the ModCraft coupling.*

- Introduce `IWorld`, `IWorldRenderer`, `IActionValidator`,
  `IArtifactLoader`, `IGameConfig` in `src/platform/`.
- Make `Inventory` optional on `Entity`.
- Refactor ModCraft to implement these. No behavior change for ModCraft.

**Verify:** `make test_e2e GAME=modcraft` → 65/65. No new tests yet.

### Stage 1 — SwimField world, one cell, log-only

- `SwimField` (bounded circle, flat plane, no currents).
- `swim_world.h`: spawn one cell at origin.
- `evolvecraft-server` + `evolvecraft` binaries build; GUI shows blue
  quad + one white dot.

**Verify:** `./evolvecraft --skip-menu --log-only` prints
`[World] 1 cell alive`, runs for 10s, shuts down clean.

### Stage 2 — Player click-to-move + food eating

- Reuse platform `pathfind.h` for click-to-move on fluid plane.
- Spawn 10 food pellets; cell eats on overlap.
- HP bar over cell.

**Verify:** scenario `test_eat.py` — force cell to cross each food pellet,
assert hp rose by (food.nutrition × 10).

### Stage 3 — Region sharding skeleton

- Carve pond into 8×8 regions; each runs on its own worker thread.
- Cross-region entity handoff.
- Still singleplayer; food & cells distributed.

**Verify:** stress test `spawn_1000_food_log_handoffs.py` — spawn 1000 food
particles moving in a line across 8 regions, assert every handoff logged
and no food lost.

### Stage 4 — BrainRuntime with species-batched Python

- Embed pybind11 per region.
- Define `decide_batch(self_array, world_query) → action_array` ABI.
- One species: "wanderer" with a 20-line decide_batch.

**Verify:** scenario `test_batch_decide.py` — 300 wanderer cells across 8
regions, assert every cell gets an Action each tick, Python runtime time
per region per tick < 2 ms.

### Stage 5 — Species registry + multiple species

- `SpeciesId` on `Creature`.
- Three base species: `wanderer`, `predator`, `prey`.
- BrainRuntime switches brain per species.

**Verify:** scenario `test_predator_prey_dynamics.py` — run 60s, assert
prey population < initial, predator population > initial.

### Stage 6 — Reproduction + trait-DNA + simple mutation

- `split` action, energy-threshold trigger.
- DNA = numeric traits only (no body plan yet).
- Gaussian mutation on split.

**Verify:** scenario `test_20_generations.py` — force 20 reproduction
rounds, assert DNA stddev > 0 and no NaN.

### Stage 7 — Parts system (data model, no 3D yet)

- `PartDef`, `BodyPlan` structs + Python artifacts.
- Stat aggregation at spawn.
- DNA includes BodyPlan.
- Body-plan mutation (swap/add/remove).
- Combat action keyed off `actions` exposed by parts.

**Verify:** scenario `test_body_plan_stats.py` — assemble known body plan,
assert computed stats match spec. `test_combat_part_gate.py` — cell
without jaw cannot attack.

### Stage 8 — 3D model assembly on client

- Load part `.glb` files.
- Build creature draw list from BodyPlan each tick.
- Near-LOD tier only (full parts). Sloppy, but visible.
- Author 4 torsos + 10 parts to start.

**Verify:** `--debug-scenario body_plan_gallery` writes
`/tmp/evolve_debug_*.ppm`: one screenshot per test body plan from 5 camera
angles. Human-eyeball check + file-exists assertion.

### Stage 9 — LOD + instanced draws

- Mid + far LOD tiers.
- GPU-instanced draws per (part, LOD) bucket.

**Verify:** perf test — render 2000 cells, assert frame time < 16 ms on
reference hardware.

### Stage 10 — Multiplayer connection layer

- Handshake gameId="evolvecraft".
- Player joins, picks a species slot, spawns one founder.
- Broadcast interest management (9-region subscription).
- Delta + quantize per §11.

**Verify:** harness spawns 1 server + 20 TestClients, runs 60s, assert no
client disconnects and bandwidth/client < 100 KB/s.

### Stage 11 — Brain editor UI

- In-match editor for species decide_batch, DNA defaults, body plan.
- Hot-reload on save.
- Part catalog browser with 3D preview.

**Verify:** manual — edit a brain mid-game, save, observe behavior switch
in next tick. Integration test: programmatically push a new decide via the
action channel, assert affected cells change goal text next tick.

### Stage 12 — Mutation modding

- `artifacts/mutations/` loaded per match.
- Editor UI for writing mutation functions.
- AST allowlist on all loaded Python.

**Verify:** scenario `test_custom_mutation.py` — load a mutation function
that doubles size on every split, run 5 splits, assert offspring size
follows 2^N curve.

### Stage 13 — Environment modding

- `artifacts/environments/` for boundary/food/currents.
- Match lobby picks environment.
- Apply currents in physics tick.

**Verify:** scenario `test_current_drift.py` — load rapid_current.py,
spawn 10 motionless cells, assert they drift ≥ 5 m downstream over 10 ticks.

### Stage 14 — Scale soak test

- 200 client harness (not real clients; TestClients with synthetic
  actions).
- 5,000 cells, 5 species.
- 10-minute run, no crashes, all perf budgets held.

**Verify:** `make soak-test` — runs the harness, produces a perf report.
Assertion suite: tick p99 < 16 ms, broadcast KB/s/client p99 < 100, no
Python timeout warnings.

### Stage 15 — Security hardening + public-match posture

- AST allowlist test suite (assert all denied ops actually raise).
- Per-species quota enforcement under adversarial Python
  (infinite loops, allocation bombs).
- Host moderation tools (kick, disable species, pause).
- Decide whether to ship public matchmaking in v1 or defer.

**Verify:** red-team scenarios:
- `def decide_batch(*a): while True: pass` → species auto-disabled in ≤10
  ticks, server still ticking.
- `[0] * 10**9` → memory quota kicks in, species disabled, no OOM.
- `__import__('os').system('rm -rf /')` → AST check rejects file at load.

### Stage 16 — Polish + first public playtest

- SFX (eat, split, death, attack).
- Match lobby screen.
- Post-match stats + lineage viewer.
- Starter species pack (5 curated species, "paper/rock/scissors" balance).
- Tutorial match.

**Verify:** 90-minute internal playtest with 6 players. Success criteria:
no crashes, no confused players at 10-min mark, at least one emergent
behavior not designed by any player.

### Timeline estimate

Assuming one focused developer (the realistic scenario):

| Block | Stages | Weeks |
|---|---|---|
| Platform prep | 0 | 1 |
| Core loop singleplayer | 1–6 | 4 |
| Parts + 3D | 7–9 | 3 |
| Multiplayer + scale | 10–11, 14 | 4 |
| Modding surfaces | 12–13 | 2 |
| Security + polish | 15–16 | 3 |
| **Total** | | **~17 weeks** |

Realistic range: 4–6 months to a playable multiplayer demo.

---

## 17. Verification strategy

Every stage has its own test, and together they form four cumulative
suites:

1. **Unit tests** (`src/EvolveCraft/tests/unit/`) — pure functions: DNA
   mutation, stat aggregation, region boundary handoff, AST allowlist.
2. **Scenario tests** (`src/EvolveCraft/tests/scenarios/`) — one-file
   gameplay assertions, run against `TestServer`, use the same headless log
   stream as ModCraft's 65 behavior tests.
3. **Perf tests** (`src/EvolveCraft/tests/perf/`) — measure + assert tick
   time, broadcast bandwidth, Python FFI time per region. Run in CI with
   regression budgets.
4. **Soak tests** (`src/EvolveCraft/tests/soak/`) — multi-minute, 200-client
   synthetic harness. Not in CI (too slow); run weekly + before releases.

Each stage gates on the relevant suite being green. CI matrix also builds
ModCraft and runs its 65-test suite every time, so platform changes can't
silently break the voxel game.

---

## 18. Open design questions

Flagged now so they don't ambush us later.

1. **Mesh pipeline authoring.** Blender + glTF is the starting answer.
   Do we need a custom export tool that enforces the "Attach empty" and
   triangle budget at export time? Probably yes by Stage 8.

2. **Sexual reproduction.** Deferred to v2, but: worth including a
   "compatibility" field in `PartDef` now so the data model doesn't need
   migration later? Probably yes — cheap insurance.

3. **Match length.** 10-minute fast matches or 60-minute marathons?
   Affects food spawn tuning, region size, perf soak length. Recommend
   20-min default, configurable.

4. **Extinction.** If a player's species hits 0 cells, are they eliminated
   for the match, or can they respawn with a new founder? Lean: respawn
   allowed 3x per match to keep players engaged; tune in playtests.

5. **Spectator mode.** Letting disconnected players watch their species
   keep evolving is compelling. Doubles broadcast load. Defer to v2.

6. **Public matchmaking vs. invite-only.** §15. The answer determines
   whether Stage 15 ships the wasm sandbox or just the AST allowlist.
   Strongly lean invite-only for v1 to de-risk.

7. **Procedural part generation.** §8 stretch. Tempting because it
   directly amplifies the modding surface, but the authoring pipeline is a
   project in itself. v2.

8. **Scripted "base" AI for low-traffic regions.** When a region has
   0 players watching, do we run its Python at reduced rate (e.g., 5 Hz) to
   save CPU? Yes, design for it — 5 Hz still gives adequate
   evolution pressure.

These are all punted until there's a reason to decide. Flagging beats
rediscovering them during implementation.

---

## 19. Relationship to ModCraft

EvolveCraft reuses ModCraft's spine:
- Server-authoritative world (Rule 3 ✓)
- Four action primitives only (Rule 0 ✓)
- Python for gameplay, C++ for engine (Rule 1 ✓)
- Player is not special — just owns entities (Rule 2 ✓)
- No server-side display logic (Rule 5 ✓)

Differs on:
- **Rule 4 (agent clients)** — ModCraft runs one subprocess per NPC;
  EvolveCraft embeds brains per region in the server. This is an
  *evolution* of Rule 4, not a violation: "all intelligence runs in isolated
  Python runtimes, separate from the server's authority" holds, but the
  isolation boundary is a thread, not a process.

The platform must accommodate both patterns without knowing which is in
use. `IAgentRuntime` in `platform/` is the seam — ModCraft plugs in
subprocess-based, EvolveCraft plugs in BrainRuntime-based. Design this
interface in Stage 0.
