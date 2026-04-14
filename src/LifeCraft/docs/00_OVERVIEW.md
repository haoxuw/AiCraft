# LifeCraft — Design Overview

> **Spore's cell stage, but multiplayer, and every behavior is Python you can
> edit mid-match. Rendered in chalk.**

## Elevator pitch

You are a **core** on a shared chalkboard. You sketch a shape around your
core — that shape becomes the body of your species. Your breed spawns,
eats, reproduces, and fights other players' breeds on the same board.
Every unit's AI is a Python `decide()` function you can rewrite while
the game is running.

Visual language: chalk strokes on a dark slate board, heavily inspired by
the *Chalk* (Bit Egg) aesthetic and the stroke-rendering trick from
*NumptyPhysics* (triangle strip with alpha gradient at the edges).

## Why this game exists

CivCraft proved one thesis: **Python is the game, C++ is the engine, and
players mod everything.** LifeCraft is the same thesis in a smaller,
faster, 2D shape:

- One match lasts minutes, not hours.
- The moddable surface (a shape + a `decide()` function) is small enough
  that a new player can author a working breed in 5 minutes.
- Multiplayer is the point. You're not modding in isolation — you're
  testing your breed against someone else's right now.
- Roguelike runs wrap matches: win → earn Mods/Buffs that unlock new
  cellular parts, stroke primitives, and Python helpers for the next run.

## The core loop

1. **Core.** Each player starts with one glowing core on the board. The
   core carries your identity: breed ID, unlocked mods, Python behavior.
2. **Draw a body.** Player sketches a chalk shape on the board. That
   shape becomes the **breed template** — the body every unit of yours
   will have.
3. **Spawn units.** The core produces units of the breed by consuming
   resources (chalk energy) scattered on the board. Each spawn copies
   the breed template.
4. **Physics from geometry.** Shape and enclosed volume drive physics:
   mass ∝ volume, moment of inertia from shape, drag from perimeter.
   Long and thin is fast and fragile; round and big is slow and tanky.
5. **Length budget.** Total alive chalk-length of your breed is capped.
   A longer body means fewer simultaneous units. Swarm vs. titan is a
   real strategic choice, not a numeric knob.
6. **Python brains.** Every unit runs your breed's `decide(self, world)`
   at ~4 Hz on its own agent process. Hunt, flock, flee, feed — whatever
   you code. You can hot-reload the Python mid-match.
7. **Domination.** Eat resources, eat rival units, protect your core.
   Cores are destructible; lose your core and you're out of the match.
8. **Roguelike meta.** Winning a match awards **Mods** (new stroke types,
   new Python helpers, new parts) and **Buffs** (per-run modifiers) for
   your next run.

## Conservation of mass (inherited from CivCraft)

Same invariant as CivCraft: **nothing is created from nothing.** Every
chalk stroke, every unit, every core has a `value`. The server enforces
that no action increases the board's total value.

- Drawing a shape consumes chalk energy equal to its length.
- **Spawning a unit costs material proportional to its mass.**
  Mass ∝ enclosed area (see § Shape → physics mapping), so a big
  tanky body costs far more to spawn than a small one.
- **Killing an enemy unit gives you its biomass** — the victim's mass
  value converts into material in your pool. Predation is the primary
  economy; drawing is just how you shape what that material becomes.
- When a unit dies with no killer (starvation, terrain), its mass
  returns to the board as neutral chalk dust anyone can harvest.
- Resources regenerate slowly from board edges (the "ecosystem" term).

### Grow bigger vs. grow more — the evolution choice

Material in your pool is fungible. At any moment you can spend it two
ways, and that choice *is* the strategy:

- **Grow bigger.** Redraw a bigger breed template (or trigger a
  mutation that enlarges it). Each unit is tankier, deals more damage,
  but costs more material to spawn and counts more against the
  length-budget cap.
- **Grow more.** Keep the breed small and spawn additional units.
  Numerical superiority, map coverage, distributed risk.

This mirrors how real species diverge — K-strategists (few big
offspring) vs. r-strategists (many small). Both are viable; which
wins depends on the board, the opponent's choice, and your Python
behavior.

This is what keeps the game honest in the presence of arbitrary Python
mods: no matter how clever your `decide()` is, you cannot make matter.

## Camera and world

- **World is a large 2D board**, much bigger than the screen. Multiple
  players and hundreds of units share it.
- **Top-down view.** No gravity — movement is free in X/Y.
- **Camera is locked to the player's core/unit.** The shape the player is
  controlling stays at screen center; the board scrolls underneath.
  Agar.io / Spore cell-stage style.
- Zoom may scale with the player's total breed mass (later — not MVP).

## Shape deformation — bodies are squishy, not rigid

Shapes are **not rigid bodies**. A unit's chalk outline deforms as it
moves and acts. This is what makes it possible to *bite*, *stab*,
*pulse*, *squeeze through gaps*:

