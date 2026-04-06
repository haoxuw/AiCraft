"""Guard — protective companion trait.

Chases cats away from nearby players/villagers and barks at
unfamiliar entities. Compose with follow for a complete guard dog.

Parameters (optional via self dict):
  guard_range   — distance to detect threats near owner (default 6)
"""
from modcraft_engine import Idle, Follow
import random

_bark_timer = 0
_rng_seeded = False

def decide(self, world):
    global _bark_timer, _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 8923 + 57)
        _rng_seeded = True
    dt = world["dt"]
    _bark_timer -= dt

    guard_range = self.get("guard_range", 6.0)

    # Find owner (player or villager)
    players = [e for e in world["nearby"] if e["category"] == "player"]
    villagers = [e for e in world["nearby"] if e["type_id"] == "base:villager"]
    owner = None
    if players:
        owner = min(players, key=lambda e: e["distance"])
    elif villagers:
        owner = min(villagers, key=lambda e: e["distance"])
    if not owner or owner["distance"] > guard_range:
        return None

    # Chase cats near owner
    cats = [e for e in world["nearby"]
            if e["type_id"] == "base:cat" and e["distance"] < 5]
    if cats:
        cat = min(cats, key=lambda e: e["distance"])
        self["goal"] = "Chasing cat away!"
        return Follow(cat["id"], speed=self["walk_speed"] * 1.5, min_distance=1)

    # Bark at strangers
    if _bark_timer <= 0:
        strangers = [e for e in world["nearby"]
                     if e["type_id"] not in ("base:player", "base:dog", "base:villager")
                     and e["category"] != "item"
                     and e["distance"] < 4]
        if strangers and random.random() < 0.2:
            _bark_timer = 8.0
            self["goal"] = "*WOOF!* Alert!"
            return Idle()

    return None  # let main behavior handle
