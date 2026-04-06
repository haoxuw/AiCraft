"""Wander — generic herd animal roaming behavior.

Animals roam in groups, flee from threats, graze, and stay near
their herd. Generic for any herd animal (pig, sheep, cow, etc.)

Species-specific traits (mud-seeking, wool-growing, etc.) should
be separate composable behaviors.

Parameters (optional via self dict):
  flee_range    — distance to flee from players (default 5)
  group_range   — max distance before rejoining herd (default 6)
  graze_chance  — probability of stopping to graze per decide (default 0.25)
  home_radius   — max wander distance from spawn (default 35)
"""
from modcraft_engine import Idle, Wander, Flee, MoveTo
import random

_graze_timer = 0
_activity = "idle"  # idle, grazing, stampede
_rng_seeded = False
_home = None
_sleeping = False

def decide(self, world):
    global _graze_timer, _activity, _rng_seeded, _home, _sleeping
    if not _rng_seeded:
        random.seed(self["id"] * 31337 + 42)
        _rng_seeded = True
    dt = world["dt"]
    _graze_timer -= dt

    # Remember home (spawn position)
    if _home is None:
        _home = (self["x"], self["y"], self["z"])

    flee_range = self.get("flee_range", 5.0)
    group_range = self.get("group_range", 6.0)
    graze_chance = self.get("graze_chance", 0.25)
    home_radius = float(self.get("home_radius", 35.0))

    time = world.get("time", 0.5)
    is_night = time > 0.75 or time < 0.25
    is_evening = 0.65 < time <= 0.75

    dx, dz = self["x"] - _home[0], self["z"] - _home[2]
    dist_home = (dx * dx + dz * dz) ** 0.5

    # ── Evening/Night: head home ──────────────────────────────────────────
    if is_night:
        _sleeping = True
        if dist_home > 3:
            self["goal"] = "Heading home..."
            return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"])
        self["goal"] = "Sleeping zzz"
        return Idle()

    if _sleeping:
        _sleeping = False
        self["goal"] = "Good morning!"
        return Idle()

    if is_evening and dist_home > 3:
        self["goal"] = "Heading home (evening)..."
        return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"])

    if dist_home > home_radius:
        self["goal"] = "Wandering back home"
        return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"] * 0.8)

    # ── Flee from threats ──
    threats = [e for e in world["nearby"]
               if (e["category"] == "player" or e["type_id"] == "base:cat")
               and e["distance"] < flee_range]
    if threats:
        closest = min(threats, key=lambda e: e["distance"])
        _activity = "stampede"
        self["goal"] = "Fleeing!"
        return Flee(closest["id"], speed=self["walk_speed"] * 1.8)

    # ── Herd stampede: flee if a friend is panicking ──
    if _activity != "stampede":
        friends = [e for e in world["nearby"]
                   if e["type_id"] == self["type_id"] and e["id"] != self["id"]]
        for f in friends:
            if f["distance"] < 8:
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
