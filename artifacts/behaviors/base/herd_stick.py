"""Herd Stick — stay near same-type herd members.

Same as flock but with a wider default range for larger animals.

Parameters: group_range (default 6)
"""
from modcraft_engine import MoveTo

def decide(self, world):
    group_range = self.get("group_range", 6.0)
    friends = [e for e in world["nearby"]
               if e["type_id"] == self["type_id"] and e["id"] != self["id"]]
    if friends:
        farthest = max(friends, key=lambda e: e["distance"])
        if farthest["distance"] > group_range:
            self["goal"] = "Joining herd"
            return MoveTo(farthest["x"], farthest["y"], farthest["z"],
                          speed=self["walk_speed"])
    return None
