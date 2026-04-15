# CellCraft

CellCraft is the Spore cell-stage / agar.io-in-chalk game that lives
alongside CivCraft in this repo.

## Primary implementation: the web client

**The active CellCraft implementation is the Three.js + TypeScript client at
[`web/`](./web/).** It has full sim + render parity with the old native
client, all scenes (menu / lab / starter / match / tier-up / end), a
dual-sim APEX ocean, a JS artifacts system (parts / monsters /
behaviors), and multiplayer via the Node WebSocket server in
[`web/server/`](./web/server/).

### Run

```bash
cd src/CellCraft/web
npm install
npm run dev          # Vite dev server on http://localhost:5173
```

### Tests

```bash
cd src/CellCraft/web
npm test             # currently 12/12 passing
```

### Multiplayer

A standalone Node WebSocket server owns a shared World and ticks the
sim. See [`web/server/`](./web/server/) for details.

```bash
cd src/CellCraft/web/server
npm install
npm run dev          # ws://0.0.0.0:7781
```

Connect a web client by appending `?mp=ws://localhost:7781` to the game
URL. Without `?mp=...` the client runs fully-offline single-player.

## Legacy: native C++ client

The original native C++ client (`client/`, `shaders/`, `main.cpp`, the
`sim/` C++ headers, `godot/` prototype) is **legacy and frozen**. It is
kept buildable for historical reference only and is no longer the
active development target. New features, bug fixes, and design work
happen in `web/`.

The native target still builds and runs:

```bash
make game GAME=cellcraft        # from repo root
# or
cd src/CellCraft && make game   # wrapper forwards to root Makefile
```

From `src/CellCraft/` you can also run:

```bash
make web                        # shortcut for (cd web && npm run dev)
make web-test                   # shortcut for (cd web && npm test)
```

## Design docs

Design docs under [`docs/`](./docs/) describe the shared game vision
(rules, tiers, parts, diets). Where the docs discuss the native C++
client specifically, treat that as historical — the web client is the
current implementation.
