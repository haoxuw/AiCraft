"""Prowl — moody cat with random whims: hunt, nap, follow player, or explore.

Cats are unpredictable. Each decision cycle they randomly pick a mood:
  - Hunt:    stalk and chase chickens (gives up after chase_range)
  - Nap:     find a high perch or curl up on the ground
  - Curious: follow a nearby player at a distance, then lose interest
  - Explore: wander slowly, investigate blocks

They always flee from dogs regardless of mood.

Parameters (set via entity dict, all optional):
  chase_range   — max distance to chase prey (default 20)
  flee_range    — distance to flee from dogs (default 5)
  curiosity     — 0.0-1.0, how often the cat follows players (default 0.3)
  home_radius   — max wander distance from home (default 25)
"""
import random
from modcraft_engine import Idle, Wander, Follow, Flee, MoveTo
from behavior_base import Behavior

MOOD_IDLE    = "idle"
MOOD_HUNT    = "hunt"
MOOD_NAP     = "nap"
MOOD_CURIOUS = "curious"
MOOD_EXPLORE = "explore"


class ProwlBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._sleeping = False
        self._resting = False
        self._mood = MOOD_IDLE
        self._mood_timer = 0.0
        self._napping = False
        self._nap_timer = 0.0
        self._hunt_cooldown = 0.0
        self._perch_target = None
        self._chase_origin = None
        self._curiosity_target = None
        self._bored_timer = 0.0
        self._rng_seeded = False
        self._returning_home = False

    def _pick_mood(self, curiosity):
        r = random.random()
        if r < 0.30:
            return MOOD_HUNT
        elif r < 0.30 + curiosity * 0.35:
            return MOOD_CURIOUS
        elif r < 0.75:
            return MOOD_EXPLORE
        else:
            return MOOD_NAP

    def decide(self, entity, world):
        if not self._rng_seeded:
            random.seed(entity.id * 31337 + 42)
            self._rng_seeded = True

        dt = world.dt
        self._mood_timer    -= dt
        self._nap_timer     -= dt
        self._hunt_cooldown -= dt
        self._bored_timer   -= dt
        self._home = self.init_home(entity, self._home)

        chase_range  = float(entity.get("chase_range", 20.0))
        flee_range   = float(entity.get("flee_range",   5.0))
        curiosity    = float(entity.get("curiosity",    0.3))
        home_radius  = float(entity.get("home_radius", 25.0))
        spd          = entity.walk_speed

        dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])

        # ── Evening/Night: return home ────────────────────────────────────────
        if self.is_night(world) or self.is_evening(world):
            self._napping = False
            self._mood_timer = 99.0
            self._resting = True
            if self.is_night(world):
                self._sleeping = True
            if dist_home > 4:
                return MoveTo(*self._home, speed=spd), "Heading home..."
            if self._sleeping:
                return Idle(), "Curled up at home zzz"
            return Idle(), "Settling in for the night"

        if self._resting:
            self._resting = False
            self._sleeping = False
            self._mood_timer = 0.0
            return Idle(), "Stretching... meow"

        # Hysteresis: start returning at home_radius, stop at home_radius * 0.5
        if dist_home > home_radius:
            self._returning_home = True
        if self._returning_home:
            if dist_home < home_radius * 0.5:
                self._returning_home = False
                self._mood_timer = 0.0
                self._napping = False
            else:
                return MoveTo(*self._home, speed=spd * 0.7), "Wandering back home"

        # ── Always flee from dogs ─────────────────────────────────────────────
        dog = world.get("base:dog", max_dist=flee_range)
        if dog:
            self._chase_origin = None
            self._napping = False
            self._mood = MOOD_IDLE
            self._mood_timer = 0.0
            return Flee(dog.id, speed=spd * 1.8), "Avoiding dog!"

        # ── Napping ───────────────────────────────────────────────────────────
        if self._napping:
            if self._nap_timer <= 0:
                self._napping = False
                self._perch_target = None
                self._mood_timer = 0.0
            else:
                return Idle(), "Napping zzz"

        # ── Walking to perch ──────────────────────────────────────────────────
        if self._perch_target:
            p = self._perch_target
            dx = entity.x - p.x
            dz = entity.z - p.z
            dist = (dx * dx + dz * dz) ** 0.5
            if dist < 2.0 or entity.y > p.y:
                self._napping = True
                self._nap_timer = 6.0 + random.random() * 8.0
                self._perch_target = None
                return Idle(), "Napping on perch zzz"
            return MoveTo(p.x + 0.5, p.y + 1.0, p.z + 0.5, speed=spd), \
                   "Climbing to perch..."

        # ── Pick new mood when timer expires ──────────────────────────────────
        if self._mood_timer <= 0:
            self._mood = self._pick_mood(curiosity)
            self._mood_timer = 6.0 + random.random() * 10.0
            self._chase_origin = None
            self._curiosity_target = None
            self._bored_timer = 4.0 + random.random() * 6.0

        # ── MOOD: Hunt ────────────────────────────────────────────────────────
        if self._mood == MOOD_HUNT and self._hunt_cooldown <= 0:
            chicken = world.get("base:chicken", max_dist=chase_range)
            if chicken:
                if self._chase_origin is None:
                    self._chase_origin = (entity.x, entity.z)
                dx = entity.x - self._chase_origin[0]
                dz = entity.z - self._chase_origin[1]
                if (dx * dx + dz * dz) ** 0.5 > chase_range:
                    self._hunt_cooldown = 5.0
                    self._chase_origin = None
                    self._mood_timer = 0.0
                    return Idle(), "Lost interest..."
                if chicken.distance < 2:
                    self._hunt_cooldown = 8.0
                    self._chase_origin = None
                    self._mood_timer = 0.0
                    return Idle(), "Got one! Resting..."
                return Follow(chicken.id, speed=spd * 1.3, min_distance=1), \
                       "Stalking chicken..."
            else:
                self._chase_origin = None

        # ── MOOD: Curious ─────────────────────────────────────────────────────
        if self._mood == MOOD_CURIOUS:
            if self._curiosity_target is None:
                player = world.nearest("player")
                if player:
                    self._curiosity_target = player.id

            if self._curiosity_target is not None:
                # Re-query to get current position
                target = next(
                    (e for e in world.entities if e.id == self._curiosity_target), None
                )
                if target and target.distance < 20:
                    if self._bored_timer <= 0:
                        self._mood_timer = 0.0
                        self._curiosity_target = None
                        return Wander(speed=spd * 0.6), "Bored now"
                    if target.distance < 3:
                        return Idle(), "Watching player..."
                    return Follow(target.id, speed=spd * 0.8, min_distance=2.5), \
                           "Following player..."
                else:
                    self._curiosity_target = None

        # ── MOOD: Nap ─────────────────────────────────────────────────────────
        if self._mood == MOOD_NAP:
            perch_types = ("base:wood", "base:leaves", "base:cobblestone",
                           "base:stone", "base:planks")
            best = None
            for pt in perch_types:
                b = world.get(pt)
                if b and b.y > entity.y + 1.5 and b.distance < 12:
                    if best is None or b.y > best.y:
                        best = b
            if best:
                self._perch_target = best
                return MoveTo(best.x + 0.5, best.y + 1.0, best.z + 0.5, speed=spd), \
                       "Climbing to perch..."
            self._napping = True
            self._nap_timer = 5.0 + random.random() * 7.0
            return Idle(), "Curling up for a nap..."

        # ── MOOD: Explore / default ───────────────────────────────────────────
        return Wander(speed=spd * 0.5), "Prowling"
