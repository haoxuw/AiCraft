"""Brave Chicken — a fearless hen that follows players, fights cats, and lays golden eggs.

Unlike normal chickens that scatter from everything, this chicken:
  - Follows the nearest player like a loyal companion
  - Charges at cats instead of fleeing from them
  - Lays eggs when near the player and feeling safe (random chance)
  - Seeks roost at dusk like normal chickens
  - Only flees from dogs (the one thing it fears)

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
from modcraft_engine import Idle, Wander, Follow, Flee, MoveTo, ConvertObject
from behavior_base import Behavior


class BraveChickenBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._sleeping = False
        self._resting = False
        self._egg_cooldown = 0.0

    def decide(self, entity, world):
        self._egg_cooldown -= world.dt
        self._home = self.init_home(entity, self._home)

        spd       = entity.walk_speed
        dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])

        # Fear dogs — highest priority, even at night (survival)
        dog = world.get("base:dog", max_dist=6)
        if dog:
            return Flee(dog.id, speed=6.0), "EEK! Dog!"

        # ── Evening/Night: roost ──────────────────────────────────────────────
        if self.is_night(world) or self.is_evening(world):
            self._resting = True
            if self.is_night(world):
                self._sleeping = True
            if dist_home > 3:
                return MoveTo(*self._home, speed=spd), "Heading home to roost..."
            if self._sleeping:
                return Idle(), "Roosting zzz"
            # Seek elevated perch if at home during evening
            for perch_type in ("base:wood", "base:fence", "base:planks"):
                b = world.get(perch_type)
                if b and b.y > entity.y + 1.5 and b.distance < 8:
                    return MoveTo(b.x + 0.5, b.y + 1.0, b.z + 0.5, speed=spd), \
                           "Seeking roost"
            return Idle(), "Settling down"

        if self._resting:
            self._resting = False
            self._sleeping = False
            return Idle(), "BAWK! Good morning!"

        # Chase cats! (brave chicken is aggressive toward felines — daytime only)
        cat = world.get("base:cat", max_dist=8)
        if cat:
            return Follow(cat.id, speed=spd * 1.5, min_distance=1.0), "BAWK! Chasing cat!"

        # Follow player and occasionally lay eggs when near them
        player = world.nearest("player")
        if player:
            if player.distance < 3 and self._egg_cooldown <= 0 \
                    and random.random() < 0.15 and entity.hp > 2:
                self._egg_cooldown = 8.0
                return (ConvertObject(from_item="hp", from_count=2,
                                     to_item="base:egg", to_count=1,
                                     direct=False),
                        "*happy cluck* Laid an egg!")
            if player.distance > 3:
                return Follow(player.id, speed=3.0, min_distance=2.0), "Following player"
            return Idle(), "Sitting by player"

        # Rejoin flock if same-type animals are nearby and we're far from them
        friends = world.all(entity.type_id)
        if friends and all(e.distance > 4 for e in friends):
            nearest = min(friends, key=lambda e: e.distance)
            return Follow(nearest.id), "Rejoining flock"

        return Wander(speed=spd), "Strutting around"
