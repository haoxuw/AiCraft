"""Flee Threats — run from players and cats.

Universal survival rule. Should be first in most prey creatures' rule lists.

Parameters: flee_range (default 5)
"""
from agentica_engine import Flee

def decide(self, world):
    flee_range = self.get("flee_range", 5.0)
    threats = [e for e in world["nearby"]
               if (e["category"] == "player" or e["type_id"] == "base:cat")
               and e["distance"] < flee_range]
    if threats:
        closest = min(threats, key=lambda e: e["distance"])
        self["_was_startled"] = True
        self["goal"] = "Fleeing!"
        return Flee(closest["id"], speed=self["walk_speed"] * 1.8)
    self["_was_startled"] = False
    return None
