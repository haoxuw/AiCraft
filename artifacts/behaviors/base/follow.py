"""Follow — loyal dog companion that follows, guards, and plays.

Dogs follow the nearest player (or villager if no player).
They guard their person by chasing cats away, bark at unfamiliar
entities, play-bow when happy, and patrol the village when alone.

Parameters (optional via self dict):
  follow_dist   — how close to sit by owner (default 3)
  guard_range   — distance to chase threats from owner (default 6)
  patrol_range  — how far to wander when no owner found (default 12)
"""
from agentica_engine import Idle, Wander, Follow, Flee, MoveTo
import random

_play_timer = 0
_bark_timer = 0
_patrol_timer = 0
_home = None
_activity = "idle"  # idle, sitting, playing, barking, patrolling

def decide(self, world):
    global _play_timer, _bark_timer, _patrol_timer, _home, _activity
    dt = world["dt"]
    _play_timer -= dt
    _bark_timer -= dt
    _patrol_timer -= dt

    follow_dist = self.get("follow_dist", 3.0)
    guard_range = self.get("guard_range", 6.0)
    patrol_range = self.get("patrol_range", 12.0)

    # Remember home position
    if _home is None:
        _home = (self["x"], self["y"], self["z"])

    # Find someone to follow (prefer player, then villager)
    players = [e for e in world["nearby"] if e["category"] == "player"]
    villagers = [e for e in world["nearby"] if e["type_id"] == "base:villager"]

    owner = None
    if players:
        owner = min(players, key=lambda e: e["distance"])
    elif villagers:
        owner = min(villagers, key=lambda e: e["distance"])

    if not owner:
        # No one around — patrol between home and nearby points
        if _patrol_timer <= 0:
            _patrol_timer = 5.0 + random.random() * 5.0
            angle = random.random() * 6.28
            px = _home[0] + random.random() * patrol_range * (1 if random.random() > 0.5 else -1)
            pz = _home[2] + random.random() * patrol_range * (1 if random.random() > 0.5 else -1)
            self["goal"] = "Patrolling"
            return MoveTo(px, _home[1], pz, speed=self["walk_speed"] * 0.6)
        self["goal"] = "Sniffing around"
        return Wander(speed=self["walk_speed"] * 0.5)

    name = owner["type_id"].split(":")[1]

    # ── Guard: chase cats near owner ──
    if owner["distance"] < guard_range:
        cats = [e for e in world["nearby"]
                if e["type_id"] == "base:cat" and e["distance"] < 5]
        if cats:
            cat = min(cats, key=lambda e: e["distance"])
            _activity = "barking"
            self["goal"] = "Chasing cat away!"
            return Follow(cat["id"], speed=self["walk_speed"] * 1.5, min_distance=1)

    # ── Bark at unfamiliar entities near owner ──
    if owner["distance"] < guard_range and _bark_timer <= 0:
        strangers = [e for e in world["nearby"]
                     if e["type_id"] not in ("base:player", "base:dog", "base:villager")
                     and e["category"] != "item"
                     and e["distance"] < 4]
        if strangers and random.random() < 0.2:
            _bark_timer = 8.0
            self["goal"] = "*WOOF!* Alert!"
            return Idle()

    # ── Close enough to owner — sit, play, or wag ──
    if owner["distance"] < follow_dist:
        if _play_timer <= 0 and random.random() < 0.08:
            _play_timer = 10.0
            r = random.random()
            if r < 0.4:
                self["goal"] = "*play bow!*"
            elif r < 0.7:
                self["goal"] = "Tail wagging"
            else:
                self["goal"] = "Panting happily"
            return Idle()
        self["goal"] = "Sitting by %s" % name
        return Idle()

    # ── Follow owner ──
    self["goal"] = "Following %s (%dm)" % (name, int(owner["distance"]))
    return Follow(owner["id"], speed=4.0, min_distance=follow_dist)
