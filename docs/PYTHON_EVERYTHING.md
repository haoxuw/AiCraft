# Python-Everything Architecture

## Core Design Principle

**All game content and gameplay rules are defined in Python. C++ is the engine; Python is the game.**

The C++ server is a dumb validator and physics engine. It knows HOW to move entities, check collisions, and send network packets. It does NOT know WHAT a pig is, HOW fast a player walks, or WHAT happens when TNT explodes. All of that comes from Python.

```
Python (the game)                 C++ (the engine)
+---------------------------+     +---------------------------+
| Creatures: pig, chicken,  |     | Physics: moveAndCollide   |
|   player, villager, ...   | --> | Network: TCP serialize    |
| Behaviors: wander, peck,  |     | Rendering: chunks, models |
|   follow, woodcutter      |     | Collision: AABB checks    |
| Items: sword, shield, ... |     | Audio: OpenAL playback    |
| Blocks: stone, TNT, ...   |     | Input: GLFW events        |
| Actions: break, place, .. |     |                           |
| World gen: terrain, trees |     | Validates proposals       |
+---------------------------+     | Applies physics           |
                                  | Broadcasts state          |
                                  +---------------------------+
```

## What MUST be Python

| Category | Examples | Python Location |
|----------|----------|-----------------|
| **Creature definitions** | Player, Pig, Chicken, Dog, Cat, Villager | `artifacts/creatures/base/*.py` |
| **Creature stats** | walk_speed, max_hp, collision_box, gravity_scale | Inside creature .py files |
| **Creature models** | Box model geometry (body parts, colors, sizes) | `artifacts/models/base/*.py` |
| **Behaviors** | wander, peck, follow, prowl, woodcutter | `artifacts/behaviors/base/*.py` |
| **Items** | Sword, shield, potion (visual + stats) | `artifacts/items/base/*.py` |
| **Blocks** | Stone, TNT, wheat, wire (properties + behavior) | `artifacts/blocks/base/*.py` |
| **Actions** | Break block, place block, attack, use item | `artifacts/actions/base/*.py` |
| **World templates** | Flat world, village world (terrain + structures) | `artifacts/templates/base/*.py` |
| **Starting inventory** | What items spawn with each creature type | In creature .py or world config |
| **Sounds** | Creature sounds, footsteps, dig sounds | `artifacts/resources/base/*.py` |

## What stays in C++

| Category | Why |
|----------|-----|
| **Physics engine** (`moveAndCollide`) | Performance-critical, runs 60x/sec for all entities |
| **Network protocol** | Binary serialization, TCP handling, message types |
| **Rendering pipeline** | OpenGL/WebGL, chunk meshing, model drawing |
| **Collision detection** | AABB intersection, raycast |
| **Audio engine** | OpenAL setup, spatial audio math |
| **Input handling** | GLFW keyboard/mouse events |
| **Chunk storage** | 16x16x16 block arrays, memory management |

## Current Gaps (SHOULD-BE-PYTHON but still hardcoded in C++)

### High Priority — Core Gameplay Rules

| Constant | Current Value | C++ Location | What it controls |
|----------|---------------|-------------|------------------|
| Block break distance | 8.0 blocks | `server.cpp:60` | How far player can reach to break blocks |
| Block place distance | 8.0 blocks | `server.cpp:118` | How far player can reach to place blocks |
| Speed clamp multiplier | 3.0x walk_speed | `server.cpp:20` | Anti-cheat: max allowed velocity |
| Item attraction radius | 3.0 blocks | `server.h:271` | Distance at which items fly toward player |
| Item pickup radius | 1.2 blocks | `server.h:271` | Distance at which items enter inventory |
| Item attraction force | 15.0 | `entity_manager.h:196` | How fast items fly toward player |
| TNT fuse ticks | 60 | `server.cpp:169` | Time before TNT explodes |
| TNT explosion radius | 3 blocks | `world.h:159` | Blast radius |
| TNT chain fuse | 20 ticks | `world.h:234` | Delay for chain-reaction TNT |
| Wheat growth ticks | 600 per stage | `world.h:169` | How fast crops grow |
| Wire power decay | -1 per block | `world.h:191` | Redstone-style signal propagation |

