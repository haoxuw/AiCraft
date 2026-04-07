"""Follow — loyal guard companion that follows a player or NPC.

Follows the nearest player or villager, guards them by chasing cats
and barking at strangers, and patrols near home when alone.

Parameters (optional via entity dict):
  follow_dist   — how close to sit by owner (default 3)
  patrol_range  — how far to wander when no owner found (default 12)
  guard_range   — radius to detect threats near owner (default 6)
  home_radius   — max wander distance from home (default 30)
"""
import random
from modcraft_engine import Idle, Wander, Follow, MoveTo
from behavior_base import Behavior


class FollowBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._sleeping = False
        self._resting = False
        self._play_timer = 0.0
        self._patrol_timer = 0.0
        self._bark_timer = 0.0
        self._rng_seeded = False

    def decide(self, entity, world):
        if not self._rng_seeded:
            random.seed(entity.id * 31337 + 42)
            self._rng_seeded = True

        dt = world.dt
        self._play_timer   -= dt
        self._patrol_timer -= dt
        self._bark_timer   -= dt
        self._home = self.init_home(entity, self._home)

        follow_dist  = float(entity.get("follow_dist",  3.0))
        patrol_range = float(entity.get("patrol_range", 12.0))
        guard_range  = float(entity.get("guard_range",  6.0))
        spd          = entity.walk_speed

        dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])

        # ── Evening/Night: head home ──────────────────────────────────────────
        if self.is_night(world) or self.is_evening(world):
            self._resting = True
            if self.is_night(world):
                self._sleeping = True
            if dist_home > 3:
                return MoveTo(*self._home, speed=spd), "Going home..."
            if self._sleeping:
                return Idle(), "Sleeping zzz"
            return Idle(), "Settling in for the night"

        if self._resting:
            self._resting = False
            self._sleeping = False
            return Idle(), "Good morning! *wag*"

        # Find owner (prefer player, then villager)
        owner = world.nearest("player") or world.get("base:villager")

        # ── Guard: chase cats near owner, bark at strangers ───────────────────
        if owner and owner.distance < guard_range:
            cat = world.get("base:cat", max_dist=guard_range)
            if cat:
                return Follow(cat.id, speed=spd * 1.5, min_distance=1), \
                       "Chasing cat away!"

            if self._bark_timer <= 0:
                # Bark at anything close that isn't a friendly type
                for e in world.entities:
                    if (e.type_id not in ("base:player", "base:dog", "base:villager")
                            and e.category != "item" and e.distance < 4):
                        if random.random() < 0.2:
                            self._bark_timer = 8.0
                            return Idle(), "*WOOF!* Alert!"
                        break

        if not owner:
            # No one around — patrol near home
            if self._patrol_timer <= 0:
                self._patrol_timer = 5.0 + random.random() * 5.0
                px = self._home[0] + (random.random() - 0.5) * patrol_range * 2
                pz = self._home[2] + (random.random() - 0.5) * patrol_range * 2
                return MoveTo(px, self._home[1], pz, speed=spd * 0.6), "Patrolling"
            return Wander(speed=spd * 0.5), "Sniffing around"

        name = owner.type_id.split(":")[1]

        # Close enough — sit, play, or idle
        if owner.distance < follow_dist:
            if self._play_timer <= 0 and random.random() < 0.08:
                self._play_timer = 10.0
                r = random.random()
                goal = "*play bow!*" if r < 0.4 else ("Tail wagging" if r < 0.7 else "Panting happily")
                return Idle(), goal
            return Idle(), "Sitting by %s" % name

        return Follow(owner.id, speed=4.0, min_distance=follow_dist), \
               "Following %s (%dm)" % (name, int(owner.distance))
