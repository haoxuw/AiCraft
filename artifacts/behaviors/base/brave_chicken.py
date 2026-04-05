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

Generated from visual behavior tree, then hand-tuned for personality.
"""
from agentica_engine import Idle, Wander, Follow, Flee, MoveTo, DropItem
import random as _rng

_egg_cooldown = 0

def decide(self, world):
    global _egg_cooldown
    _egg_cooldown -= world["dt"]

    # Fear only dogs
    for e in world["nearby"]:
        if e["type_id"] == "base:dog" and e["distance"] < 6:
            self["goal"] = "EEK! Dog!"
            return Flee(e["id"], speed=6.0)

    # Chase cats! (brave chicken is aggressive toward felines)
    for e in world["nearby"]:
        if e["type_id"] == "base:cat" and e["distance"] < 8:
            self["goal"] = "BAWK! Chasing cat!"
            return Follow(e["id"], speed=self.get("walk_speed", 2.5) * 1.5, min_distance=1.0)

    # Roost at dusk
    time = world.get("time", 0.5)
    if 0.65 < time < 0.80:
        for b in world["blocks"]:
            if b["type"] in ("base:wood", "base:fence", "base:planks") and b["y"] > self["y"] + 1.5:
                self["goal"] = "Seeking roost"
                return MoveTo(b["x"] + 0.5, b["y"] + 1.0, b["z"] + 0.5)
        self["goal"] = "Settling down"
        return Idle()

    # Follow player and occasionally lay eggs when near them
    for e in world["nearby"]:
        if e["category"] == "player":
            if e["distance"] < 3 and _egg_cooldown <= 0 and _rng.random() < 0.15:
                _egg_cooldown = 8.0
                self["goal"] = "*happy cluck* Laid an egg!"
                return DropItem("base:egg", 1)
            if e["distance"] > 3:
                self["goal"] = "Following player"
                return Follow(e["id"], speed=3.0, min_distance=2.0)
            self["goal"] = "Sitting by player"
            return Idle()

    # Rejoin flock if far
    if all(e["distance"] > 4 for e in world["nearby"]
           if e["type_id"] == self["type_id"] and e["id"] != self["id"]):
        for e in world["nearby"]:
            if e["type_id"] == self["type_id"] and e["id"] != self["id"]:
                self["goal"] = "Rejoining flock"
                return Follow(e["id"])

    # Wander proudly
    self["goal"] = "Strutting around"
    return Wander(speed=self.get("walk_speed", 2.5))
