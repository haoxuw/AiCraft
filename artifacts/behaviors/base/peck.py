"""Peck — timid ground-feeding bird behavior.

Generic pecking/scratching behavior for birds. Flees from threats,
pecks at seeds, dust-bathes, and flocks with same-type creatures.
Does NOT lay eggs or roost — those are separate traits defined
in the creature's Python artifact.

Parameters (optional via self dict):
  scatter_range  — flee trigger distance (default 4)
  peck_chance    — probability of pecking vs scratching (default 0.45)
  home_radius    — max wander distance from spawn (default 25)
"""
from modcraft_engine import Idle, Wander, Flee, MoveTo, DropItem
import random

PECK_CHANCE = 0.45
EGG_COOLDOWN = 10.0
EGG_CHANCE = 0.20

_activity = "idle"     # idle, pecking, dust_bath
_activity_timer = 0
_was_startled = False
_egg_cooldown = 0
_rng_seeded = False
_home = None
_sleeping = False

def decide(self, world):
    global _activity, _activity_timer, _was_startled, _egg_cooldown, _rng_seeded
    global _home, _sleeping
    if not _rng_seeded:
        random.seed(self["id"] * 31337 + 42)
        _rng_seeded = True
    dt = world["dt"]
    _activity_timer -= dt
    _egg_cooldown -= dt

    if _home is None:
        _home = (self["x"], self["y"], self["z"])

    scatter_range = self.get("scatter_range", 4.0)
    peck_chance = self.get("peck_chance", PECK_CHANCE)
    home_radius = float(self.get("home_radius", 25.0))

    time = world.get("time", 0.5)
    is_night = time > 0.75 or time < 0.25
    is_evening = 0.65 < time <= 0.75

    dx, dz = self["x"] - _home[0], self["z"] - _home[2]
    dist_home = (dx * dx + dz * dz) ** 0.5

    # ── Evening/Night: roost at home ──────────────────────────────────────
    if is_night:
        _sleeping = True
        if dist_home > 3:
            self["goal"] = "Heading home..."
            return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"])
        self["goal"] = "Roosting zzz"
        return Idle()

    if _sleeping:
        _sleeping = False
        self["goal"] = "Good morning! *cluck*"
        return Idle()

    if is_evening and dist_home > 3:
        self["goal"] = "Heading home to roost..."
        return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"])

    if dist_home > home_radius:
        self["goal"] = "Wandering back home"
        return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"] * 0.8)

    # ── Flee from threats (players and cats) ──
    threats = [e for e in world["nearby"]
               if (e["category"] == "player" or e["type_id"] == "base:cat")
               and e["distance"] < scatter_range]

    if threats:
        closest = min(threats, key=lambda e: e["distance"])

        # Lay egg on first startle (if cooldown ready)
        if not _was_startled and _egg_cooldown <= 0:
            _was_startled = True
            if random.random() < EGG_CHANCE:
                _egg_cooldown = EGG_COOLDOWN
                self["goal"] = "BAWK!! *lays egg!*"
                return DropItem("base:egg", 1)

        self["goal"] = "BAWK!! Scattering!"
        return Flee(closest["id"], speed=6.0)

    _was_startled = False

    # ── Dust bathing ──
    if _activity == "dust_bath" and _activity_timer > 0:
        self["goal"] = "Dust bathing"
        return Idle()

    if random.random() < 0.05 and _activity_timer <= 0:
        _activity = "dust_bath"
        _activity_timer = 3.0 + random.random() * 3.0
        self["goal"] = "Dust bathing"
        return Idle()

    # ── Flock together ──
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
    if random.random() < peck_chance:
        self["goal"] = "Pecking at seeds"
        return Idle()

    self["goal"] = "Scratching ground"
    return Wander(speed=1.8)
