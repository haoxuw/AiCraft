"""Wander — default animal behavior (pigs and others).

Animals roam randomly, flee from players, and try to stay
close to others of the same species. Sometimes they stop
to graze or rest.
"""
from agentworld_engine import Idle, Wander, Flee, MoveTo
import random

FLEE_DISTANCE = 5.0
GROUP_DISTANCE = 6.0
_graze_timer = 0

def decide(self, world):
    global _graze_timer
    _graze_timer -= world["dt"]

    # Flee from nearby players
    for e in world["nearby"]:
        if e["category"] == "player" and e["distance"] < FLEE_DISTANCE:
            self["goal"] = "Fleeing!"
            return Flee(e["id"], speed=self["walk_speed"] * 1.8)

    # Stay with friends of same species
    friends = [e for e in world["nearby"]
               if e["type_id"] == self["type_id"] and e["id"] != self["id"]]
    if friends:
        farthest = max(friends, key=lambda e: e["distance"])
        if farthest["distance"] > GROUP_DISTANCE:
            self["goal"] = "Joining herd"
            return MoveTo(farthest["x"], farthest["y"], farthest["z"],
                          speed=self["walk_speed"])

    # Graze (stop and eat)
    if _graze_timer <= 0 and random.random() < 0.25:
        _graze_timer = 3.0 + random.random() * 4.0
        self["goal"] = "Grazing"
        return Idle()

    if _graze_timer > 0:
        self["goal"] = "Grazing"
        return Idle()

    # Wander
    self["goal"] = "Wandering"
    return Wander(speed=self["walk_speed"])
