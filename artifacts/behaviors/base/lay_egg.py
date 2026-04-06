"""Lay Egg — egg-laying trait for hens.

Drops an egg when startled by a threat. Only triggers once per
startle event, with a long cooldown between eggs.
Compose with peck or wander for a complete hen behavior.

Parameters (optional via self dict):
  egg_chance    — probability per startle (default 0.10)
  egg_cooldown  — seconds between possible eggs (default 60)
  scatter_range — threat detection distance (default 4)
"""
from modcraft_engine import DropItem
import random

EGG_COOLDOWN = 60.0

_was_startled = False
_egg_cooldown = 0
_rng_seeded = False

def decide(self, world):
    global _was_startled, _egg_cooldown, _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 7919 + 101)
        _rng_seeded = True
    dt = world["dt"]
    _egg_cooldown -= dt

    egg_chance = self.get("egg_chance", 0.10)
    scatter_range = self.get("scatter_range", 4.0)

    threats = [e for e in world["nearby"]
               if (e["category"] == "player" or e["type_id"] == "base:cat")
               and e["distance"] < scatter_range]

    if threats:
        if not _was_startled and _egg_cooldown <= 0:
            _was_startled = True
            if random.random() < egg_chance:
                _egg_cooldown = EGG_COOLDOWN
                self["goal"] = "BAWK!! *lays egg!*"
                return DropItem("base:egg", 1)
    else:
        _was_startled = False

    return None  # no action — let the main behavior handle movement