- **Locomotion deformation.** A moving cell wobbles; a spiked shape
  pulses with each stroke; a long flagellum curls. The deformation is
  driven by the unit's velocity and an internal phase clock.
- **Combat deformation.** Attacking is a *shape distortion event* —
  extending a point outward along a chosen direction = a stab; rapidly
  closing two adjacent edges = a bite. Damage is dealt where the
  distorted edge overlaps an enemy's body.
- **Physical constraint.** Deformations are bounded: the deformed
  shape's length and area stay within epsilon of the breed template,
  so players can't cheese by "drawing" a tiny shape and then
  distorting it into a spear.
- **Implementation sketch.** Each shape vertex has a rest position (from
  the breed template) plus a displacement driven by a per-shape set of
  "muscles" (Python-controllable oscillators). Soft-body spring model,
  not Box2D rigid polygons.

This makes shape design expressive: *where* you put length on the
template determines *where* the creature can attack from.

## Shape → physics mapping

The **core** is the origin of the shape's local frame — every physics
value is derived relative to it.

| Physics stat | Default formula (from shape + core) |
|---|---|
| **Turn speed** | `∝ 1 / max(distance from core to any vertex)` — the "reach radius." A long spike out from the core turns slowly, a tight blob turns fast. |
| **Movement speed** | `∝ 1 / max_width_perpendicular_to_heading` — the widest section perpendicular to the travel direction. Wide frontal area = slow forward; streamlined needle = fast. |
| **Mass** | `∝ enclosed area` (closed shapes) |
| **Drag** | `∝ perimeter` |
| **Budget cost** | `= perimeter` (total chalk length; see § Length budget) |
| **Damage surface** | per-edge property; spiky/distorted edges deal damage on contact |

Closed shapes become **creatures** (a shape with a core socket).
Open strokes become **terrain/structures** (static after a grace
period, no core, no AI).

### Formulas are moddable

These are defaults. The **formulas themselves live in Python**, in a
`physics/` artifact category, so Mods can rewrite how shape maps to
stats. A Mod could e.g. make turn speed depend on the *narrowest*
radius instead of the widest, creating a strategically different
breed archetype. The server still enforces conservation invariants
on the *output* values regardless of formula.

```python
# artifacts/physics/base/default.py  (sketch)
def compute_stats(shape, core):
    reach  = max(dist(core, v) for v in shape.vertices)
    width  = max_width_perpendicular(shape, axis=shape.heading)
    area   = polygon_area(shape.vertices)
    return {
        "turn_speed":  BASE_TURN  / reach,
        "move_speed":  BASE_SPEED / width,
        "mass":        DENSITY    * area,
        "drag":        DRAG_COEF  * shape.perimeter,
    }
```

Python-derived stats are recomputed when the breed template changes
(redraw between lives, mutation event), not per-frame.

## Action types — same four as CivCraft

The server validates exactly four `ActionProposal` types. Everything a
core, a unit, or a player can do compiles to one of these.

| # | Type | LifeCraft meaning |
|---|------|-------------------|
| 0 | `TYPE_MOVE` | Set a unit's 2D velocity (Python emits this from `decide()`) |
| 1 | `TYPE_RELOCATE` | Pick up / drop a resource pellet; move a chalk object |
| 2 | `TYPE_CONVERT` | Draw stroke (chalk budget → stroke), spawn unit (resources → unit), kill unit (unit → dust), erase (stroke → nothing) |
| 3 | `TYPE_INTERACT` | Toggle drawn switches/gates (later-game content) |

No new action types. The 2D nature of the game is entirely contained in
the *payload* of these primitives, never in new server opcodes.

## Architecture — mirrors CivCraft exactly

Three process types. Always TCP. Identical in singleplayer (localhost)
and multiplayer (remote server).

| Process | Binary | Responsibility |
|---|---|---|
| Server | `lifecraft-server` | Headless. Owns board, runs 2D physics, validates actions. **No Python, no OpenGL.** |
| Player client | `lifecraft` | 2D chalk rendering, mouse-drawing input, draw-board UI. **OpenGL, no Python.** |
| Agent client | `lifecraft-agent` | One process per unit. Runs Python `decide()`. **Python + pybind11, no OpenGL.** |

Dependency rules match CivCraft:

- `platform/` — game-agnostic engine (already shared with CivCraft)
- `LifeCraft/` — must never `#include "CivCraft/..."` and vice versa
- Shared code gets promoted to `platform/` first

## Artifact system — same layout as CivCraft

Python artifacts are hot-reloadable, namespaced, shareable.

```
src/LifeCraft/artifacts/
  breeds/       base/cell.py, base/spike.py, ...     (shape template + stats)
  behaviors/    base/wander.py, base/hunter.py, ...  (decide() functions)
  mods/         base/speedboost.py, base/spikes.py, ...
  buffs/        base/regeneration.py, ...            (roguelike run modifiers)
  resources/    base/chalk_pellet.py, ...
  boards/       base/arena.py, base/maze.py, ...     (match layout + spawn rules)
```

