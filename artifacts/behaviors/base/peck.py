"""Peck — timid ground-feeding bird behavior.

Generic pecking/scratching behavior for birds. Flees from threats,
pecks at seeds, dust-bathes, and flocks with same-type creatures.
Does NOT lay eggs or roost — those are separate traits defined
in the creature's Python artifact.

Parameters (optional via self dict):
  scatter_range  — flee trigger distance (default 4)
  peck_chance    — probability of pecking vs scratching (default 0.45)
"""
from agentica_engine import Idle, Wander, Flee, MoveTo, DropItem
import random

PECK_CHANCE = 0.45
EGG_COOLDOWN = 20.0  # TEST: 20s cooldown
EGG_CHANCE = 1.0     # TEST: 100% chance

_activity = "idle"     # idle, pecking, dust_bath
_activity_timer = 0
_was_startled = False
_egg_cooldown = 0
_rng_seeded = False

def decide(self, world):
    global _activity, _activity_timer, _was_startled, _egg_cooldown, _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 31337 + 42)
        _rng_seeded = True
    dt = world["dt"]
    _activity_timer -= dt
    _egg_cooldown -= dt

    scatter_range = self.get("scatter_range", 4.0)
    peck_chance = self.get("peck_chance", PECK_CHANCE)

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
