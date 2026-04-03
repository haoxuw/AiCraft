"""Prowl — independent hunter that stalks prey and naps.

Cats are solitary hunters. They stalk chickens, avoid dogs,
and take frequent naps in sunny spots. Very independent —
they ignore players completely.
"""
from agentworld_engine import Idle, Wander, Follow, Flee
import random

_nap_timer = 0
_napping = False
_hunt_cooldown = 0

def decide(self, world):
    global _nap_timer, _napping, _hunt_cooldown
    dt = world["dt"]
    _nap_timer -= dt
    _hunt_cooldown -= dt

    # Napping
    if _napping:
        if _nap_timer <= 0:
            _napping = False
            _nap_timer = 12.0 + random.random() * 8.0
        self["goal"] = "Napping zzz"
        return Idle()

    # Avoid dogs (flee if one is close)
    dogs = [e for e in world["nearby"]
            if e["type_id"] == "base:dog" and e["distance"] < 5]
    if dogs:
        self["goal"] = "Avoiding dog!"
        return Flee(dogs[0]["id"], speed=self["walk_speed"] * 1.8)

    # Hunt chickens
    if _hunt_cooldown <= 0:
        chickens = [e for e in world["nearby"]
                    if e["type_id"] == "base:chicken" and e["distance"] < 10]
        if chickens:
            target = min(chickens, key=lambda e: e["distance"])
            if target["distance"] < 2:
                # "Caught" it — rest after hunt
                _hunt_cooldown = 8.0
                self["goal"] = "Got one! Resting..."
                return Idle()
            self["goal"] = "Stalking chicken..."
            return Follow(target["id"], speed=self["walk_speed"] * 1.3, min_distance=1)

    # Time for a nap?
    if _nap_timer <= 0:
        _napping = True
        _nap_timer = 5.0 + random.random() * 5.0
        self["goal"] = "Finding a sunny spot..."
        return Idle()

    # Prowl slowly
    self["goal"] = "Prowling"
    return Wander(speed=self["walk_speed"] * 0.5)
