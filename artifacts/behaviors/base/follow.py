"""Follow — loyal companion that follows the nearest player or villager.

Follows the nearest player first. If no player is nearby,
follows the nearest villager. Sits when close to their target.
"""

from agentworld_engine import Idle, Wander, Follow

FOLLOW_DISTANCE = 3.0
FOLLOW_SPEED = 4.0

goal = "Looking for someone"


def decide(self, world):
    """Called 4 times per second. Return what to do next."""

    # Find nearest player
    players = [e for e in world["nearby"] if e.category == "player"]
    villagers = [e for e in world["nearby"] if e.type_id == "base:villager"]

    target = None
    if players:
        target = min(players, key=lambda e: e.distance)
    elif villagers:
        target = min(villagers, key=lambda e: e.distance)

    if not target:
        self["goal"] = "Looking for someone"
        return Wander(speed=self["walk_speed"] * 0.5)

    # Close enough — sit
    if target.distance < FOLLOW_DISTANCE:
        name = target.type_id.split(":")[1]
        self["goal"] = f"Sitting by {name}"
        return Idle()

    # Follow them
    name = target.type_id.split(":")[1]
    self["goal"] = f"Following {name} ({target.distance:.0f}m)"
    return Follow(target.id, speed=FOLLOW_SPEED, min_distance=FOLLOW_DISTANCE)
