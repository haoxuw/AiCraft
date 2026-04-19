# Object Model — Everything is Python

## Principles

Every game concept is a Python definition that players can view, fork, and modify from the in-game editor. The C++ engine is a generic runtime that loads and executes Python data. No game content is C++ source code.

### Built-in Naming Rule

**All built-in content uses plain, single-word names.** No adjectives, no modifiers, no compound names.

- YES: Sword, Shield, Boots, Helmet, Cape, Potion
- NO: Iron Sword, Leather Boots, Traveler's Cape, Health Potion
- YES: Pig, Dog, Cat, Villager, Chicken
- NO: Wild Pig, Guard Dog, Orange Tabby Cat

Built-ins are archetypes — the simplest version of each concept. Players create variants (Diamond Sword, Fire Potion) by forking and modifying. The built-in is always the plain, unadorned version.

---

## Handbook Grouping

```
Built-in | Custom
├── Living          — things with HP, behaviors, AI
│   ├── Creatures   — AI-driven (pig, chicken, dog, cat, villager)
│   └── Characters  — player skins
├── Objects         — things in the world or inventory
│   ├── Items       — holdable/wearable (sword, shield, potion, boots)
│   └── Blocks      — placed in voxel grid (dirt, stone, wood)
└── Logic           — rules and code
    ├── Effects     — what happens (heal, damage, haste, poison)
    └── Behaviors   — AI scripts (wander, peck, follow, prowl, woodcutter)
```

---

## Concept Hierarchy

```
Object                          ← everything in the world
├── Block                       ← placed in the voxel grid (1x1x1)
│   ├── Terrain blocks          ← dirt, stone, sand, grass, snow, water
│   ├── Plant blocks            ← wood, leaves (trees)
│   └── Active blocks           ← TNT, crops, wire, logic gates
│
├── Creature                    ← living entity with behavior + stats
│   ├── Player                  ← human-controlled (keyboard/mouse/auto-pilot)
│   ├── Animal                  ← AI-driven (pig, chicken, dog)
│   └── Creatures                     ← AI-driven humanoid (villager)
│
└── Item                        ← exists in inventory, usable
    ├── Weapon                  ← sword, bow
    ├── Shield                  ← blocks damage
    ├── Potion                  ← consumable with effect
    ├── Tool                    ← bucket, fishing rod
    └── Placeable               ← furniture, torch (becomes a block when placed)
```

---

## Core Concepts

### 1. Object

The base of everything. Has an ID, a name, and belongs to a category.

```python
object = {
    "id": "base:dirt",           # unique ID (namespace:name)
    "name": "Dirt",              # display name
    "category": "block",         # block | creature | item
    "description": "...",        # flavor text
}
```

### 2. Attributes

Properties that define an object's characteristics. Numeric, bounded, change-tracked.

```python
# On blocks:
"hardness": 0.8,                 # how long to break (0 = instant, -1 = unbreakable)
"solid": True,                   # entities collide with it
"transparent": False,            # light passes through
"light_emission": 0,             # 0-15 (torch = 14)

# On creatures:
"max_hp": 20,
"walk_speed": 2.0,
"gravity": 1.0,
"eye_height": 1.5,

# On items:
"stack_max": 64,
"cooldown": 0.5,                 # seconds between uses
```

### 3. Behavior

A Python function that decides what a creature does each tick. Reads the world (read-only), returns an action.

```python
# artifacts/behaviors/base/wander.py
from civcraft_engine import MoveTo
from behavior_base import Behavior

class WanderBehavior(Behavior):
    def decide(self, entity, world):
        """Called 4 times per second. Returns (action, goal_str)."""
        player = world.nearest("player", max_dist=5)
        if player:
            # Flee: compute direction away from player, return MoveTo
            dx = entity.x - player.x
            dz = entity.z - player.z
            d = (dx*dx + dz*dz) ** 0.5 or 1
            return MoveTo(entity.x + dx/d * 12, entity.y,
                          entity.z + dz/d * 12,
                          speed=entity.walk_speed * 1.8), "Fleeing!"

        tx, ty, tz = self.wander_target(entity, radius=8)
        return MoveTo(tx, ty, tz, speed=entity.walk_speed), "Wandering"
```

Behaviors are pure Python files stored in the artifact store. Any creature references a behavior by name. Players can edit any behavior from the in-game code editor.

### 4. Action

