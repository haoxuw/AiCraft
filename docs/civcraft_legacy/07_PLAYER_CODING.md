# CivCraft - Player Coding System

The defining feature: players write Python code inside the game to create new Objects and Actions, test them locally, then upload them to become real in the shared world.

---

## 1. The Player Creation Flow

```
+------------------------------------------------------------------+
|                    Player Creation Pipeline                        |
+------------------------------------------------------------------+

  1. IDEA
     Player wants a "Flying Pig" mob
       |
       v
  2. OPEN EDITOR
     Press F4 (or /editor command)
     Choose template: "Basic Mob"
       |
       v
  3. WRITE CODE
     Modify the template in the in-game editor
     - Define attributes (wing_power, altitude, etc.)
     - Write step() behavior (flapping, wandering, landing)
     - Reference or create textures
       |
       v
  4. TEST LOCALLY
     Click "Test Locally" button
     - Mini sandbox spawns the entity
     - 3D preview shows it in a test world
     - Console shows step() timing, errors, print() output
     - Player can interact: punch it, watch it move
     - Iterate: edit code -> re-test (instant hot-reload)
       |
       v
  5. UPLOAD
     Click "Upload to Server"
     - Client bundles source + assets
     - Server validates (sandbox check, resource limits)
     - On success: entity is registered globally
     - On failure: error shown in console ("line 42: os.system not allowed")
       |
       v
  6. USE IN WORLD
     - Player can spawn their creation via crafting, commands, or other Actions
     - Other players see and interact with it
     - Creator can update (re-upload) or delete it
       |
       v
  7. SHARE
     - Other players can inspect the source (read-only)
     - "Fork" button: copy source to their own editor, modify, upload as their own
     - Server tracks attribution chain: "based on alice:flying_pig"
```

---

## 2. What Players Can Create

### Objects

```
Players CAN create:
  +--------------------------------------------------+
  | New block types (custom ore, magic stone, etc.)   |
  | New mobs (animals, monsters, bosses)              |
  | New items (tools, weapons, potions, food)          |
  | New plants (crops, flowers, trees)                |
  | New fluids (custom liquids with unique behavior)   |
  | New NPCs (merchants, quest-givers)                |
  | New particles/effects (visual-only objects)        |
  +--------------------------------------------------+

Players CANNOT create:
  +--------------------------------------------------+
  | Core engine changes (rendering, physics impl)     |
  | Client-side hacks (wallhack, speed, fly)          |
  | Objects that bypass the sandbox                    |
  | Objects with unlimited range/damage/effect         |
  +--------------------------------------------------+
```

### Actions

```
Players CAN create:
  +--------------------------------------------------+
  | New spells (fireball, heal, teleport, summon)     |
  | New tool behaviors (multi-mine, auto-farm)        |
  | New interactions (trade, tame, breed, enchant)     |
  | New item effects (potion effects, food buffs)      |
  | New block triggers (pressure plates, traps)        |
  +--------------------------------------------------+

Players CANNOT create:
  +--------------------------------------------------+
  | Actions without validation (must have validate()) |
  | Actions that exceed range/radius limits            |
  | Actions that spawn unlimited entities              |
  | Actions that directly modify other players' data   |
  |   (must go through damage/trade systems)           |
  +--------------------------------------------------+
```

---

## 3. Sandbox Boundaries

### What the API Exposes

```python
# civcraft.api -- the ONLY module players interact with

# Base classes (extend these)
from civcraft.api import (
    PassiveObject, ActiveObject, LivingObject,
    PlayerObject, MobObject, NPCObject,
    FluidObject, EffectObject, ItemEntity,
    Action,
)

# Metadata
from civcraft.api import ObjectMeta, ActionMeta

# Types
from civcraft.api import (
    Vec3, BlockPos, EntityId,
    Attribute, Inventory, ItemStack,
    SoundSet, LootTable, LootEntry,
    ParticleSpec,
)

# WorldView (passed to step/execute, NOT instantiable)
from civcraft.api import WorldView

# Utilities (safe subset)
import math
import random
from typing import Optional, List, Dict, Tuple, Set
from enum import Enum
from dataclasses import dataclass
from pydantic import BaseModel
```

### What Players CANNOT Access

```python
# BLOCKED - these raise SandboxViolation on import or use:

import os                    # filesystem access
import sys                   # interpreter internals
import subprocess            # shell commands
import socket                # network
import http                  # network
import ctypes                # native code
import importlib             # dynamic imports
import ast                   # code manipulation
import pickle                # arbitrary deserialization
import threading             # thread creation
import multiprocessing       # process creation

eval(...)                    # arbitrary code execution
exec(...)                    # arbitrary code execution
open(...)                    # file I/O
__import__(...)              # dynamic imports
globals()                    # scope escape
type(...)                    # metaclass manipulation
```

### Resource Limits

```
Per-object-type limits:
  +-------------------------------------------+--------+
  | step() CPU time per call                  | 10ms   |
  | execute() CPU time per call               | 50ms   |
  | Memory per object instance                | 1MB    |
  | Total memory per player's object types    | 50MB   |
  | Max object types per player               | 100    |
  | Max action types per player               | 50     |
  | Max entities spawned per action           | 10     |
  | Max blocks modified per action            | 1000   |
  | Max WorldView radius for objects          | 32     |
  | Max WorldView radius for actions          | 64     |
  | Source code size                          | 100KB  |
  | Asset size per type (textures+models)     | 10MB   |
  +-------------------------------------------+--------+
```

---

## 4. Attribution & Ownership

