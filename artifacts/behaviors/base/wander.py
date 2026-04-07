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
from modcraft_engine import Idle, Wander, Flee, MoveTo
from behavior_base import Behavior


class WanderBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._graze_timer = 0.0
        self._sleeping = False
        self._resting = False   # True during evening/night; cleared at dawn
        self._rng_seeded = False

    def decide(self, entity, world):
        if not self._rng_seeded:
            random.seed(entity["id"] * 31337 + 42)
            self._rng_seeded = True

        self._graze_timer -= world["dt"]
        self._home = self.init_home(entity, self._home)

        flee_range   = float(entity.get("flee_range", 5.0))
        group_range  = float(entity.get("group_range", 6.0))
        graze_chance = float(entity.get("graze_chance", 0.25))
        home_radius  = float(entity.get("home_radius", 35.0))
        spd          = entity["walk_speed"]

        dist_home = self.dist2d(entity["x"], entity["z"],
                                self._home[0], self._home[2])

        # ── Evening/Night: go home ──────────────────────────────────────────
        if self.is_night(world) or self.is_evening(world):
            self._resting = True
            if self.is_night(world):
                self._sleeping = True
            if dist_home > 3:
                return (MoveTo(self._home[0], self._home[1], self._home[2],
                               speed=spd),
                        "Heading home...")
            if self._sleeping:
                return Idle(), "Sleeping zzz"
            return Idle(), "Settling in for the night"

        if self._resting:
            self._resting = False
            self._sleeping = False
            return Idle(), "Good morning!"

        if dist_home > home_radius:
            return (MoveTo(self._home[0], self._home[1], self._home[2],
                           speed=spd * 0.8),
                    "Wandering back home")

        # ── Flee from threats ───────────────────────────────────────────────
        threats = [e for e in world["nearby"]
                   if (e["category"] == "player" or e["type_id"] == "base:cat")
                   and e["distance"] < flee_range]
        if threats:
            closest = min(threats, key=lambda e: e["distance"])
            return Flee(closest["id"], speed=spd * 1.8), "Fleeing!"

        # ── Stay with herd ──────────────────────────────────────────────────
        friends = [e for e in world["nearby"]
                   if e["type_id"] == entity["type_id"] and e["id"] != entity["id"]]
        if friends:
            farthest = max(friends, key=lambda e: e["distance"])
            if farthest["distance"] > group_range:
                return (MoveTo(farthest["x"], farthest["y"], farthest["z"],
                               speed=spd),
                        "Joining herd")

        # ── Graze ───────────────────────────────────────────────────────────
        if self._graze_timer <= 0 and random.random() < graze_chance:
            self._graze_timer = 3.0 + random.random() * 4.0
            return Idle(), "Grazing"

        if self._graze_timer > 0:
            return Idle(), "Grazing"

        return Wander(speed=spd), "Wandering"
