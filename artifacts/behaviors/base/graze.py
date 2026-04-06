"""Graze — stop and eat grass.

Random chance to start grazing, then idle for a few seconds.

Parameters: graze_chance (default 0.25)
"""
from modcraft_engine import Idle
import random

_rng_seeded = False

def decide(self, world):
    global _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 4217 + 31)
        _rng_seeded = True

    timer = self.get("_graze_timer", 0) - world["dt"]
    self["_graze_timer"] = timer

    if timer > 0:
        self["goal"] = "Grazing"
        return Idle()

    graze_chance = self.get("graze_chance", 0.25)
    if random.random() < graze_chance:
        self["_graze_timer"] = 3.0 + random.random() * 4.0
        self["goal"] = "Grazing"
        return Idle()

    return None
