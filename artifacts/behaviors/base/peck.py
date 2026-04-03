"""Peck — timid chicken that scatters from threats.

Chickens flee from players AND cats. When startled they
scatter with panicked clucking. Otherwise they peck at
the ground and waddle around looking for seeds.

The server handles egg-laying automatically when chickens
are startled (20% chance per scare event).
"""
from agentworld_engine import Idle, Wander, Flee
import random

SCATTER_DISTANCE = 4.0
PECK_CHANCE = 0.45
_startled = False

def decide(self, world):
    global _startled

    # Flee from players and cats
    threats = [e for e in world["nearby"]
               if (e["category"] == "player" or e["type_id"] == "base:cat")
               and e["distance"] < SCATTER_DISTANCE]

    if threats:
        closest = min(threats, key=lambda e: e["distance"])
        if not _startled:
            _startled = True
            # Signal server for possible egg drop
            self["goal"] = "BAWK!! *startled*"
        else:
            self["goal"] = "BAWK!! Running!"
        return Flee(closest["id"], speed=6.0)

    _startled = False

    # Peck at ground or waddle around
    if random.random() < PECK_CHANCE:
        self["goal"] = "Pecking at seeds"
        return Idle()

    self["goal"] = "Scratching ground"
    return Wander(speed=1.8)
