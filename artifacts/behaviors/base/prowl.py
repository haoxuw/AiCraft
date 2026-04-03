"""Prowl — independent hunter that stalks prey and naps on high perches.

Cats are solitary hunters. They stalk chickens, avoid dogs,
and seek elevated blocks for napping. Very independent —
they ignore players completely.
"""
from agentworld_engine import Idle, Wander, Follow, Flee, MoveTo
import random

_nap_timer = 0
_napping = False
_hunt_cooldown = 0
_perch_target = None

def decide(self, world):
    global _nap_timer, _napping, _hunt_cooldown, _perch_target
    dt = world["dt"]
    _nap_timer -= dt
    _hunt_cooldown -= dt

    # Napping on a perch
    if _napping:
        if _nap_timer <= 0:
            _napping = False
            _nap_timer = 12.0 + random.random() * 8.0
            _perch_target = None
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

    # Time for a nap? Seek a high block to perch on
    if _nap_timer <= 0:
        # Look for elevated blocks (wood, leaves, stone above ground level)
        perch_types = {"base:wood", "base:leaves", "base:cobblestone",
                       "base:stone", "base:planks"}
        high_blocks = [b for b in world["blocks"]
                       if b["type"] in perch_types
                       and b["y"] > self["y"] + 1.5
                       and b["distance"] < 12]
        if high_blocks:
            # Pick the highest nearby block
            best = max(high_blocks, key=lambda b: b["y"])
            _perch_target = best
            self["goal"] = "Climbing to perch..."
            return MoveTo(best["x"] + 0.5, best["y"] + 1.0, best["z"] + 0.5,
                          speed=self["walk_speed"])

        # No perch found — nap on the ground
        _napping = True
        _nap_timer = 5.0 + random.random() * 5.0
        self["goal"] = "Curling up for a nap..."
        return Idle()

    # Walking to perch target
    if _perch_target:
        dx = self["x"] - _perch_target["x"]
        dz = self["z"] - _perch_target["z"]
        dist = (dx * dx + dz * dz) ** 0.5
        if dist < 2.0 or self["y"] > _perch_target["y"]:
            # Reached the perch — start napping
            _napping = True
            _nap_timer = 6.0 + random.random() * 6.0
            _perch_target = None
            self["goal"] = "Napping on perch zzz"
            return Idle()
        self["goal"] = "Climbing to perch..."
        return MoveTo(_perch_target["x"] + 0.5, _perch_target["y"] + 1.0,
                      _perch_target["z"] + 0.5, speed=self["walk_speed"])

    # Prowl slowly
    self["goal"] = "Prowling"
    return Wander(speed=self["walk_speed"] * 0.5)
