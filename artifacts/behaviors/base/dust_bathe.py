"""Dust Bathe — birds roll in dirt to clean feathers.

Random chance to start a dust bath, then idle for a few seconds.
"""
from agentica_engine import Idle
import random

_rng_seeded = False

def decide(self, world):
    global _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 3571 + 19)
        _rng_seeded = True

    timer = self.get("_dust_timer", 0) - world["dt"]
    self["_dust_timer"] = timer

    if timer > 0:
        self["goal"] = "Dust bathing"
        return Idle()

    if random.random() < 0.05:
        self["_dust_timer"] = 3.0 + random.random() * 3.0
        self["goal"] = "Dust bathing"
        return Idle()

    return None
