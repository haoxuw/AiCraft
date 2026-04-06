"""Peck - chicken behavior.

Chickens are skittish and scatter quickly when players approach.
They alternate between short walks and pecking at the ground.

Try changing:
  - SCATTER_DISTANCE to make chickens braver
  - PECK_CHANCE to change how often they stop to peck
  - Add an egg-laying timer!
"""

import random
from modcraft.api import Wander, Flee, Idle

SCATTER_DISTANCE = 3.5
PECK_CHANCE = 0.5

goal = "Pecking at ground"


def decide(self, world):
    """Called 4 times per second. Return what to do next."""

    # Chickens scatter from players easily
    players = world.get_entities_in_radius(
        self.pos, SCATTER_DISTANCE, category="player"
    )
    if players:
        closest = min(players, key=lambda e: e.distance)
        self.goal = "Scattering!"
        return Flee(closest.id, speed=self.walk_speed * 2.0)

    # Peck or walk
    if random.random() < PECK_CHANCE:
        self.goal = "Looking around"
        return Idle(duration=0.3 + random.random() * 0.8)

    self.goal = "Pecking at ground"
    return Wander(speed=self.walk_speed * 0.7)
