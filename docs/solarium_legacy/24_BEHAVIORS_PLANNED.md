# Behavior System: Composable Rule-Based AI

> **DESIGN SKETCH — NOT IMPLEMENTED.**
> This document describes a planned composable rule-list system where each
> creature has multiple behavior files (one per rule). The current implementation
> uses a single behavior file per creature. The Python API here (dict-style `self`,
> `Follow`/`Flee` imports) is also outdated — see `docs/21_BEHAVIOR_API.md` for
> the actual API.

## Overview

Every creature's AI is an **ordered list of rules**. Each rule is a tiny Python function:
`condition → action` or `None` (pass to next rule). First non-None wins.

```
IF threat_nearby      THEN flee
ELIF egg_ready        THEN drop_egg
ELIF dusk             THEN seek_roost
ELIF far_from_flock   THEN rejoin
ELSE                       wander
```

There is NO distinction between "behaviors" and "traits". Everything is a **rule**.
A creature is defined by its **rule list** — an ordered composition of small, reusable rules.

## Rule Contract

Every rule is a Python file in `artifacts/behaviors/base/` with:

```python
def decide(self, world):
    """Return an action, or None to pass to the next rule."""
    if some_condition(self, world):
        return SomeAction(...)
    return None  # pass — let the next rule decide
```

- `self` — entity state dict (position, hp, properties, persistent state)
- `world` — nearby entities, blocks, time
- Returns: `Idle()`, `Wander()`, `Flee()`, `MoveTo()`, `Follow()`, `DropItem()`, or `None`
- `None` means "I don't want to act" → the next rule gets a chance

## Creature Definition

Creatures define an ordered list of rule names:

```python
# Hen — flees, lays eggs when startled, roosts at dusk, flocks, pecks
creature = {
    "id": "base:chicken",
    "behaviors": ["flee_threats", "lay_egg", "roost", "flock", "dust_bathe", "peck_ground", "wander_slow"],
}

# Rooster — same as hen but no egg-laying
creature = {
    "id": "base:rooster",
    "behaviors": ["flee_threats", "roost", "flock", "dust_bathe", "peck_ground", "wander_slow"],
}

# Guard dog — guards first, then follows owner, then patrols
creature = {
    "id": "base:dog",
    "behaviors": ["guard", "follow_owner", "patrol_home"],
}

# Stray dog — just wanders, no guarding or following
creature = {
    "id": "base:stray_dog",
    "behaviors": ["flee_threats", "wander_slow"],
}
```

**Order matters.** Rules at the top have higher priority. "flee_threats" should almost
always be first — survival overrides everything.

## Standard Rule Library

### Survival (high priority)
| Rule | Condition | Action |
|------|-----------|--------|
| `flee_threats` | player/cat within flee_range | `Flee(threat)` |
| `herd_stampede` | friend is near a threat | `Flee(threat)` |

### Species Traits (medium priority)
| Rule | Condition | Action |
|------|-----------|--------|
| `lay_egg` | startled + cooldown ready + random chance | `DropItem("base:egg", 1)` |
| `roost` | dusk + perch available | `MoveTo(perch)` / `Idle()` |
| `mud_seek` | random chance + water nearby | `MoveTo(water)` / `Idle()` |
| `guard` | cat near owner | `Follow(cat)` / bark |

### Social (medium priority)
| Rule | Condition | Action |
|------|-----------|--------|
| `flock` | far from same-type friends | `MoveTo(friend)` |
| `herd_stick` | far from herd | `MoveTo(farthest_friend)` |
| `follow_owner` | owner found + far away | `Follow(owner)` |

### Idle Activities (low priority)
| Rule | Condition | Action |
|------|-----------|--------|
| `dust_bathe` | random chance | `Idle()` |
| `graze` | random chance + timer | `Idle()` |
| `peck_ground` | random chance | `Idle()` |
| `patrol_home` | no owner found | `MoveTo(random near home)` / `Wander()` |

### Terminal (always fires, placed last)
| Rule | Condition | Action |
|------|-----------|--------|
| `wander_slow` | always | `Wander(speed=walk_speed)` |
| `idle` | always | `Idle()` |

## Creature → Rule Compositions

| Creature | Rules (priority order) |
|----------|----------------------|
| **Chicken (hen)** | flee_threats, lay_egg, roost, flock, dust_bathe, peck_ground, wander_slow |
| **Chicken (rooster)** | flee_threats, roost, flock, dust_bathe, peck_ground, wander_slow |
| **Pig** | flee_threats, herd_stampede, mud_seek, herd_stick, graze, wander_slow |
| **Dog (guard)** | guard, follow_owner, patrol_home |
| **Dog (stray)** | flee_threats, wander_slow |
| **Cat** | prowl (monolithic for now — decompose later) |
| **Villager** | woodcutter (monolithic for now — decompose later) |
| **Robot chicken** | peck_ground, wander_slow (no flee, no eggs, no roost) |

## Per-Entity Randomness

Each rule seeds `random.seed(self["id"] * PRIME + OFFSET)` on first call.
Different primes per rule ensure different entities make different decisions,
even if they run the same rule at the same time.

## State Management

Rules store per-entity state in `self["_key"]` (underscore prefix = internal).
Examples:
- `self["_egg_cooldown"]` — seconds until next egg
- `self["_was_startled"]` — flag set by flee_threats, read by lay_egg
- `self["_graze_timer"]` — grazing duration countdown

The `self` dict persists across ticks for the same entity. Rules in the same
tick see each other's writes (since they share the same `self` dict).

Module-level globals (`_timer = 0`) also work but are per-rule-handle
(not shared between rules). Use `self["_key"]` for cross-rule communication.

## Execution Model

The bot client process evaluates rules for each entity at 4 Hz:

```
for entity in controlled_entities:
    for rule in entity.rules:
        action = rule.decide(self_dict, world_dict)
        if action is not None:
            convert_to_action_proposal(action)
            break
    else:
        action = Idle()  # no rule fired
```

## Implementation Notes

### C++ Side (agent_client.h)
- `BehaviorRules` entity property: comma-separated rule names
- Falls back to `BehaviorId` (single behavior) for backward compatibility
- Each rule is a separate `PythonBehavior` handle
- `BehaviorAction::None` type added to distinguish "pass" from "idle"

### Python Side (artifacts/behaviors/base/)
- Each rule is 10-30 lines
- Returns `None` to pass, or an action object to act
- Parameters read from `self.get("key", default)` — creature artifact can override
- `from solarium_engine import Idle, Wander, Flee, MoveTo, Follow, DropItem`

### Network Protocol
- No changes needed — bot client sends the same `C_ACTION` messages
- `S_RELOAD_BEHAVIOR` can target individual rules (future)

### Player Override
- Players can view and fork any rule from the in-game editor
- Forked rules are persisted by the DB layer (not yet implemented) —
  there is no on-disk `player/` tier
- Player rules override base rules of the same name at load time

## Future Extensions

- **Utility AI**: rules return a score (0-1) instead of bool, highest score wins
- **Behavior trees**: rules can contain sub-rules (sequence, selector, parallel)
- **Learning**: rules can be trained from player demonstrations
- **Market**: players upload/share rule compositions as creature "personalities"
