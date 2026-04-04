"""Peck — timid ground-feeding bird behavior.

Generic pecking/scratching behavior for birds. Flees from threats,
pecks at seeds, dust-bathes, and flocks with same-type creatures.
Does NOT lay eggs or roost — those are separate traits defined
in the creature's Python artifact.

Parameters (optional via self dict):
  scatter_range  — flee trigger distance (default 4)
  peck_chance    — probability of pecking vs scratching (default 0.45)
"""
from agentica_engine import Idle, Wander, Flee, MoveTo
import random

PECK_CHANCE = 0.45

_activity = "idle"     # idle, pecking, dust_bath
_activity_timer = 0
_rng_seeded = False

def decide(self, world):
    global _activity, _activity_timer, _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 31337 + 42)
        _rng_seeded = True
    dt = world["dt"]
    _activity_timer -= dt

    scatter_range = self.get("scatter_range", 4.0)
    peck_chance = self.get("peck_chance", PECK_CHANCE)

    # ── Flee from threats (players and cats) ──
    threats = [e for e in world["nearby"]
               if (e["category"] == "player" or e["type_id"] == "base:cat")
               and e["distance"] < scatter_range]

    if threats:
        closest = min(threats, key=lambda e: e["distance"])
        self["goal"] = "BAWK!! Scattering!"
        return Flee(closest["id"], speed=6.0)

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
