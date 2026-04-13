# EvolveCraft (stub)

Spore-like cell-stage game. Builds on `src/platform/` alongside ModCraft.

**Status:** directory skeleton only — no source yet. Added during the
two-game restructure so the platform split has a second consumer for sanity
checks, and so future EvolveCraft work has a place to land without another
restructure.

## Planned structure

```
shared/      swim_field.h (IWorld for a 2D/2.5D fluid plane), cell_traits.h
server/      swim_world.h (entity spawning, food respawn)
client/      fluid_renderer.h (IWorldRenderer), cell_drawer.h (billboard sprites)
content/     builtin.cpp — baseline cell + food kinds
artifacts/   cells/ food/ mutations/ environments/ behaviors/ models/
python/      fluid helpers, evolution rules
shaders/     fluid.{vert,frag}, cell.{vert,frag}
main.cpp, main_server.cpp, main_client.cpp
CMakeLists.txt
```

## Scope

**In:** one swimming cell (playable), food pellets, wander behavior, eat-to-
heal, a second cell that eats/flees. No inventory, no blocks, no structures.

**Out (cell-stage doesn't need):** `Inventory`, `Chunk`, `BlockRegistry`,
`WorldTemplate`, structure blueprints.

## Platform contract EvolveCraft will implement

These interfaces are defined in `src/platform/` (or will be, once Phase 3 —
"introduce IWorld, IWorldRenderer, IActionValidator, IArtifactLoader,
IGameConfig" — completes):

| Interface | EvolveCraft implementation |
|---|---|
| `IWorld` | `SwimField` (unbounded 2D plane, `isSolid=false`, boundary push-back) |
| `IWorldRenderer` | `FluidRenderer` (flat blue quad + billboards) |
| `IActionValidator` | `SwimActionValidator` (MOVE to target, CONVERT food→hp, cell split) |
| `IArtifactLoader` | registers cell / food / mutation defs |
| `IGameConfig` | `{ gameName:"EvolveCraft", artifactRoot:"src/EvolveCraft/artifacts", ... }` |

Until those interfaces exist, ModCraft uses `ModCraft::World` directly; we'll
lift them into `platform/` when EvolveCraft starts and needs a second impl.
