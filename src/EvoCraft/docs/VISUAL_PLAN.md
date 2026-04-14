# EvoCraft — Visual Plan (v1)

**Goal:** reproduce the Spore cell-stage look-and-feel from the reference
screenshots, frame-for-frame. Everything else is subordinate.

**Architecture for v1: Godot-only.** No C++ server, no TCP, no pybind11, no
Python. A single Godot 4 project at `src/EvoCraft/godot/` runs the whole
thing. All sim happens locally in GDScript. If/when we want modding or
multiplayer, we revisit; not before the visuals land.

Reference screenshots live under `docs/screenshots/reference/`.

## Ground rules

- **Every visible object has ≥1 animated parameter.** Static = dead.
- **Each stage ends with a committed screenshot** next to the reference,
  named `docs/screenshots/stage_N.png`. If it doesn't match, the stage isn't
  done.
- **No new abstraction before pixels.** Copy-paste is fine; extract once
  three call sites exist.

## Stages

### Stage 0 — Strip netcode
Delete `net_client.gd`. Replace it with `local_sim.gd` that produces the
same snapshot API (`update_cells`, `update_food`, `update_player_stats`).
`world_builder.gd` stays untouched.

### Stage 1 — Underwater backplate
WorldEnvironment with ACES tonemap, glow, volumetric fog density ~0.03,
teal fog tint. Sky gradient. Caustics floor. Water ceiling with ripple.
Two god-ray SpotLight3Ds animated ±2° yaw over 8 s.

### Stage 2 — Kelp parallax
Procedural 12-segment kelp ribbon, per-vertex `sin(TIME + seed + uv.y*3)`
sway. Two MultiMesh layers: `KelpBG` at z∈[-20,-6] (DOF-blurred),
`KelpFG` at z∈[4,7] (foreground blur).

### Stage 3 — Hero creature
One cell. `microbe.gdshader` tuned: wobble 0.06, breathing pulse 1.2 Hz,
tracking pupils, occasional blink. Flagellum: amplitude & frequency both
velocity-driven. 2–3 side cilia with desynced phase seeds. Body banks
±15° into turns. Idle wander on Perlin. It should look alive for 30 s
with no input.

### Stage 4 — Species variations
DNA-slider variants (hue, scale, spike count/length). Green antagonist,
red spiky predator — all driven by shader uniforms, one mesh family.

### Stage 5 — Food + particles
Emissive red food pellets (bloom-hot). `GPUParticles3D` bubble streams
from the floor, light motes drifting across the whole scene.

### Stage 6 — HUD
Radial gauge bottom-right (population / food / evolve progress) drawn via
`@tool` custom draw. F3 toggles debug text top-left.

### Stage 7 — Dynamic polish
Camera micro-motion, ambient audio bed, chromatic aberration, film grain.
30 s screen recording is the deliverable.

---

**Timeline:** ~7 working sessions. Each stage is independently shippable.

**What NOT to do in v1:** no pybind11, no TCP, no authoritative server, no
mod loading, no save/load, no multiplayer, no web export.
