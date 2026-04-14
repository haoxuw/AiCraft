# CreatureForge

A procedural creature editor aiming to surpass Spore's Creature Creator
on body fidelity, locomotion believability, and modding surface.

## Status

**Scaffolding only.** No UE install on this machine — project source is
ready to open in **Unreal Engine 5.3+** once installed.

## Layout

```
CreatureForge.uproject           Project manifest
Config/DefaultEngine.ini         Renderer + physics defaults (Lumen on, Chaos on)
Source/CreatureForge/
  CreatureForge.Build.cs         Module deps (GeometryScripting, ControlRig, …)
  CreatureForge.Target.cs        Game target
  CreatureForgeEditor.Target.cs  Editor target
  Public/
    MorphologyGraph.h            Save-file schema (the design-defining type)
    CreaturePartAsset.h          Palette-part DataAsset
    BodyCompiler.h               Incremental recompile interface
  Private/
    CreatureForgeModule.cpp
    MorphologyGraph.cpp          Per-subgraph hashing
    BodyCompiler.cpp             Stage-1 scaffold; passes stubbed
Content/Parts/                   Where part .uassets land (hot-reload target)
docs/STAGES.md                   Build plan, one stage per commit
```

## Opening the project

1. Install Unreal Engine 5.3 (or newer) from Epic Games Launcher, or
   build from source (`git clone https://github.com/EpicGames/UnrealEngine`).
2. Right-click `CreatureForge.uproject` → *Generate project files*.
3. Open the generated `.uproject`; UE will build the `CreatureForge`
   module on first launch.

## Build order

See `docs/STAGES.md`. Each stage = one commit + one screenshot.