### Medium Priority — Entity Behavior

| Constant | Current Value | C++ Location | What it controls |
|----------|---------------|-------------|------------------|
| Behavior tick rate | 0.25s (4 Hz) | `entity_manager.h:114` | How often AI decides |
| Turn speed | 4.0 deg/frame | `entity_manager.h:226` | How fast entities rotate |
| Idle velocity decay | 0.85x | `entity_manager.h:241` | Friction when idle |
| Block scan radius | 30 blocks | `entity_manager.h:119` | AI perception range |
| Stuck check interval | 2.0s | `server_tuning.h` | How often stuck detection runs |
| Stuck nudge height | 1.5 blocks | `server_tuning.h` | How high to teleport stuck entities |

### Low Priority — World Generation

| Constant | Current Value | C++ Location | What it controls |
|----------|---------------|-------------|------------------|
| Noise octave scales | 0.008, 0.025, 0.07, 0.15 | `noise.h:34-37` | Terrain bumpiness |
| Noise octave amplitudes | 30, 12, 4, 1.5 | `noise.h:34-37` | Terrain height range |
| House count | 4 | `world_template.h:202` | Village structure count |
| House positions | Hardcoded offsets | `world_template.h:203-206` | Village layout |
| Village path dimensions | Width 2, length -20 to +25 | `world_template.h:261-270` | Path generation |
| Spawn search iterations | 50 | `server.h:78` | How hard to look for safe spawn |

## The Player Is Not Special

The player character (`base:player`) is defined identically to any creature:
- Same EntityDef structure as pig, chicken, dog
- Same rendering path (model lookup from EntityDef.model)
- Same physics (moveAndCollide with entity collision box)
- Same behavior dispatch (BehaviorId prop → Python decide())

The only things that distinguish a "player entity" from an animal:
1. **Spawned on client connect** — server creates one when a TCP client joins
2. **Input source** — WASD/mouse instead of Python `decide()` function
3. **Not saved to disk** — per-session, re-created on reconnect
4. **Camera follows it** — client-side only, server doesn't know about cameras

Any entity can be "possessed" (controlled by input instead of AI). Any entity can run behaviors. A pig could have a camera follow it. A player could run on auto-pilot.

## How to Add New Content

### New Creature
Create `artifacts/creatures/base/mycritter.py`:
```python
creature = {
    "id": "base:mycritter",
    "display_name": "My Critter",
    "model": "mycritter.gltf",
    "walk_speed": 3.0,
    "run_speed": 6.0,
    "max_hp": 12,
    "collision_box_min": [-0.3, 0.0, -0.3],
    "collision_box_max": [0.3, 0.8, 0.3],
    "default_behavior": "wander",
    "default_props": {"hp": 12, "age": 0.0},
}
```

### New Behavior
Create `artifacts/behaviors/base/mybehavior.py`:
```python
def decide(self, world):
    # Return an action: Move, BreakBlock, Follow, Flee, Idle
    nearest = world.nearest_entity("base:player")
    if nearest and nearest.distance < 5.0:
        return Follow(nearest.id)
    return Wander()
```

### New Block
Create `artifacts/blocks/base/myblock.py`:
```python
block = {
    "id": "base:myblock",
    "display_name": "My Block",
    "color_top": [0.8, 0.2, 0.2],
    "color_side": [0.6, 0.15, 0.15],
    "hardness": 2.0,
    "drop": "base:myblock",
    "sound_break": "dig_stone",
    "sound_place": "place_stone",
}
```

## Migration Path

The 82 hardcoded constants listed above should migrate to a `GameplayConfig` Python structure, similar to how `WorldGenConfig` works. This would be loaded from a Python file at server startup:

```python
# artifacts/config/gameplay.py
gameplay = {
    "block_break_distance": 8.0,
    "block_place_distance": 8.0,
    "item_attraction_radius": 3.0,
    "item_pickup_radius": 1.2,
    "tnt_fuse_ticks": 60,
    "tnt_explosion_radius": 3,
    "wheat_growth_ticks": 600,
    "behavior_tick_rate": 0.25,
    # ... etc
}
```

The C++ server reads this at startup and uses the values instead of hardcoded constants. Python modders can change any value without touching C++.
