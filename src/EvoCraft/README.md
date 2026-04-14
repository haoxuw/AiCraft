# EvoCraft

Spore-cell-stage-inspired game. Completely separate codebase from `src/EvolveCraft/`
(which is being retired). Rewritten from scratch — nothing copied.

- **Server:** C++ (`evocraft-server`), embeds pybind11. Authoritative sim.
- **Client:** Godot 4 project (`godot/`). Renders slab via TCP stream.
- **Mods:** Python for gameplay (`artifacts/species/…`), Godot resources for visuals (`mods/`).

Start here: [`docs/00_PLAN.md`](docs/00_PLAN.md).

Visual reference only (NOT imported): `prototypes/godot_fish/`.
