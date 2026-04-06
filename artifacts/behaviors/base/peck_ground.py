"""Peck Ground — bird pecking at seeds on the ground.

Random chance to stop and peck. Simple idle animation trigger.
"""
from modcraft_engine import Idle
import random

_rng_seeded = False

def decide(self, world):
    global _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 2903 + 7)
        _rng_seeded = True

    if random.random() < 0.30:
        self["goal"] = "Pecking at seeds"
        return Idle()
    return None
