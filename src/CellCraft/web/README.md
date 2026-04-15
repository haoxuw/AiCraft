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

## Multiplayer

The `server/` subdirectory holds a standalone Node WebSocket server
(`ws` + TypeScript) that owns a shared World and ticks the sim. Run it
alongside the Vite dev server:

```bash
cd src/CellCraft/web/server
npm install
npm run dev              # listens on ws://0.0.0.0:7781
```

Then connect a client by adding `?mp=ws://localhost:7781` to the game
URL (e.g. `http://localhost:5173/?mp=ws://localhost:7781`). Without
`?mp=...` the client runs the existing fully-offline single-player
match unchanged.

The server runs NO AI (per CLAUDE.md Rule 4) — it's currently a
human-vs-human arena. AI opponents remain client-side in single-player.

## Status

v1 scaffold: player cell + 4 AI cells + 16 food blubs on the cream
arena. No combat, no parts beyond MOUTH, no lab. Follow the native
`src/CellCraft/client/app.cpp` + `shaders/*.frag` for visual parity when
extending.
