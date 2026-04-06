"""Follow — loyal companion that follows a player or NPC.

Generic following behavior. Finds the nearest player (or villager),
follows them, sits nearby, and occasionally does idle animations
(play bow, tail wag, panting).

Species-specific traits (guarding, barking at cats) should be
separate composable behaviors.

Parameters (optional via entity dict):
  follow_dist   — how close to sit by owner (default 3)
  patrol_range  — how far to wander when no owner found (default 12)
  home_radius   — max wander distance from home (default 30)
"""
import random
from modcraft_engine import Idle, Wander, Follow, MoveTo
from behavior_base import Behavior


class FollowBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._sleeping = False
        self._play_timer = 0.0
        self._patrol_timer = 0.0
        self._rng_seeded = False

    def decide(self, entity, world):
        if not self._rng_seeded:
            random.seed(entity["id"] * 31337 + 42)
            self._rng_seeded = True

        dt = world["dt"]
        self._play_timer   -= dt
        self._patrol_timer -= dt
        self._home = self.init_home(entity, self._home)

        follow_dist  = float(entity.get("follow_dist",  3.0))
        patrol_range = float(entity.get("patrol_range", 12.0))
        spd          = entity["walk_speed"]

        dist_home = self.dist2d(entity["x"], entity["z"],
                                self._home[0], self._home[2])

        # ── Evening/Night: head home ──────────────────────────────────────────
        if self.is_night(world):
            self._sleeping = True
            if dist_home > 3:
                return (MoveTo(self._home[0], self._home[1], self._home[2],
                               speed=spd),
                        "Going home...")
            return Idle(), "Sleeping zzz"

        if self._sleeping:
            self._sleeping = False
            return Idle(), "Good morning! *wag*"

        if self.is_evening(world) and dist_home > 3:
            return (MoveTo(self._home[0], self._home[1], self._home[2],
                           speed=spd),
                    "Heading home (evening)...")

        # Find someone to follow (prefer player, then villager)
        players   = [e for e in world["nearby"] if e["category"] == "player"]
        villagers = [e for e in world["nearby"] if e["type_id"] == "base:villager"]

        owner = None
        if players:
            owner = min(players, key=lambda e: e["distance"])
        elif villagers:
            owner = min(villagers, key=lambda e: e["distance"])

        if not owner:
            # No one around — patrol near home
            if self._patrol_timer <= 0:
                self._patrol_timer = 5.0 + random.random() * 5.0
                px = self._home[0] + (random.random() - 0.5) * patrol_range * 2
                pz = self._home[2] + (random.random() - 0.5) * patrol_range * 2
                return (MoveTo(px, self._home[1], pz, speed=spd * 0.6),
                        "Patrolling")
            return Wander(speed=spd * 0.5), "Sniffing around"

        name = owner["type_id"].split(":")[1]

        # Close enough — sit, play, or idle
        if owner["distance"] < follow_dist:
            if self._play_timer <= 0 and random.random() < 0.08:
                self._play_timer = 10.0
                r = random.random()
                if r < 0.4:
                    goal = "*play bow!*"
                elif r < 0.7:
                    goal = "Tail wagging"
                else:
                    goal = "Panting happily"
                return Idle(), goal
            return Idle(), "Sitting by %s" % name

        # Follow owner
        return (Follow(owner["id"], speed=4.0, min_distance=follow_dist),
                "Following %s (%dm)" % (name, int(owner["distance"])))
