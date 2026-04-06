"""Wander Slow — terminal rule, always wanders.

Place this last in any rule list as the default fallback.
Always returns an action (never None).
"""
from modcraft_engine import Wander

def decide(self, world):
    self["goal"] = "Wandering"
    return Wander(speed=self.get("walk_speed", 2.0))
