"""Wander — herd animal behavior (pigs and others).

Animals roam in groups, flee from threats, and have idle activities:
grazing, mud-seeking (near water/dirt), and herd-sticking. When one
pig panics, nearby pigs panic too (stampede).

Parameters (optional via self dict):
  flee_range    — distance to flee from players (default 5)
  group_range   — max distance before rejoining herd (default 6)
  graze_chance  — probability of stopping to graze per decide (default 0.25)
"""
from agentworld_engine import Idle, Wander, Flee, MoveTo
import random

_graze_timer = 0
_activity = "idle"  # idle, grazing, mud_bath, stampede

def decide(self, world):
    global _graze_timer, _activity
    dt = world["dt"]
    _graze_timer -= dt

    flee_range = self.get("flee_range", 5.0)
    group_range = self.get("group_range", 6.0)
    graze_chance = self.get("graze_chance", 0.25)

    # ── Flee from players and cats ──
    threats = [e for e in world["nearby"]
               if (e["category"] == "player" or e["type_id"] == "base:cat")
               and e["distance"] < flee_range]
    if threats:
        closest = min(threats, key=lambda e: e["distance"])
        _activity = "stampede"
        self["goal"] = "Fleeing!"
        return Flee(closest["id"], speed=self["walk_speed"] * 1.8)

    # ── Herd stampede: flee if a nearby friend is fleeing ──
    if _activity != "stampede":
        friends = [e for e in world["nearby"]
                   if e["type_id"] == self["type_id"] and e["id"] != self["id"]]
        for f in friends:
            # If a friend is moving fast (> run speed * 0.7), they're probably fleeing
            if f["distance"] < 8:
                # Check if any threat is near that friend
                for t in world["nearby"]:
                    if (t["category"] == "player" or t["type_id"] == "base:cat") \
                       and t["distance"] < flee_range + 3:
                        _activity = "stampede"
                        self["goal"] = "Stampede!"
                        return Flee(t["id"], speed=self["walk_speed"] * 1.6)

    _activity = "idle"

    # ── Stay with herd ──
    friends = [e for e in world["nearby"]
               if e["type_id"] == self["type_id"] and e["id"] != self["id"]]
    if friends:
        farthest = max(friends, key=lambda e: e["distance"])
        if farthest["distance"] > group_range:
            self["goal"] = "Joining herd"
            return MoveTo(farthest["x"], farthest["y"], farthest["z"],
                          speed=self["walk_speed"])

    # ── Seek water/mud (pigs love mud) ──
    if _graze_timer <= 0 and random.random() < 0.1:
        water_blocks = [b for b in world["blocks"]
                        if b["type"] == "base:water" and b["distance"] < 15]
        if water_blocks:
            nearest = min(water_blocks, key=lambda b: b["distance"])
            if nearest["distance"] > 2:
                _graze_timer = 6.0
                self["goal"] = "Heading to water"
                return MoveTo(nearest["x"] + 0.5, nearest["y"] + 1,
                              nearest["z"] + 0.5, speed=self["walk_speed"] * 0.8)
            else:
                _graze_timer = 4.0 + random.random() * 3.0
                self["goal"] = "Wallowing in mud"
                return Idle()

    # ── Graze ──
    if _graze_timer <= 0 and random.random() < graze_chance:
        _graze_timer = 3.0 + random.random() * 4.0
        self["goal"] = "Grazing"
        return Idle()

    if _graze_timer > 0:
        self["goal"] = "Grazing"
        return Idle()

    # ── Wander ──
    self["goal"] = "Wandering"
    return Wander(speed=self["walk_speed"])
