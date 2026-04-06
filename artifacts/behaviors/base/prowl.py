"""Prowl — moody cat with random whims: hunt, nap, follow player, or explore.

Cats are unpredictable. Each decision cycle they randomly pick a mood:
  - Hunt:   stalk and chase chickens (gives up after chase_range)
  - Nap:    find a high perch or curl up on the ground
  - Curious: follow a nearby player at a distance, then lose interest
  - Explore: wander slowly, investigate blocks

They always flee from dogs regardless of mood.

Parameters (set via self dict, all optional):
  chase_range   — max distance to chase prey (default 10)
  flee_range    — distance to flee from dogs (default 5)
  curiosity     — 0.0-1.0, how often the cat follows players (default 0.3)
"""
from modcraft_engine import Idle, Wander, Follow, Flee, MoveTo
import random

# Mood states
MOOD_IDLE = "idle"
MOOD_HUNT = "hunt"
MOOD_NAP = "nap"
MOOD_CURIOUS = "curious"
MOOD_EXPLORE = "explore"

_mood = MOOD_IDLE
_mood_timer = 0        # how long to stay in current mood
_napping = False
_nap_timer = 0
_hunt_cooldown = 0
_perch_target = None
_chase_origin = None
_curiosity_target = None
_bored_timer = 0       # lose interest timer for curiosity

def _pick_mood(curiosity):
    """Randomly choose next mood based on personality."""
    r = random.random()
    if r < 0.30:
        return MOOD_HUNT
    elif r < 0.30 + curiosity * 0.35:
        return MOOD_CURIOUS
    elif r < 0.75:
        return MOOD_EXPLORE
    else:
        return MOOD_NAP

_rng_seeded = False

def decide(self, world):
    global _mood, _mood_timer, _napping, _nap_timer, _hunt_cooldown, _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 31337 + 42)
        _rng_seeded = True
    global _perch_target, _chase_origin, _curiosity_target, _bored_timer
    dt = world["dt"]
    _mood_timer -= dt
    _nap_timer -= dt
    _hunt_cooldown -= dt
    _bored_timer -= dt

    chase_range = self.get("chase_range", 10)
    flee_range = self.get("flee_range", 5)
    curiosity = self.get("curiosity", 0.3)

    # ── Always flee from dogs ──
    dogs = [e for e in world["nearby"]
            if e["type_id"] == "base:dog" and e["distance"] < flee_range]
    if dogs:
        _chase_origin = None
        _napping = False
        _mood = MOOD_IDLE
        _mood_timer = 0
        self["goal"] = "Avoiding dog!"
        return Flee(dogs[0]["id"], speed=self["walk_speed"] * 1.8)

    # ── Napping (persists across mood changes) ──
    if _napping:
        if _nap_timer <= 0:
            _napping = False
            _perch_target = None
            _mood_timer = 0  # pick new mood
        else:
            self["goal"] = "Napping zzz"
            return Idle()

    # ── Walking to perch ──
    if _perch_target:
        dx = self["x"] - _perch_target["x"]
        dz = self["z"] - _perch_target["z"]
        dist = (dx * dx + dz * dz) ** 0.5
        if dist < 2.0 or self["y"] > _perch_target["y"]:
            _napping = True
            _nap_timer = 6.0 + random.random() * 8.0
            _perch_target = None
            self["goal"] = "Napping on perch zzz"
            return Idle()
        self["goal"] = "Climbing to perch..."
        return MoveTo(_perch_target["x"] + 0.5, _perch_target["y"] + 1.0,
                      _perch_target["z"] + 0.5, speed=self["walk_speed"])

    # ── Pick new mood when timer expires ──
    if _mood_timer <= 0:
        _mood = _pick_mood(curiosity)
        _mood_timer = 6.0 + random.random() * 10.0
        _chase_origin = None
        _curiosity_target = None
        _bored_timer = 4.0 + random.random() * 6.0

    # ── MOOD: Hunt ──
    if _mood == MOOD_HUNT and _hunt_cooldown <= 0:
        chickens = [e for e in world["nearby"]
                    if e["type_id"] == "base:chicken" and e["distance"] < chase_range]
        if chickens:
            target = min(chickens, key=lambda e: e["distance"])

            if _chase_origin is None:
                _chase_origin = (self["x"], self["z"])

            # Give up if chased too far
            dx = self["x"] - _chase_origin[0]
            dz = self["z"] - _chase_origin[1]
            if (dx * dx + dz * dz) ** 0.5 > chase_range:
                _hunt_cooldown = 5.0
                _chase_origin = None
                _mood_timer = 0  # pick new mood
                self["goal"] = "Lost interest..."
                return Idle()

            if target["distance"] < 2:
                _hunt_cooldown = 8.0
                _chase_origin = None
                _mood_timer = 0
                self["goal"] = "Got one! Resting..."
                return Idle()

            self["goal"] = "Stalking chicken..."
            return Follow(target["id"], speed=self["walk_speed"] * 1.3, min_distance=1)
        else:
            _chase_origin = None

    # ── MOOD: Curious (follow player) ──
    if _mood == MOOD_CURIOUS:
        if _curiosity_target is None:
            players = [e for e in world["nearby"] if e["category"] == "player"]
            if players:
                _curiosity_target = min(players, key=lambda e: e["distance"])["id"]

        if _curiosity_target is not None:
            target = None
            for e in world["nearby"]:
                if e["id"] == _curiosity_target:
                    target = e
                    break

            if target and target["distance"] < 20:
                if _bored_timer <= 0:
                    # Got bored, wander away
                    _mood_timer = 0
                    _curiosity_target = None
                    self["goal"] = "Bored now"
                    return Wander(speed=self["walk_speed"] * 0.6)

                if target["distance"] < 3:
                    # Close enough, sit and stare
                    self["goal"] = "Watching player..."
                    return Idle()

                self["goal"] = "Following player..."
                return Follow(target["id"], speed=self["walk_speed"] * 0.8,
                              min_distance=2.5)
            else:
                _curiosity_target = None

    # ── MOOD: Nap ──
    if _mood == MOOD_NAP:
        perch_types = {"base:wood", "base:leaves", "base:cobblestone",
                       "base:stone", "base:planks"}
        high_blocks = [b for b in world["blocks"]
                       if b["type"] in perch_types
                       and b["y"] > self["y"] + 1.5
                       and b["distance"] < 12]
        if high_blocks:
            best = max(high_blocks, key=lambda b: b["y"])
            _perch_target = best
            self["goal"] = "Climbing to perch..."
            return MoveTo(best["x"] + 0.5, best["y"] + 1.0, best["z"] + 0.5,
                          speed=self["walk_speed"])

        _napping = True
        _nap_timer = 5.0 + random.random() * 7.0
        self["goal"] = "Curling up for a nap..."
        return Idle()

    # ── MOOD: Explore / default ──
    self["goal"] = "Prowling"
    return Wander(speed=self["walk_speed"] * 0.5)
