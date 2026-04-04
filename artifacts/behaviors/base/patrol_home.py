"""Patrol Home — wander near spawn point when no owner found.

Terminal rule for companion creatures. Always returns an action.

Parameters: patrol_range (default 12)
"""
from agentica_engine import Wander, MoveTo
import random

_rng_seeded = False

def decide(self, world):
    global _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 6337 + 67)
        _rng_seeded = True

    patrol_range = self.get("patrol_range", 12.0)
    home = self.get("_home", None)
    if home is None:
        self["_home"] = [self["x"], self["y"], self["z"]]
        home = self["_home"]

    timer = self.get("_patrol_timer", 0) - world["dt"]
    self["_patrol_timer"] = timer

    if timer <= 0:
        self["_patrol_timer"] = 5.0 + random.random() * 5.0
        px = home[0] + (random.random() - 0.5) * patrol_range * 2
        pz = home[2] + (random.random() - 0.5) * patrol_range * 2
        self["goal"] = "Patrolling"
        return MoveTo(px, home[1], pz, speed=self["walk_speed"] * 0.6)

    self["goal"] = "Sniffing around"
    return Wander(speed=self["walk_speed"] * 0.5)
