"""Prowl — independent hunter that chases prey and naps.

Wanders slowly, chases any chicken that gets too close,
and takes frequent naps. Very independent.
"""
from agentworld_engine import Idle, Wander, Follow

_nap_timer = 0
_napping = False

def decide(self, world):
    global _nap_timer, _napping
    _nap_timer -= world["dt"]

    if _napping:
        if _nap_timer <= 0:
            _napping = False
            _nap_timer = 15.0
        self["goal"] = "Napping..."
        return Idle()

    # Chase chickens
    chickens = [e for e in world["nearby"]
                if e.type_id == "base:chicken" and e.distance < 8]
    if chickens:
        target = min(chickens, key=lambda e: e.distance)
        self["goal"] = "Chasing chicken!"
        return Follow(target.id, speed=self["walk_speed"] * 1.5, min_distance=1)

    # Time for a nap?
    if _nap_timer <= 0:
        _napping = True
        _nap_timer = 7.0
        self["goal"] = "Finding a nap spot..."
        return Idle()

    self["goal"] = "Prowling"
    return Wander(speed=self["walk_speed"] * 0.6)
