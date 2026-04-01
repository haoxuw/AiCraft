"""Wander - default animal behavior.

Animals roam randomly, flee from nearby players,
and try to stay close to others of the same species.

This is the first code players see when they inspect a pig.
It's designed to be readable and easy to modify!

Try changing:
  - FLEE_DISTANCE to make animals braver or more cowardly
  - GROUP_DISTANCE to change how tightly they bunch up
  - Remove the flee section entirely to make fearless pigs
"""

import random
from agentworld.api import Wander, Flee, MoveTo, Idle

# How close a player must be before the animal runs
FLEE_DISTANCE = 5.0

# How far apart friends can be before regrouping
GROUP_DISTANCE = 6.0

goal = "Wandering"


def decide(self, world):
    """Called 4 times per second. Return what to do next."""

    # Step 1: Check for danger
    # Look for players within FLEE_DISTANCE blocks
    players = world.get_entities_in_radius(
        self.pos, FLEE_DISTANCE, category="player"
    )
    if players:
        closest = min(players, key=lambda e: e.distance)
        self.goal = "Fleeing!"
        return Flee(closest.id, speed=self.walk_speed * 1.8)

    # Step 2: Stay with friends
    # Find others of the same species nearby
    friends = world.get_entities_in_radius(
        self.pos, 12.0, type=self.type_id
    )
    friends = [f for f in friends if f.id != self.id]

    if friends and friends[0].distance > GROUP_DISTANCE:
        self.goal = "Joining friends"
        return MoveTo(friends[0].pos, speed=self.walk_speed)

    # Step 3: Wander or rest
    if random.random() < 0.3:
        self.goal = "Resting"
        return Idle(duration=1.0 + random.random() * 2.0)

    self.goal = "Wandering"
    return Wander(speed=self.walk_speed)
