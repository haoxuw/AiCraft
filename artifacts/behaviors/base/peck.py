"""Peck — timid chicken that scatters from threats and lays eggs.

Chickens flee from players AND cats. When startled, they have
a 20% chance to drop an egg. The behavior controls everything:
when to lay, what item, how many. The server only validates.
"""
from agentworld_engine import Idle, Wander, Flee, DropItem
import random

SCATTER_DISTANCE = 4.0
PECK_CHANCE = 0.45
EGG_CHANCE = 0.20         # 20% chance to lay egg when startled
EGG_COOLDOWN = 10.0       # minimum seconds between egg drops

_was_startled = False
_egg_cooldown = 0

def decide(self, world):
    global _was_startled, _egg_cooldown
    _egg_cooldown -= world["dt"]

    # Flee from players and cats
    threats = [e for e in world["nearby"]
               if (e["category"] == "player" or e["type_id"] == "base:cat")
               and e["distance"] < SCATTER_DISTANCE]

    if threats:
        closest = min(threats, key=lambda e: e["distance"])

        # First moment of being startled — chance to lay egg
        if not _was_startled and _egg_cooldown <= 0:
            _was_startled = True
            if random.random() < EGG_CHANCE:
                _egg_cooldown = EGG_COOLDOWN
                self["goal"] = "BAWK!! *lays egg!*"
                return DropItem("base:egg", 1)

        self["goal"] = "BAWK!! Scattering!"
        return Flee(closest["id"], speed=6.0)

    _was_startled = False

    # Peck at ground or waddle around
    if random.random() < PECK_CHANCE:
        self["goal"] = "Pecking at seeds"
        return Idle()

    self["goal"] = "Scratching ground"
    return Wander(speed=1.8)