A discrete event that changes the world. Has `validate()` (read-only check) and `execute()` (mutation).

```python
# Built-in actions:
"use"           # player uses an item (potion, sword, bucket)
"place"         # player places a block or furniture
"break"         # player breaks a block
"attack"        # creature attacks another creature
"interact"      # player interacts with a block/creature (open door, pet dog)

# Custom actions (player-defined):
"cast_fireball" # throw a projectile that explodes
"teleport"      # move to a target location instantly
```

Actions are the ONLY way to change the world. Objects propose actions; the server validates and executes them.

---

## Block Types (basics only)

| Block | Solid | Notes |
|-------|-------|-------|
| `base:dirt` | yes | Can grow grass on top |
| `base:grass` | yes | Dirt + grass top. Spreads to adjacent dirt. Dies when covered. |
| `base:stone` | yes | Basic building material |
| `base:sand` | yes | Falls when unsupported |
| `base:snow` | yes | Light, melts near heat sources |
| `base:water` | no | Flows, transparent, slows movement |
| `base:wood` | yes | Tree trunk |
| `base:leaves` | yes | Transparent, decays when not near wood |
| `base:cobblestone` | yes | Crafted from stone |
| `base:glass` | yes | Transparent, doesn't drop when broken |

No granite, diorite, andesite, etc. Just the basics.

---

## Creature Types

| Creature | Category | Default Behavior | Notes |
|----------|----------|-----------------|-------|
| `base:player` | player | keyboard input / auto-pilot | Human-controlled. Can switch to Python auto-pilot. |
| `base:pig` | animal | `wander` | Flees from players, groups with other pigs |
| `base:chicken` | animal | `peck` | Skittish, pecks at ground, scatters from players |
| `base:dog` | animal | `follow` | Follows nearest player, sits when close |
| `base:cat` | animal | `prowl` | Chases chickens, naps frequently |
| `base:villager` | npc | `woodcutter` | Searches for trees, works, returns home |

### Creature Definition Format

```python
# artifacts/creatures/base/dog.py

creature = {
    "id": "base:dog",
    "name": "Dog",
    "category": "animal",
    "behavior": "follow",           # → artifacts/behaviors/base/follow.py

    "collision": {"min": [-0.3, 0, -0.3], "max": [0.3, 0.7, 0.3]},
    "gravity": 1.0,
    "walk_speed": 4.0,
    "max_hp": 15,

    "model": "dog",                 # → box model definition
    "color": [0.75, 0.55, 0.35],
}
```

---

## Item Types (basics only)

| Item | Category | On Use |
|------|----------|--------|
| `base:sword` | weapon | Attack target creature. Damage = 5. |
| `base:shield` | shield | Block incoming damage while held. |
| `base:potion` | potion | Apply an effect (heal, speed, strength). Consumed on use. |
| `base:bucket` | tool | Pick up water block → becomes water bucket. Place water bucket → creates water block. |
| `base:torch` | placeable | When placed, becomes a torch block (light_emission = 14). |

No iron sword / diamond sword / etc. Just one sword.

### Item Definition Format

```python
# artifacts/items/base/sword.py

item = {
    "id": "base:sword",
    "name": "Sword",
    "category": "weapon",
    "stack_max": 1,
    "cooldown": 0.5,               # seconds between uses

    "on_use": "attack",            # action to perform when used
    "damage": 5,                   # action parameter
    "range": 3.0,                  # reach distance

    "model": "sword",
    "color": [0.7, 0.7, 0.75],
}
```

### Potion Definition

```python
# artifacts/items/base/potion.py

item = {
    "id": "base:potion",
    "name": "Potion",
    "category": "potion",
    "stack_max": 16,
    "consumable": True,             # removed from inventory on use

    # Default effect (can be overridden per instance)
    "effect": "heal",
    "effect_amount": 10,
    "effect_duration": 0,           # 0 = instant

    "model": "potion",
    "color": [0.8, 0.2, 0.3],
}
```

---

## Behavior Available Actions

What a behavior's `decide()` can return (import from `civcraft_engine`):

| Action | Parameters | Description |
|--------|-----------|-------------|
| `Idle()` | — | Stand still |
| `MoveTo(x, y, z, speed)` | position, speed | Walk to position |
| `Convert(from_item, to_item, block_pos, …)` | items, position | Chop/mine/attack |
| `StoreItem(chest_entity_id)` | chest id | Deposit inventory into chest (≤2 blocks) |
| `PickupItem(entity_id)` | item entity id | Pick up a dropped item |
| `DropItem(item_type, count)` | item, count | Drop item at feet |
| `BreakBlock(x, y, z)` | position | Mine a block |
| `Interact(x, y, z)` | position | Open door, press button |

