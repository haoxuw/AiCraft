"""Wander — herd animal that grazes, flees threats, and goes home at night.

Priority order:
  1. Evening/night — head home and sleep
  2. Too far from home — wander back
  3. Flee from players / cats
  4. Join herd if drifted too far
  5. Graze occasionally
  6. Wander

Entity props (optional):
  flee_range   — distance to flee from players/cats (default 5)
  group_range  — max distance before rejoining herd (default 6)
  graze_chance — probability of stopping to graze per decide (default 0.25)
  home_radius  — max wander distance from spawn before returning (default 35)
"""
import random
from modcraft_engine import Move
from behavior_base import Behavior
from stats import stats


class WanderBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._graze_timer = 0.0
        self._sleeping = False
        self._resting = False
        self._rng_seeded = False

    def decide(self, entity: "SelfEntity", local_world: "LocalWorld"):
        stats.inc("decide", entity.type)
        if not self._rng_seeded:
            random.seed(entity.id * 31337 + 42)
            self._rng_seeded = True

        self._graze_timer -= local_world.dt
        self._home = self.init_home(entity, self._home)

        flee_range   = float(entity.get("flee_range",   5.0))
        group_range  = float(entity.get("group_range",  6.0))
        graze_chance = float(entity.get("graze_chance", 0.25))
        home_radius  = float(entity.get("home_radius",  35.0))
        spd          = entity.walk_speed

        dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])

        # ── Evening/Night: go home ──────────────────────────────────────────
        if self.is_night(local_world) or self.is_evening(local_world):
            self._resting = True
            if self.is_night(local_world):
                self._sleeping = True
            if dist_home > 3:
                return Move(*self._home, speed=spd), "Heading home..."
            if self._sleeping:
                return Move(entity.x, entity.y, entity.z), "Sleeping zzz", 3.0
            return Move(entity.x, entity.y, entity.z), "Settling in for the night", 2.0

        if self._resting:
            self._resting = False
            self._sleeping = False
            return Move(entity.x, entity.y, entity.z), "Good morning!"

        if dist_home > home_radius:
            return Move(*self._home, speed=spd * 0.8), "Wandering back home"

        # ── Flee from threats (any Living that isn't my species) ────────────
        threats = [e for e in local_world.entities
                   if e.distance <= flee_range
                   and e.kind == "living"
                   and e.type != entity.type]
        if threats:
            closest = min(threats, key=lambda t: t.distance)
            stats.inc("flee", entity.type)
            return Move(*self.flee_pos(entity, closest), speed=spd * 1.8), "Fleeing!"

        # ── Stay with herd ──────────────────────────────────────────────────
        friends = local_world.all(entity.type)
        if friends:
            nearest = min(friends, key=lambda e: e.distance)
            if nearest.distance > group_range:
                return Move(nearest.x, nearest.y, nearest.z, speed=spd), \
                       "Joining herd"

        # ── Graze ───────────────────────────────────────────────────────────
        if self._graze_timer <= 0 and random.random() < graze_chance:
            self._graze_timer = 3.0 + random.random() * 4.0
            return Move(entity.x, entity.y, entity.z), "Grazing", self._graze_timer

        if self._graze_timer > 0:
            return Move(entity.x, entity.y, entity.z), "Grazing", self._graze_timer

        # Plan is immutable between re-decides (event-driven loop), so the
        # wander target is automatically "sticky" until arrival/interrupt.
        tx, ty, tz = self.wander_target(entity, radius=12)
        return Move(tx, ty, tz, speed=spd), "Wandering"
