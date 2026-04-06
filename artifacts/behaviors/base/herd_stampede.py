"""Herd Stampede — flee if a friend is near a threat.

When a nearby same-type creature is near a threat, join the stampede.

Parameters: flee_range (default 5)
"""
from modcraft_engine import Flee

def decide(self, world):
    flee_range = self.get("flee_range", 5.0)
    friends = [e for e in world["nearby"]
               if e["type_id"] == self["type_id"] and e["id"] != self["id"]
               and e["distance"] < 8]
    for f in friends:
        for t in world["nearby"]:
            if (t["category"] == "player" or t["type_id"] == "base:cat") \
               and t["distance"] < flee_range + 3:
                self["goal"] = "Stampede!"
                return Flee(t["id"], speed=self["walk_speed"] * 1.6)
    return None
