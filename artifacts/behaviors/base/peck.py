"""Peck — timid chicken that scatters, lays eggs, and roosts.

Chickens flee from players and cats, have a chance to drop an egg
when startled, peck at seeds on the ground, dust-bathe, and seek
elevated blocks to roost on at dusk.

Parameters (optional via self dict):
  scatter_range  — flee trigger distance (default 4)
  egg_chance     — probability of egg drop on startle (default 0.20)
  roost_height   — min height above ground to consider a roost (default 1.5)
"""
from agentica_engine import Idle, Wander, Flee, MoveTo, DropItem
import random

PECK_CHANCE = 0.45
EGG_COOLDOWN = 10.0

_was_startled = False
_egg_cooldown = 0
_activity = "idle"     # idle, pecking, dust_bath, roosting
_activity_timer = 0
_roost_target = None

def decide(self, world):
    global _was_startled, _egg_cooldown, _activity, _activity_timer, _roost_target
    dt = world["dt"]
    _egg_cooldown -= dt
    _activity_timer -= dt

    scatter_range = self.get("scatter_range", 4.0)
    egg_chance = self.get("egg_chance", 0.20)
    roost_height = self.get("roost_height", 1.5)

    # ── Flee from players and cats ──
    threats = [e for e in world["nearby"]
               if (e["category"] == "player" or e["type_id"] == "base:cat")
               and e["distance"] < scatter_range]

    if threats:
        closest = min(threats, key=lambda e: e["distance"])
        _roost_target = None

        # Egg drop on first startle
        if not _was_startled and _egg_cooldown <= 0:
            _was_startled = True
            if random.random() < egg_chance:
                _egg_cooldown = EGG_COOLDOWN
                self["goal"] = "BAWK!! *lays egg!*"
                return DropItem("base:egg", 1)

        self["goal"] = "BAWK!! Scattering!"
        return Flee(closest["id"], speed=6.0)

    _was_startled = False

    # ── Walking to roost ──
    if _roost_target:
        dx = self["x"] - _roost_target["x"]
        dz = self["z"] - _roost_target["z"]
        dist = (dx * dx + dz * dz) ** 0.5
        if dist < 2.0 or self["y"] > _roost_target["y"]:
            _activity = "roosting"
            _activity_timer = 8.0 + random.random() * 6.0
            _roost_target = None
            self["goal"] = "Roosting"
            return Idle()
        self["goal"] = "Flying to roost..."
        return MoveTo(_roost_target["x"] + 0.5, _roost_target["y"] + 1.0,
                      _roost_target["z"] + 0.5, speed=self["walk_speed"])

    # ── Roosting (sitting on perch) ──
    if _activity == "roosting" and _activity_timer > 0:
        self["goal"] = "Roosting"
        return Idle()

    # ── Seek roost (occasionally, or if it's getting dark) ──
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

    # ── Dust bathing ──
    if _activity == "dust_bath" and _activity_timer > 0:
        self["goal"] = "Dust bathing"
        return Idle()

    if random.random() < 0.05 and _activity_timer <= 0:
        _activity = "dust_bath"
        _activity_timer = 3.0 + random.random() * 3.0
        self["goal"] = "Dust bathing"
        return Idle()

    # ── Flock together (chickens stick tight) ──
    friends = [e for e in world["nearby"]
               if e["type_id"] == self["type_id"] and e["id"] != self["id"]]
    if friends:
        farthest = max(friends, key=lambda e: e["distance"])
        if farthest["distance"] > 4:
            self["goal"] = "Rejoining flock"
            return MoveTo(farthest["x"], farthest["y"], farthest["z"],
                          speed=self["walk_speed"])

    # ── Peck or scratch ──
    _activity = "idle"
    if random.random() < PECK_CHANCE:
        self["goal"] = "Pecking at seeds"
        return Idle()

    self["goal"] = "Scratching ground"
    return Wander(speed=1.8)
