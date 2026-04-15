# CellCraft — Web Client (scaffold)

Parallel Three.js + TypeScript port of CellCraft, living alongside the
native C++ client. Nothing in this directory compiles into or depends on
the native build.

## Run

```bash
cd src/CellCraft/web
npm install
npm run dev   # → http://localhost:5173
```

Production build (TS typecheck + Vite bundle):

```bash
npm run build
```

## Layout

```
src/
  main.ts              entry: boot + fixed-timestep main loop
  sim/
    tuning.ts          mirror of sim/tuning.h constants
    part.ts            Diet, PartKind, PartEffect
    monster.ts         Monster + lobed-circle shape builder
    world.ts           World, scatter_food, spawn_monster
    sim.ts             tick: move, boundary, MOUTH-gated pickup
  render/
    board_material.ts     ShaderMaterial: cream paper + fbm drift
    chalk_material.ts     ShaderMaterial: chalk ribbon (grit + feathered edge)
    cell_fill_material.ts ShaderMaterial: diet-tinted cell body fill
    renderer.ts           Three.js scene + per-frame geometry builders
  input/input.ts       WASD/arrows + click-to-move + R to restart
  ai/simple_ai.ts      wander/feeder heuristic (TS replaces Python decide())
```

## Controls

- **WASD** / arrow keys — move
- **Click** — order player cell to steer toward that world point
- **R** — restart scene

## Status

v1 scaffold: player cell + 4 AI cells + 16 food blubs on the cream
arena. No combat, no parts beyond MOUTH, no lab. Follow the native
`src/CellCraft/client/app.cpp` + `shaders/*.frag` for visual parity when
extending.