```
Every artifact has:
  - author: player who uploaded it
  - created_at: timestamp
  - version: auto-incremented on update
  - based_on: original artifact ID (if forked)
  - license: open (can be forked) or closed (view-only)

Example artifact chain:
  base:pig (system)
    -> alice:flying_pig (v1, forked from base:pig)
      -> alice:flying_pig (v2, updated by alice)
      -> bob:rocket_pig (v1, forked from alice:flying_pig v1)
        -> bob:rocket_pig (v2, updated by bob)
```

### Permissions

```
+------------------+--------+--------+--------+--------+
|                  | Author | Others | Admins | System |
+------------------+--------+--------+--------+--------+
| View source      | yes    | yes*   | yes    | yes    |
| Edit / re-upload | yes    | no     | yes    | yes    |
| Delete           | yes    | no     | yes    | yes    |
| Fork             | yes    | yes*   | yes    | yes    |
| Spawn in world   | yes    | yes    | yes    | yes    |
| Inspect instance | yes    | yes    | yes    | yes    |
+------------------+--------+--------+--------+--------+

* if license is "open" (default)
```

---

## 5. Artifact Storage

```
Server-side:
  artifacts/
    objects/
      base/                          # Built-in (ships with game)
        dirt.py
        stone.py
        pig.py
        ...
      alice/                         # Player "alice"'s creations
        flying_pig.py                # v2 (latest)
        flying_pig.v1.py             # v1 (archived)
        magic_ore.py
      bob/
        rocket_pig.py
    actions/
      base/
        mine.py
        place.py
        attack.py
        ...
      alice/
        cast_fireball.py
    assets/
      base/
        textures/
          dirt.png
          pig.png
        models/
          pig.gltf
        sounds/
          pig_idle.ogg
      alice/
        textures/
          flying_pig.png
          magic_ore.png
        models/
          flying_pig_wings.gltf
```

---

## 6. In-Game Inspector

Players can inspect any object in the world to see its code:

```
Right-click entity/block -> "Inspect"

+====================================================================+
|  Inspector: alice:flying_pig                                        |
+====================================================================+
|                                                                      |
|  Type: LivingObject (ActiveObject > LivingObject)                   |
|  Author: alice                                                      |
|  Version: 2                                                         |
|  Based on: base:pig                                                 |
|                                                                      |
|  --- Live Attributes ---                                            |
|  hp:          8 / 10                                                |
|  hunger:      0.72                                                  |
|  wing_power:  5.0                                                   |
|  altitude:    12.3                                                  |
|  pos:         (142.5, 67.3, -89.1)                                 |
|  velocity:    (1.2, 0.3, -0.5)                                     |
|                                                                      |
|  --- Meta ---                                                       |
|  display_name: "Flying Pig"                                        |
|  max_hp: 10                                                        |
|  walk_speed: 2.0                                                   |
|  gravity_scale: 0.3                                                |
|  groups: {animal: 1, flammable: 1}                                 |
|                                                                      |
|  [View Source]  [Fork to Editor]  [Close]                           |
+====================================================================+
```

---

## 7. Crafting Player-Created Objects

How do player creations enter the game world?

### Option A: Recipes (Declarative)

```python
class MagicOre(PassiveObject):
    meta = ObjectMeta(
        id="alice:magic_ore",
        ...
        recipes=[
            CraftRecipe(
                pattern=[
                    "DGD",
                    "GSG",
                    "DGD",
                ],
                key={
                    "D": "base:diamond",
                    "G": "base:gold_ingot",
                    "S": "base:stone",
                },
                output_count=1,
            ),
        ],
    )
```

### Option B: Creative Spawning

```
Players with "creative" privilege can:
  /spawn alice:flying_pig          # spawn entity at cursor
  /give alice:magic_ore 64         # add to inventory
```

### Option C: Action-Based Spawning

```python
# An action that summons the creation
@Action
class SummonFlyingPig:
    meta = ActionMeta(
        id="alice:summon_flying_pig",
        trigger="player_input",
        cost={"stamina": 50},
        cooldown=30.0,
    )
    caster: EntityId

    def execute(self, world: WorldView):
        caster = world.get_entity(self.caster)
        world.spawn_entity(
            caster.pos + Vec3(0, 2, 0),
            "alice:flying_pig",
        )
```

### Option D: Natural Spawning

```python
class FlyingPig(LivingObject):
    meta = ObjectMeta(
        ...
        # World will naturally spawn these in suitable biomes
        spawn_biomes=["grassland", "forest"],
        spawn_chance=0.005,              # per chunk per spawn cycle
        spawn_light_min=8,
        spawn_group_size=(1, 3),
    )
```

---

## 8. Safety & Moderation

```
+------------------------------------------------------------------+
|                    Content Safety Layers                           |
+------------------------------------------------------------------+
|                                                                    |
|  Layer 1: Sandbox (automatic)                                     |
|    - AST analysis blocks dangerous imports/calls                  |
|    - Resource limits prevent DoS                                  |
|    - WorldView scoping prevents global state corruption           |
|                                                                    |
|  Layer 2: Rate Limits (automatic)                                 |
|    - Max N uploads per hour per player                            |
|    - Max total artifact count per player                          |
|    - Upload size limits                                           |
|                                                                    |
|  Layer 3: Server Admin Tools                                      |
|    - /ban_artifact <id>      -- disable an artifact               |
|    - /ban_player <name>      -- revoke upload privilege           |
|    - /inspect_artifact <id>  -- view source + usage stats         |
|    - /artifact_log           -- recent uploads                    |
|    - Auto-flag: artifacts with unusual resource usage             |
|                                                                    |
|  Layer 4: Community Reports (future)                              |
|    - Players can report problematic artifacts                     |
|    - Voting system for popular/useful content                     |
|    - Content marketplace / gallery                                |
|                                                                    |
+------------------------------------------------------------------+
```
