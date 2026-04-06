"""Brave Chicken — a fearless hen that follows players, fights cats, and lays golden eggs.

Unlike normal chickens that scatter from everything, this chicken:
  - Follows the nearest player like a loyal companion
  - Charges at cats instead of fleeing from them
  - Lays eggs when near the player and feeling safe (random chance)
  - Seeks roost at dusk like normal chickens
  - Only flees from dogs (the one thing it fears)

This behavior was designed using the visual behavior tree editor.
It demonstrates nested IF/THEN/ELSE with multiple conditions and actions.

Tree structure:
  IF [See Entity: dog]           → FLEE from dog
  ELIF [See Entity: cat]         → CHASE the cat!
  ELIF [Is Dusk]                 → Seek Roost
  ELIF [See Entity: player]
    IF [Random %: 15]            → Drop Egg (near player = happy)
    ELSE                         → Follow Player
  ELIF [Far From Flock]          → Rejoin flock
  ELSE                           → Wander
"""
import random
from modcraft_engine import Idle, Wander, Follow, Flee, MoveTo, DropItem
from behavior_base import Behavior


class BraveChickenBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._sleeping = False
        self._egg_cooldown = 0.0

    def decide(self, entity, world):
        self._egg_cooldown -= world["dt"]
        self._home = self.init_home(entity, self._home)

        spd       = entity.get("walk_speed", 2.5)
        dist_home = self.dist2d(entity["x"], entity["z"],
                                self._home[0], self._home[2])

        # ── Evening/Night: roost at home ──────────────────────────────────────
        if self.is_night(world):
            self._sleeping = True
            if dist_home > 3:
                return (MoveTo(self._home[0], self._home[1], self._home[2],
                               speed=spd),
                        "Heading home to roost...")
            return Idle(), "Roosting zzz"

        if self._sleeping:
            self._sleeping = False
            return Idle(), "BAWK! Good morning!"

        if self.is_evening(world) and dist_home > 3:
            return (MoveTo(self._home[0], self._home[1], self._home[2],
                           speed=spd),
                    "Heading home (evening)...")

        # Fear only dogs
        for e in world["nearby"]:
            if e["type_id"] == "base:dog" and e["distance"] < 6:
                return Flee(e["id"], speed=6.0), "EEK! Dog!"

        # Chase cats! (brave chicken is aggressive toward felines)
        for e in world["nearby"]:
            if e["type_id"] == "base:cat" and e["distance"] < 8:
                return (Follow(e["id"], speed=spd * 1.5, min_distance=1.0),
                        "BAWK! Chasing cat!")

        # Seek roost at dusk
        if self.is_evening(world):
            for b in world["blocks"]:
                if b["type"] in ("base:wood", "base:fence", "base:planks") \
                        and b["y"] > entity["y"] + 1.5:
                    return (MoveTo(b["x"] + 0.5, b["y"] + 1.0, b["z"] + 0.5),
                            "Seeking roost")
            return Idle(), "Settling down"

        # Follow player and occasionally lay eggs when near them
        for e in world["nearby"]:
            if e["category"] == "player":
                if e["distance"] < 3 and self._egg_cooldown <= 0 \
                        and random.random() < 0.15:
                    self._egg_cooldown = 8.0
                    return DropItem("base:egg", 1), "*happy cluck* Laid an egg!"
                if e["distance"] > 3:
                    return Follow(e["id"], speed=3.0, min_distance=2.0), "Following player"
                return Idle(), "Sitting by player"

        # Rejoin flock if far
        flock = [e for e in world["nearby"]
                 if e["type_id"] == entity["type_id"] and e["id"] != entity["id"]]
        if flock and all(e["distance"] > 4 for e in flock):
            nearest = min(flock, key=lambda e: e["distance"])
            return Follow(nearest["id"]), "Rejoining flock"

        # Wander proudly
        return Wander(speed=spd), "Strutting around"
