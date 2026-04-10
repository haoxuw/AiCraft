# Behavior Tree Tutorial

> **DESIGN SKETCH — NOT IMPLEMENTED.**
> This document describes a planned visual behavior tree editor. The editor
> does not currently exist. The compiled Python examples here also use a stale
> dict-style API and import `Follow`/`Flee` from `modcraft_engine` — neither
> is correct. See `docs/21_BEHAVIOR_API.md` for the actual behavior API.

The behavior tree editor lets you define entity AI visually using
IF/THEN/ELSE trees. No coding required — pick conditions and actions
from dropdown menus. The system compiles your tree to Python automatically.

## Quick Start

Right-click any entity → "Behavior Tree" section → build your tree.

## Building Blocks

### Conditions (IF tests)

| Condition | What it checks |
|-----------|---------------|
| **Is Day** | Time between 6am and 6pm |
| **Is Night** | Time between 6pm and 6am |
| **Is Dusk** | Time between ~4pm and ~6pm |
| **Threatened** | Player or cat within 5 blocks |
| **Just Startled** | Player or cat within 4 blocks (tighter) |
| **HP Low** | Health below 30% of max |
| **See Entity** *(param)* | Specific entity type nearby (e.g. `chicken`) |
| **Near Block** *(param)* | Specific block type nearby (e.g. `wood`) |
| **Far From Flock** | No same-species entity within 4 blocks |
| **Near Water** | Water block within 15 blocks |
| **Random %** *(param)* | Random chance 0-100 each tick (e.g. `30` = 30%) |

### Actions (THEN do this)

| Action | What it does |
|--------|-------------|
| **Stand Still** | Stop moving, apply friction |
| **Wander** | Walk in random direction |
| **Follow** *(param)* | Chase entity type (e.g. `player`) |
| **Flee From** *(param)* | Run away from entity type (e.g. `cat`) |
| **Follow Player** | Follow nearest player |
| **Attack** *(param)* | Chase + attack entity type |
| **Go to Block** *(param)* | Walk toward block type (e.g. `wood`) |
| **Break Block** *(param)* | Mine a block type |
| **Drop Item** *(param)* | Drop an item (e.g. `egg`) |
| **Graze** | Stand still, show "Grazing" |
| **Nap** | Stand still, show "Napping zzz" |
| **Seek Roost** | Find elevated wood/fence block, climb to it |
| **Seek Water** | Walk toward nearest water |
| **Socialize** | Walk to nearest same-species entity |
| **Patrol** | Slow wander near home position |

## Node Types

Each node in the tree is one of:

- **Action** — a leaf: does something (Wander, Flee, etc.)
- **Condition** — a test: checks something (Threatened, Is Night, etc.)
- **IF / THEN / ELSE** — a branch: if condition then do A, else do B
- **Sequence** — multiple steps in order

## Examples

### Example 1: Timid Chicken (Simple)

```
IF [Threatened]
  THEN [Flee From: player]
  ELSE [Wander]
```

The chicken runs from players, otherwise wanders. Simplest useful behavior.

### Example 2: Egg-Laying Chicken

```
IF [Threatened]
  THEN
    IF [Just Startled]
      THEN [Drop Item: egg]
      ELSE [Flee From: player]
  ELSE
    IF [Is Dusk]
      THEN [Seek Roost]
      ELSE
        IF [Far From Flock]
          THEN [Follow: chicken]
          ELSE [Wander]
```

This chicken:
1. When threatened and just startled → drops an egg
2. When threatened but not startled → flees
3. At dusk → seeks a roost (elevated block)
4. If far from other chickens → rejoins flock
5. Otherwise → wanders

### Example 3: Guard Dog

```
IF [See Entity: cat]
  THEN [Follow: cat]
  ELSE
    IF [See Entity: player]
      THEN [Follow Player]
      ELSE [Patrol]
```

Dog chases cats when visible, follows the player when nearby, patrols when alone.

### Example 4: Farmer Villager

```
IF [Is Night]
  THEN [Stand Still]
  ELSE
    IF [Near Block: wheat]
      THEN [Break Block: wheat]
      ELSE
        IF [Near Block: wood]
          THEN [Break Block: wood]
          ELSE [Wander]
```

Works during the day: harvests wheat first, then chops wood, wanders when nothing to do. Sleeps at night.

### Example 5: Adventurous Pig

```
IF [Threatened]
  THEN [Flee From: player]
  ELSE
    IF [Near Water]
      THEN [Seek Water]
      ELSE
        IF [Random %: 20]
          THEN [Graze]
          ELSE
            IF [Far From Flock]
              THEN [Follow: pig]
              ELSE [Wander]
```

Flees threats, seeks water when available, occasionally grazes, stays with herd.

### Example 6: Moody Cat

```
IF [See Entity: dog]
  THEN [Flee From: dog]
  ELSE
    IF [Random %: 25]
      THEN
        IF [See Entity: chicken]
          THEN [Follow: chicken]
          ELSE [Nap]
      ELSE
        IF [Random %: 30]
          THEN [Follow Player]
          ELSE [Wander]
```

Always flees dogs. 25% of the time: hunts chickens or naps. Otherwise: 30% chance
to follow the player, 70% wanders. Unpredictable mood.

## How It Works Under the Hood

The visual tree compiles to Python code like:

```python
from modcraft_engine import Idle, Wander, Follow, Flee, DropItem
import random as _rng

def decide(self, world):
    if any((e["category"] == "player" or e["type"] == "base:cat")
           and e["distance"] < 5.0 for e in world["nearby"]):
        if any((e["category"] == "player" or e["type"] == "base:cat")
               and e["distance"] < 4.0 for e in world["nearby"]):
            return DropItem("base:egg", 1)
        else:
            for e in world["nearby"]:
                if e["type"] == "base:player":
                    return Flee(e["id"], speed=5.0)
            return Wander()
    else:
        if (0.65 < world.get("time", 0.5) < 0.80):
            # seek_roost code...
        else:
            if all(e["distance"] > 4 for e in world["nearby"]
                   if e["type"] == self["type"]):
                for e in world["nearby"]:
                    if e["type"] == "base:chicken":
                        return Follow(e["id"])
                return Wander()
            else:
                return Wander()
```

You can see the Python Preview in the inspect panel to verify.

## Apply Scope

When you click Apply:
- **"Apply to This Pig"** — only this specific entity gets the new behavior
- **"Apply to All Pigs"** — every pig in the world gets it

Instance behaviors are saved to `artifacts/behaviors/player/entity_N_behavior.py`.
Type behaviors are saved to `artifacts/behaviors/player/custom_XXXXXX.py`.

## Advanced: Raw Python

For behaviors too complex for the tree (timers, state machines, memory), edit
the `.py` files directly in `artifacts/behaviors/player/`. See `docs/22_BEHAVIORS.md`
for the full Python API reference.
