# Entity Behaviors

All entity AI is defined in Python artifacts at `artifacts/behaviors/base/`.
Each behavior is a `decide(self, world)` function called at 4Hz by the entity's
agent client process. Behaviors return actions: `Idle`, `Wander`, `Follow`,
`Flee`, `MoveTo`, `BreakBlock`, `DropItem`.

## Behavior Index

### wander.py — Herd Animals (Pigs)

Pigs roam in groups, flee from threats, and have idle activities.

**Key behaviors:**
- **Herd sticking**: rejoins friends if too far apart (`group_range`)
- **Stampede**: when one pig flees, nearby pigs panic and flee too
- **Mud seeking**: heads toward water blocks to "wallow"
- **Grazing**: stops to eat (configurable `graze_chance`)
- **Flee**: runs from players and cats

**Parameters:** `flee_range` (5), `group_range` (6), `graze_chance` (0.25)

### peck.py — Chickens

Timid birds that scatter, lay eggs, roost, and flock together tightly.

**Key behaviors:**
- **Egg laying**: 20% chance to drop an egg when startled (cooldown 10s)
- **Roosting**: seeks elevated blocks (fence, wood) at dusk to perch on
- **Dust bathing**: occasional idle "dust bath" activity
- **Tight flocking**: chickens regroup at 4 blocks (tighter than pigs at 6)
- **Flee**: runs from players and cats, faster than pigs

**Parameters:** `scatter_range` (4), `egg_chance` (0.20), `roost_height` (1.5)

### prowl.py — Cats

Moody, unpredictable. Randomly picks a mood each cycle (6-16 seconds):

**Moods:**
- **Hunt** (30%): stalk and chase chickens, give up after `chase_range`
- **Curious** (~10-30%): follow a nearby player, stare, then get bored
- **Explore** (~20%): slow wander
- **Nap** (~15%): seek high perch or curl up on ground

**Always:** flees from dogs regardless of mood.

**Parameters:** `chase_range` (10), `flee_range` (5), `curiosity` (0.3)

### follow.py — Dogs

Loyal companion that follows, guards, and plays.

**Key behaviors:**
- **Follow owner**: prefers nearest player, falls back to villager
- **Guard**: chases cats away from owner
- **Bark alert**: barks at unfamiliar entities near owner
- **Play bow / wag**: random idle animations when sitting by owner
- **Patrol**: wanders between home and nearby points when no owner found

**Parameters:** `follow_dist` (3), `guard_range` (6), `patrol_range` (12)

### woodcutter.py — Villagers

Resource gatherer with a day/night cycle and social interactions.

**Day states:** searching → walking → chopping → returning → resting → (socialize?) → searching

**Key behaviors:**
- **Resource gathering**: alternates between wood and stone
- **Day/night cycle**: works during day, returns home at night to sleep
- **Socializing**: walks to nearby villagers to "chat" during rest breaks
- **Player greeting**: occasionally waves at nearby players
- **State machine**: 7 states with timeouts and transitions

**Parameters:** `work_radius` (30), `social_chance` (0.3)

## World Support for Behaviors

The village world template generates features that behaviors interact with:

| Feature | Blocks | Used By |
|---------|--------|---------|
| **Village pond** | `base:water` + `base:sand` | Pigs (mud seeking) |
| **Farm plot** | `base:farmland` + `base:wheat` + water channel | Villagers (future farmer role) |
| **Animal pen** | `base:fence` perimeter with gate | Visual containment for animals |
| **Tree clearing** | `base:wood` in surrounding forest | Villagers (woodcutting target) |
| **House roofs/fences** | `base:wood`, `base:fence`, `base:planks` | Chickens (roosting), Cats (perching) |

## Creating Custom Behaviors

Fork any behavior and edit:
```bash
# In-game: Inspect entity → [E] → edit Python → Apply
# Or fork via Handbook → "Fork to Custom"
# Or edit directly: artifacts/behaviors/player/my_behavior.py
```

All parameters are optional and read via `self.get("key", default)`.
Override per-entity through creature definitions or the behavior editor.

## Behavior API Reference

Available actions (from `modcraft_engine`):
```python
Idle()                              # stop and apply friction
Wander(speed=2.0)                   # random walk direction
MoveTo(x, y, z, speed=2.0)         # walk to position
Follow(entity_id, speed=2.0, min_distance=1.5)  # follow entity
Flee(entity_id, speed=4.0)         # run away from entity
BreakBlock(x, y, z)                # mine a block
DropItem(item_type, count=1)       # spawn item entity at feet
```

World data available in `decide(self, world)`:
```python
world["dt"]         # seconds since last decide (0.25 at 4Hz)
world["nearby"]     # list of nearby entities: {id, type_id, category, x,y,z, distance, hp}
world["blocks"]     # list of nearby notable blocks: {x,y,z, type, distance}
world["time"]       # world time (0=midnight, 0.5=noon)
self["x"], self["y"], self["z"]   # entity position
self["walk_speed"]                 # from creature definition
self["type_id"]                    # e.g. "base:pig"
self.get("key", default)          # read optional parameter
```