Wandering, following, and fleeing are **not** engine primitives — they are
Python logic in the behavior that computes a target position and returns `MoveTo`.
The `Behavior` base class provides a `wander_target(entity, radius)` helper.

---

## How Everything Connects

**Key principle: ALL intelligence runs on agent clients. The server is a dumb validator.**

A single server may host hundreds of players and NPCs. AI behaviors, pathfinding,
and decision-making are computationally expensive. If the server ran AI for every
creature, it would become the bottleneck. Instead, each NPC gets its own headless
`civcraft-agent` process that runs Python `decide()`, and the server only validates
the resulting intents. The GUI client (`civcraft-ui`) does NOT run Python — it only
renders and forwards player input.

```
┌──────────────────────────────────────────┐
│  ARTIFACT STORE (Python files)           │
│                                          │
│  living/base/pig.py     → behavior ref   │
│  behaviors/base/wander.py → decide()     │
│  items/base/sword.py    → on_use action  │
│  blocks/base/dirt.py    → solid, hardness│
└──────────┬───────────────────────────────┘
           │ loaded by each agent client
           ▼
┌──────────────────────────────────────────┐   ┌──────────────────────────────┐
│  AGENT CLIENT (civcraft-agent)           │   │  GUI CLIENT (civcraft-ui)    │
│  one process per NPC — headless, Python  │   │  C++ + renderer, NO Python   │
│                                          │   │                              │
│  Runs behavior decide() for its entity   │   │  Renders world from server   │
│  Scans local chunk cache for pathfinding │   │  state. Sends player input   │
│  Sends ActionProposals (4 types only:    │   │  (WASD / clicks) as action   │
│    MOVE / RELOCATE / CONVERT / INTERACT) │   │  proposals. Click-to-move    │
│                                          │   │  navigation is handled       │
│                                          │   │  server-side (greedy steer). │
└──────────┬───────────────────────────────┘   └──────────────┬───────────────┘
           │ ActionProposals                                  │ ActionProposals
           ▼                                                  ▼
          ┌─────────────────────────────────────────────────────┐
          │  SERVER (C++ engine — NO AI, NO Python)             │
          │                                                      │
          │  Receives intents from all clients                   │
          │  Validates: in range? path clear? value conserved?   │
          │  Executes: physics, chunk mutation                   │
          │  Broadcasts state updates to all clients             │
          │  NEVER runs Python behavior code                     │
          └─────────────────────────────────────────────────────┘
```

**Scaling:** Adding 100 NPCs = spawning 100 headless agent processes. The server
CPU stays constant — it only validates. See `00_OVERVIEW.md` for the full
process-model rationale.

---

## Customization Flow

1. Player sees a pig → right-clicks → Inspection Panel shows stats + behavior code
2. Player presses E → Code Editor opens with `wander.py`
3. Player edits the behavior (e.g., makes the pig aggressive instead of fleeing)
4. Player presses Ctrl+Enter → code uploaded to DB layer (planned)
5. Server validates (sandbox) → loads the new behavior → pig starts attacking players
6. Other players see the pig's new behavior in the shared world

Players can customize:
- **Behaviors** — what creatures do (wander, follow, attack, build)
- **Creatures** — stats, speed, HP, model, which behavior they use
- **Items** — what happens on use, damage, effects, cooldowns
- **Blocks** — hardness, transparency, what they drop, active behavior (like TNT)
- **Actions** — entirely new actions (teleport, fireball, gravity gun)

---

## Artifact Store Structure

```
artifacts/
  blocks/
    base/               ← built-in (dirt, stone, sand, water, ...)
  creatures/
    base/               ← built-in (pig, chicken, dog, villager)
  behaviors/
    base/               ← built-in (wander, peck, follow, prowl, woodcutter)
  items/
    base/               ← built-in (sword, shield, potion, bucket, torch)
  actions/
    base/               ← built-in (attack, place, break, use)
```

Everything in `base/` is read-only (ships with the game). User-created or
user-modified artifacts are served by a DB layer (not yet implemented) —
there is no on-disk `player/` tier.
