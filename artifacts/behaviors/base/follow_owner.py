"""Follow Owner — companion behavior, follows nearest player or villager.

Sits nearby when close, does idle animations, follows when far.

Parameters: follow_dist (default 3)
"""
from modcraft_engine import Idle, Follow
import random

_rng_seeded = False

def decide(self, world):
    global _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 5701 + 53)
        _rng_seeded = True

    follow_dist = self.get("follow_dist", 3.0)
    play_timer = self.get("_play_timer", 0) - world["dt"]
    self["_play_timer"] = play_timer

    # Find owner
    players = [e for e in world["nearby"] if e["category"] == "player"]
    villagers = [e for e in world["nearby"] if e["type_id"] == "base:villager"]
    owner = None
    if players:
        owner = min(players, key=lambda e: e["distance"])
    elif villagers:
        owner = min(villagers, key=lambda e: e["distance"])

    if not owner:
        return None  # no owner — let patrol_home handle it

    name = owner["type_id"].split(":")[1]

    # Close enough — sit or play
    if owner["distance"] < follow_dist:
        if play_timer <= 0 and random.random() < 0.08:
            self["_play_timer"] = 10.0
            r = random.random()
            if r < 0.4:
                self["goal"] = "*play bow!*"
            elif r < 0.7:
                self["goal"] = "Tail wagging"
            else:
                self["goal"] = "Panting happily"
            return Idle()
        self["goal"] = "Sitting by %s" % name
        return Idle()

    # Follow
    self["goal"] = "Following %s" % name
    return Follow(owner["id"], speed=4.0, min_distance=follow_dist)