Forking and sharing works identically to CivCraft: fork `base/cell.py`
→ `player/my_cell.py`, namespace is rewritten automatically, upload to
share. See `CivCraft/docs/00_OVERVIEW.md` § Artifact System.

### Breed artifact (sketch)

```python
breed = {
    "id": "base:cell",
    "name": "Basic cell",
    "behavior": "wander",
    "shape": {
        "points": [(0,0), (1,0), (1,1), (0,1)],  # polyline, will be
        "closed": True,                           # simplified by server
    },
    "base_hp": 10,
    "eat_range": 0.5,
}
```

### Behavior artifact (sketch)

```python
def decide(self, world):
    prey = world.nearest(kind="resource", radius=20)
    if prey:
        return MoveTo(prey.x, prey.y)
    return Wander()
```

## Roguelike meta — Mods and Buffs

Between matches the game is a roguelike run. Winning a match (or
meeting a match objective) awards one of:

- **Mods** — permanent additions to the breed design palette: new
  stroke types (jagged, curved, dashed), new body parts (flagellum,
  eye, spike), new Python helpers (`Flock()`, `Ambush()`).
- **Buffs** — per-run modifiers applied to your core: faster resource
  pickup, larger length budget, cheaper respawns. Buffs stack within
  a run and reset at run end.

Mods persist across runs (the meta-progression); Buffs are the
run-local risk/reward layer. Dying in PvP ends the run — you return
to the lobby with your Mods but none of the Buffs.

This makes the PvP session double as a roguelike, hence *"rogue life."*

## No difficulty selection — same design as CivCraft

Start is casual: a practice board, no opponents, just you drawing and
watching your breed work. Threats and stakes come only when you *choose*
to queue into a ranked match or accept an invitation. Matches the
CivCraft "no forced pressure" principle.

## Open design questions (to be resolved in later docs)

1. **One shape per breed per match, or redrawable?** Leaning redrawable
   between lives but not during a life; maybe "mutation" events that
   force a redraw with length budget changes.
2. **Core count per player.** One core (single point of failure) vs
   hive (cores are spawnable and cheap). First match type ships with
   single-core to keep stakes legible.
3. **Length-budget formulation.** Global simultaneous cap
   (`Σ length_alive ≤ cap`) is the shipping default — matches the
   "more line = fewer units" wording directly.
4. **Deformation authoring.** Two options: (a) a fixed set of built-in
   locomotion styles (wobble, pulse, crawl) selected by the breed, or
   (b) Python-authored per-breed deformation functions. (b) is far more
   expressive and matches the "mod everything" principle — likely the
   long-term target with (a) as a library of defaults.
5. **Soft-body simulation budget.** Deformable shapes are much more
   expensive than rigid Box2D polygons. With hundreds of units on a
   board, a cheap spring-mesh approximation is the likely path rather
   than a full FEM. Benchmark once MVP physics is in.

## Implementation status

**M0 (shipped)** — single `lifecraft` binary. Opens a window, renders a
procedural chalkboard background (shader, no texture asset), and turns
mouse drag gestures into feathered chalk strokes with Douglas-Peucker
simplification and along-length grit noise. No server split, no
physics, no Python yet. Build with `make game GAME=lifecraft`.

**M1 (next)** — split into `lifecraft-server` + `lifecraft` (GUI) + a
stub `lifecraft-agent`. TCP protocol mirrors CivCraft's. Stroke
uploads go through `C_ACTION TYPE_CONVERT`; server stores and
broadcasts `S_STROKE`. Clear is a second CONVERT.

**M2+** — shape → physics body (mass / turn / move stats from
geometry), cores, breed templates, spawn loop, Python `decide()`, and
the conservation-of-mass economy.

The legacy `src/LifeCraft/godot/` prototype is retained as a **visual
reference** only — it pioneered the feathered-ribbon chalk look we
subsequently reimplemented in C++ shaders. The Godot project is not
built or launched by `make game GAME=lifecraft`; use
`make lifecraft-godot` if you want to compare the two renders.

## Research references

- `docs/REFERENCES.md` (later) — notes on *Chalk* (Bit Egg) aesthetic and
  numptyphysics (GPL-3, cloned at `/tmp/research/numptyphysics/` for
  study; key file `platform/gl/GLRenderer.cpp:792-835` — the 10-vertex
  per-segment feathered-ribbon technique. Our implementation is a fresh
  write with the same idea moved into a fragment shader.).

## Relationship to CivCraft

LifeCraft and CivCraft are **separate games sharing the C++ engine in
`src/platform/`**. They do not share artifacts, content, or game rules.
Any code either game wants from the other must first be promoted into
`platform/`. See the root `CLAUDE.md` § Two games, same engine.
