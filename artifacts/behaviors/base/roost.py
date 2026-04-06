"""Roost — seeks elevated perches at dusk.

Birds fly to elevated blocks (fences, logs, cobblestone) when
it gets dark and sit there. Generic roosting trait — compose
with peck or wander for a complete bird behavior.

Parameters (optional via self dict):
  roost_height  — min height above ground for a perch (default 1.5)
"""
from modcraft_engine import Idle, MoveTo
import random

_roost_target = None
_roost_timer = 0
_rng_seeded = False

def decide(self, world):
    global _roost_target, _roost_timer, _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 4591 + 77)
        _rng_seeded = True
    dt = world["dt"]
    _roost_timer -= dt

    roost_height = self.get("roost_height", 1.5)

    # Walking to roost
    if _roost_target:
        dx = self["x"] - _roost_target["x"]
        dz = self["z"] - _roost_target["z"]
        dist = (dx * dx + dz * dz) ** 0.5
        if dist < 2.0 or self["y"] > _roost_target["y"]:
            _roost_timer = 8.0 + random.random() * 6.0
            _roost_target = None
            self["goal"] = "Roosting"
            return Idle()
        self["goal"] = "Flying to roost..."
        return MoveTo(_roost_target["x"] + 0.5, _roost_target["y"] + 1.0,
                      _roost_target["z"] + 0.5, speed=self["walk_speed"])

    # Currently roosting
    if _roost_timer > 0:
        self["goal"] = "Roosting"
        return Idle()

    # Seek roost at dusk
    time = world.get("time", 0.5)
    is_dusk = 0.65 < time < 0.80
    if is_dusk and random.random() < 0.15:
        perch_types = {"base:wood", "base:fence", "base:cobblestone", "base:planks"}
        perches = [b for b in world["blocks"]
                   if b["type"] in perch_types
                   and b["y"] > self["y"] + roost_height
                   and b["distance"] < 10]
        if perches:
            _roost_target = min(perches, key=lambda b: b["distance"])
            self["goal"] = "Seeking roost..."
            return MoveTo(_roost_target["x"] + 0.5, _roost_target["y"] + 1.0,
                          _roost_target["z"] + 0.5, speed=self["walk_speed"])

    return None  # no action — let the main behavior handle movement
