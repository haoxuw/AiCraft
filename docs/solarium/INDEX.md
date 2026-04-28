# Solarium docs — index

Start here: read [`00_OVERVIEW.md`](00_OVERVIEW.md) before any gameplay change.
The mandatory design rules (four action types, Python-is-game, server-authoritative,
AI-on-agent-clients, server-has-no-display-logic) are summarized in the repo root
[`CLAUDE.md`](../../../CLAUDE.md).

## Architecture & core model

- [`00_OVERVIEW.md`](00_OVERVIEW.md) — full architecture: process model, TCP protocol, artifact system
- [`01_WORLD.md`](01_WORLD.md) — world: chunks, blocks, generation
- [`02_OBJECTS.md`](02_OBJECTS.md) — entities, items, blocks as first-class objects
- [`03_ACTIONS.md`](03_ACTIONS.md) — the four `ActionProposal` types: MOVE / RELOCATE / CONVERT / INTERACT
- [`09_CORE_LOOP.md`](09_CORE_LOOP.md) — per-tick loop, `decide()`, server/client responsibility split
- [`10_CLIENT_SERVER_PHYSICS.md`](10_CLIENT_SERVER_PHYSICS.md) — **mandatory**: unified entity tick, LocalWorld, shared physics, no player special-casing
- [`15_BLOCK_AND_ENTITY_MODEL.md`](15_BLOCK_AND_ENTITY_MODEL.md) — block & entity data model
- [`19_OBJECT_MODEL.md`](19_OBJECT_MODEL.md) — Python-as-game: artifact store, hot-loading, customization flow
- [`28_MATERIAL_VALUE.md`](28_MATERIAL_VALUE.md) — value-conservation rule for CONVERT / RELOCATE
- [`29_CHUNK_INFO.md`](29_CHUNK_INFO.md) — event-driven block awareness for agent clients

## Gameplay systems

- [`22_BEHAVIORS.md`](22_BEHAVIORS.md) — implemented creature AI: wander, peck, prowl, follow, woodcutter
- [`25_MULTI_BLOCK_OBJECTS.md`](25_MULTI_BLOCK_OBJECTS.md) — Structures: chests, beds, trees, houses — multi-block entities with completeness
- [`30_INVENTORY_MANAGEMENT.md`](30_INVENTORY_MANAGEMENT.md) — inventory slots, transfer rules
- [`31_CARRY_CAPACITY.md`](31_CARRY_CAPACITY.md) — carry weight, auto-pickup

## Player-facing features

- [`100_CORE_GAMEPLAY_FEATURE.md`](100_CORE_GAMEPLAY_FEATURE.md) — **the player-facing contract**: four camera modes (FPS/TPS/RPG/RTS), build/break, click-to-move, NPC loops that must work
- [`07_PLAYER_CODING.md`](07_PLAYER_CODING.md) — in-game Python editor: view & override behaviors
- [`17_RESOURCE_GUIDE.md`](17_RESOURCE_GUIDE.md) — art direction: making the voxel look great
- [`18_WEB_CLIENT.md`](18_WEB_CLIENT.md) — WASM web client design (OpenGL migration track)
- [`90_ADVANCED_GAMEPLAY.md`](90_ADVANCED_GAMEPLAY.md) — difficulty, progression, endgame hooks

## Planning & background

- [`12_FEASIBILITY.md`](12_FEASIBILITY.md) — feasibility note: C++ server embedding hot-reloadable Python
- [`13_MILESTONE_1.md`](13_MILESTONE_1.md) — M1: playable flat-world sandbox
- [`16_ARTIFACT_REGISTRY_TODO.md`](16_ARTIFACT_REGISTRY_TODO.md) — artifact registry TODO
- [`20_EASTERN.md`](20_EASTERN.md) — mythology/theming notes
- [`24_BEHAVIORS_PLANNED.md`](24_BEHAVIORS_PLANNED.md) — planned behavior-tree system (sketch, not implemented)
- [`TODO_structures.md`](TODO_structures.md) — superseded by `25_MULTI_BLOCK_OBJECTS.md`

## Working on the game

- [`DEBUGGING.md`](DEBUGGING.md) — iterative dev loop, screenshot pipeline, in-game shortcuts
