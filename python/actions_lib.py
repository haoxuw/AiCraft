"""actions_lib.py — Reusable actions for rule-based behaviors.

Each action's `run(entity, world, ctx)` returns either:
  - (action_obj, goal_str[, duration])  — a normal decide-return, or
  - None                                 — declines (e.g. cooldown not ready),
                                            letting RulesBehavior try the
                                            next matching rule.

Actions with internal state (cooldowns, timers) implement `tick(dt)`.
"""
import math
import random
from modcraft_engine import Move, Convert, Ground
from rules import Action, init_home


# ── Basic movement ───────────────────────────────────────────────────────

class Idle(Action):
    def __init__(self, message="Idle"):
        self.message = message
    def run(self, e, w, ctx):
        return Move(e.x, e.y, e.z), self.message


class Wander(Action):
    """Random walk within `radius`. Plan is sticky between decides, so the
    target doesn't flicker — a new random point is only picked when the
    previous move completes or is interrupted."""
    def __init__(self, radius=8.0, speed_mul=1.0, message="Wandering"):
        self.radius = radius
        self.speed_mul = speed_mul
        self.message = message
    def run(self, e, w, ctx):
        angle = random.uniform(0, 2 * math.pi)
        d = random.uniform(self.radius * 0.3, self.radius)
        return (Move(e.x + math.cos(angle) * d, e.y, e.z + math.sin(angle) * d,
                     speed=e.walk_speed * self.speed_mul),
                self.message)


class Flee(Action):
    """Move away from the nearest threat within `range`. If `types` is given,
    only those type ids count as threats; otherwise any non-same-species
    Living is a threat."""
    def __init__(self, range=5.0, types=None, distance=12.0,
                 speed_mul=1.8, message="Fleeing!"):
        self.range = range
        self.types = set(types) if types else None
        self.distance = distance
        self.speed_mul = speed_mul
        self.message = message
    def run(self, e, w, ctx):
        threat = None
        for x in w.entities:
            if x.distance > self.range:
                continue
            matches = (self.types is None and x.kind == "living" and x.type != e.type) \
                      or (self.types is not None and x.type in self.types)
            if matches and (threat is None or x.distance < threat.distance):
                threat = x
        if threat is None:
            return None
        dx = e.x - threat.x
        dz = e.z - threat.z
        d = (dx * dx + dz * dz) ** 0.5 or 1.0
        tx = e.x + dx / d * self.distance
        tz = e.z + dz / d * self.distance
        return (Move(tx, e.y, tz, speed=e.walk_speed * self.speed_mul),
                self.message)


class Follow(Action):
    """Trail a target entity matched by tag or type. Stands still once within
    `close_dist`, walks toward it otherwise."""
    def __init__(self, target_tag=None, target_type=None,
                 close_dist=3.0, range=20.0, speed_mul=1.0,
                 at_target_msg=None, following_msg=None):
        self.target_tag = target_tag
        self.target_type = target_type
        self.close_dist = close_dist
        self.range = range
        self.speed_mul = speed_mul
        self.at_target_msg = at_target_msg
        self.following_msg = following_msg
    def _find(self, e, w):
        if self.target_type:
            return w.get(self.target_type, max_dist=self.range)
        best = None
        for x in w.entities:
            if self.target_tag and not x.has_tag(self.target_tag):
                continue
            if x.distance > self.range or x.id == e.id:
                continue
            if best is None or x.distance < best.distance:
                best = x
        return best
    def run(self, e, w, ctx):
        t = self._find(e, w)
        if t is None:
            return None
        name = t.type.split(":")[-1] if ":" in t.type else t.type
        if t.distance <= self.close_dist:
            return (Move(e.x, e.y, e.z),
                    self.at_target_msg or f"With {name}")
        return (Move(t.x, t.y, t.z, speed=e.walk_speed * self.speed_mul),
                self.following_msg or f"Following {name}")


class Rejoin(Action):
    """Walk toward the nearest same-species entity. Declines if no flockmates
    or if already within `close_dist`."""
    def __init__(self, close_dist=4.0, speed_mul=1.0, message="Rejoining flock"):
        self.close_dist = close_dist
        self.speed_mul = speed_mul
        self.message = message
    def run(self, e, w, ctx):
        friends = w.all(e.type)
        if not friends:
            return None
        nearest = min(friends, key=lambda f: f.distance)
        if nearest.distance <= self.close_dist:
            return None
        return (Move(nearest.x, nearest.y, nearest.z,
                     speed=e.walk_speed * self.speed_mul),
                self.message)


# ── Home / rest ──────────────────────────────────────────────────────────

class GoHome(Action):
    """Walk back to the entity's home. Declines if already within `close`."""
    def __init__(self, speed_mul=1.0, close=3.0, message="Heading home"):
        self.speed_mul = speed_mul
        self.close = close
        self.message = message
    def run(self, e, w, ctx):
        home = init_home(e, ctx)
        dx = e.x - home[0]; dz = e.z - home[2]
        if dx * dx + dz * dz < self.close * self.close:
            return None
        return (Move(*home, speed=e.walk_speed * self.speed_mul),
                self.message)


class Rest(Action):
    """Stand still at the current position. Use for roosting, sleeping,
    grazing, napping — whatever the flavor text calls for."""
    def __init__(self, message="Resting", duration=None):
        self.message = message
        self.duration = duration
    def run(self, e, w, ctx):
        if self.duration is not None:
            return Move(e.x, e.y, e.z), self.message, self.duration
        return Move(e.x, e.y, e.z), self.message


# ── Special ──────────────────────────────────────────────────────────────

class LayEgg(Action):
    """Convert HP into a dropped item on the ground. Cooldown-gated and
    chance-gated; declines when not ready so the next rule fires instead.
    Perfect for "flee + drop egg" — put LayEgg above Flee in the rule list."""
    def __init__(self, chance=1.0, cooldown=8.0, hp_cost=2,
                 item="base:egg", message="Laid an egg!"):
        self.chance = chance
        self.cooldown = cooldown
        self.hp_cost = hp_cost
        self.item = item
        self.message = message
        self._cd = 0.0
    def tick(self, dt):
        self._cd = max(0.0, self._cd - dt)
    def run(self, e, w, ctx):
        if self._cd > 0 or e.hp <= self.hp_cost:
            return None
        if random.random() > self.chance:
            return None
        self._cd = self.cooldown
        return (Convert(from_item="hp", from_count=self.hp_cost,
                        to_item=self.item, to_count=1,
                        convert_into=Ground()),
                self.message)


class SeekPerch(Action):
    """Climb to the highest elevated block of a given type nearby. Declines
    if nothing suitable is found. Used by cats, chickens, etc."""
    def __init__(self, block_types=("base:wood", "base:planks", "base:fence"),
                 min_height_above=1.5, range=8.0, message="Seeking perch"):
        self.block_types = block_types
        self.min_height_above = min_height_above
        self.range = range
        self.message = message
    def run(self, e, w, ctx):
        best = None
        for bt in self.block_types:
            for b in w.all(bt):
                if b.distance > self.range: continue
                if b.y <= e.y + self.min_height_above: continue
                if best is None or b.y > best.y:
                    best = b
        if best is None:
            return None
        return (Move(best.x + 0.5, best.y + 1.0, best.z + 0.5,
                     speed=e.walk_speed),
                self.message)
