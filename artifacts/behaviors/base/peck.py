"""Peck — timid ground-feeding bird that flocks, pecks grass, and roosts.

Priority order:
  1. Evening/night — roost at home
  2. Too far from home — return
  3. Flee from threats (lay egg on first startle)
  4. Peck grass (if standing on grass block)
  5. Flock with same-type birds nearby
  6. Peck seeds or scratch ground

Entity props (optional):
  scatter_range — flee trigger distance (default 4)
  peck_chance   — probability of pecking vs scratching (default 0.45)
  home_radius   — max wander distance from spawn (default 25)
"""
import random
from modcraft_engine import Idle, Wander, Flee, MoveTo, ConvertObject
from behavior_base import Behavior

EGG_COOLDOWN = 10.0
EGG_CHANCE   = 0.20


class PeckBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._sleeping = False
        self._resting = False
        self._activity = "idle"
        self._activity_timer = 0.0
        self._was_startled = False
        self._egg_cooldown = 0.0
        self._rng_seeded = False

    def decide(self, entity, world):
        if not self._rng_seeded:
            random.seed(entity.id * 31337 + 42)
            self._rng_seeded = True

        dt = world.dt
        self._activity_timer -= dt
        self._egg_cooldown   -= dt
        self._home = self.init_home(entity, self._home)

        scatter_range = float(entity.get("scatter_range", 4.0))
        peck_chance   = float(entity.get("peck_chance",   0.45))
        home_radius   = float(entity.get("home_radius",  25.0))
        spd           = entity.walk_speed

        dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])

        # ── Evening/Night: roost at home ─────────────────────────────────────
        if self.is_night(world) or self.is_evening(world):
            self._resting = True
            if self.is_night(world):
                self._sleeping = True
            if dist_home > 3:
                return MoveTo(*self._home, speed=spd), "Heading home to roost..."
            if self._sleeping:
                return Idle(), "Roosting zzz"
            return Idle(), "Settling in to roost"

        if self._resting:
            self._resting = False
            self._sleeping = False
            return Idle(), "Good morning! *cluck*"

        if dist_home > home_radius:
            return MoveTo(*self._home, speed=spd * 0.8), "Wandering back home"

        # ── Flee from threats ────────────────────────────────────────────────
        threat_p = world.nearest("player", max_dist=scatter_range)
        threat_c = world.get("base:cat",   max_dist=scatter_range)
        threats  = [t for t in [threat_p, threat_c] if t]
        if threats:
            closest = min(threats, key=lambda t: t.distance)
            if not self._was_startled and self._egg_cooldown <= 0:
                self._was_startled = True
                if random.random() < EGG_CHANCE and entity.hp > 2:
                    self._egg_cooldown = EGG_COOLDOWN
                    return (ConvertObject(from_item="hp", from_count=2,
                                         to_item="base:egg", to_count=1,
                                         direct=False),
                            "BAWK!! *lays egg!*")
            return Flee(closest.id, speed=6.0), "BAWK!! Scattering!"

        self._was_startled = False

        # ── Peck grass (if standing on grass block) ──────────────────────────
        if self._activity == "peck_grass" and self._activity_timer > 0:
            return Idle(), "Pecking grass"

        on_grass = world.get("base:grass") is not None and any(
            abs(b.x - entity.x) < 1.5 and
            abs(b.z - entity.z) < 1.5 and
            abs(b.y - (entity.y - 1)) < 1.5
            for b in world.all("base:grass")
        )
        if on_grass and random.random() < 0.10 and self._activity_timer <= 0:
            self._activity = "peck_grass"
            self._activity_timer = 2.0 + random.random() * 3.0
            return Idle(), "Pecking grass"

        # ── Flock (only if same-type animals are actually nearby) ─────────────
        friends = world.all(entity.type_id)
        if friends:
            farthest = max(friends, key=lambda e: e.distance)
            if farthest.distance > 4:
                return MoveTo(farthest.x, farthest.y, farthest.z, speed=spd), \
                       "Rejoining flock"

        # ── Peck or scratch ──────────────────────────────────────────────────
        self._activity = "idle"
        if random.random() < peck_chance:
            return Idle(), "Pecking at seeds"

        return Wander(speed=1.8), "Scratching ground"
