"""Flock — stay near same-type creatures.

Moves toward the farthest friend if they're too far away.
Works for birds (tight flocking) and herds (loose grouping).

Parameters: flock_range (default 4)
"""
from modcraft_engine import MoveTo

def decide(self, world):
    flock_range = self.get("flock_range", 4.0)
    friends = [e for e in world["nearby"]
               if e["type_id"] == self["type_id"] and e["id"] != self["id"]]
    if friends:
        farthest = max(friends, key=lambda e: e["distance"])
        if farthest["distance"] > flock_range:
            self["goal"] = "Rejoining flock"
            return MoveTo(farthest["x"], farthest["y"], farthest["z"],
                          speed=self["walk_speed"])
    return None
