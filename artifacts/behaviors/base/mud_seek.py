"""Mud Seek — pig-specific trait to wallow in water/mud.

Pigs occasionally seek out water blocks to wallow in.
Compose with wander for a complete pig behavior.
"""
from agentica_engine import Idle, MoveTo
import random

_mud_timer = 0
_rng_seeded = False

def decide(self, world):
    global _mud_timer, _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 6151 + 13)
        _rng_seeded = True
    dt = world["dt"]
    _mud_timer -= dt

    if _mud_timer > 0:
        self["goal"] = "Wallowing in mud"
        return Idle()

    if random.random() < 0.08:
        water_blocks = [b for b in world["blocks"]
                        if b["type"] == "base:water" and b["distance"] < 15]
        if water_blocks:
            nearest = min(water_blocks, key=lambda b: b["distance"])
            if nearest["distance"] > 2:
                _mud_timer = 6.0
                self["goal"] = "Heading to mud"
                return MoveTo(nearest["x"] + 0.5, nearest["y"] + 1,
                              nearest["z"] + 0.5, speed=self["walk_speed"] * 0.8)
            else:
                _mud_timer = 4.0 + random.random() * 3.0
                self["goal"] = "Wallowing in mud"
                return Idle()

    return None  # let main behavior handle
