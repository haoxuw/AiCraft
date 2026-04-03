"""Follow — loyal companion that follows and guards its owner.

Dogs follow the nearest player. If no player, they follow
villagers. They sit when close, play-bow when happy, and
growl at cats that get too close to their person.
"""
from agentworld_engine import Idle, Wander, Follow, Flee
import random

FOLLOW_DISTANCE = 3.0
GUARD_DISTANCE = 6.0
_play_timer = 0

def decide(self, world):
    global _play_timer
    _play_timer -= world["dt"]

    # Find someone to follow
    players = [e for e in world["nearby"] if e["category"] == "player"]
    villagers = [e for e in world["nearby"] if e["type_id"] == "base:villager"]

    owner = None
    if players:
        owner = min(players, key=lambda e: e["distance"])
    elif villagers:
        owner = min(villagers, key=lambda e: e["distance"])

    if not owner:
        self["goal"] = "Sniffing around"
        return Wander(speed=self["walk_speed"] * 0.5)

    name = owner["type_id"].split(":")[1]

    # Chase away cats near owner (guard behavior)
    if owner["distance"] < GUARD_DISTANCE:
        cats = [e for e in world["nearby"]
                if e["type_id"] == "base:cat" and e["distance"] < 5]
        if cats:
            cat = min(cats, key=lambda e: e["distance"])
            self["goal"] = "Chasing cat away!"
            return Follow(cat["id"], speed=self["walk_speed"] * 1.5, min_distance=1)

    # Close enough — sit or play
    if owner["distance"] < FOLLOW_DISTANCE:
        if _play_timer <= 0 and random.random() < 0.1:
            _play_timer = 8.0
            self["goal"] = "*play bow!*"
            return Idle()
        self["goal"] = "Sitting by %s" % name
        return Idle()

    # Follow
    self["goal"] = "Following %s (%dm)" % (name, int(owner["distance"]))
    return Follow(owner["id"], speed=4.0, min_distance=FOLLOW_DISTANCE)
