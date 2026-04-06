"""Follow — loyal companion that follows a player or NPC.

Generic following behavior. Finds the nearest player (or villager),
follows them, sits nearby, and occasionally does idle animations
(play bow, tail wag, panting).

Species-specific traits (guarding, barking at cats) should be
separate composable behaviors.

Parameters (optional via self dict):
  follow_dist   — how close to sit by owner (default 3)
  patrol_range  — how far to wander when no owner found (default 12)
"""
from modcraft_engine import Idle, Wander, Follow, MoveTo
import random

_play_timer = 0
_patrol_timer = 0
_home = None
_rng_seeded = False

def decide(self, world):
    global _play_timer, _patrol_timer, _home, _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 31337 + 42)
        _rng_seeded = True
    dt = world["dt"]
    _play_timer -= dt
    _patrol_timer -= dt

    follow_dist = self.get("follow_dist", 3.0)
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
        # No one around — patrol
        if _patrol_timer <= 0:
            _patrol_timer = 5.0 + random.random() * 5.0
            px = _home[0] + (random.random() - 0.5) * patrol_range * 2
            pz = _home[2] + (random.random() - 0.5) * patrol_range * 2
            self["goal"] = "Patrolling"
            return MoveTo(px, _home[1], pz, speed=self["walk_speed"] * 0.6)
        self["goal"] = "Sniffing around"
        return Wander(speed=self["walk_speed"] * 0.5)

    name = owner["type_id"].split(":")[1]

    # Close enough — sit, play, or idle
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

    # Follow owner
    self["goal"] = "Following %s (%dm)" % (name, int(owner["distance"]))
    return Follow(owner["id"], speed=4.0, min_distance=follow_dist)
