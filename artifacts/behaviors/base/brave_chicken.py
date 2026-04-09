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
from modcraft_engine import Move, Convert, Ground
from behavior_base import Behavior


class BraveChickenBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._sleeping = False
        self._resting = False
        self._egg_cooldown = 0.0

    def decide(self, entity: "SelfEntity", local_world: "LocalWorld"):
        self._egg_cooldown -= local_world.dt
        self._home = self.init_home(entity, self._home)

        spd       = entity.walk_speed
        dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])

        # Fear dogs — highest priority, even at night (survival)
        dog = local_world.get("base:dog", max_dist=6)
        if dog:
            return Move(*self.flee_pos(entity, dog), speed=6.0), "EEK! Dog!"

        # ── Evening/Night: roost ──────────────────────────────────────────────
        if self.is_night(local_world) or self.is_evening(local_world):
            self._resting = True
            if self.is_night(local_world):
                self._sleeping = True
            if dist_home > 3:
                return Move(*self._home, speed=spd), "Heading home to roost..."
            if self._sleeping:
                return Move(entity.x, entity.y, entity.z),"Roosting zzz"
            # Seek elevated perch if at home during evening
            for perch_type in ("base:wood", "base:fence", "base:planks"):
                b = local_world.get(perch_type)
                if b and b.y > entity.y + 1.5 and b.distance < 8:
                    return Move(b.x + 0.5, b.y + 1.0, b.z + 0.5, speed=spd), \
                           "Seeking roost"
            return Move(entity.x, entity.y, entity.z),"Settling down"

        if self._resting:
            self._resting = False
            self._sleeping = False
            return Move(entity.x, entity.y, entity.z),"BAWK! Good morning!"

        # Chase cats! (brave chicken is aggressive toward felines — daytime only)
        cat = local_world.get("base:cat", max_dist=8)
        if cat:
            return Move(cat.x, cat.y, cat.z, speed=spd * 1.5), "BAWK! Chasing cat!"

        # Follow player and occasionally lay eggs when near them
        player = local_world.nearest("base:player")
        if player:
            if player.distance < 3 and self._egg_cooldown <= 0 \
                    and random.random() < 0.15 and entity.hp > 2:
                self._egg_cooldown = 8.0
                return (Convert(from_item="hp", from_count=2,
                                     to_item="base:egg", to_count=1,
                                     convert_into=Ground()),
                        "*happy cluck* Laid an egg!")
            if player.distance > 3:
                return Move(player.x, player.y, player.z, speed=3.0), "Following player"
            return Move(entity.x, entity.y, entity.z),"Sitting by player"

        # Rejoin flock if same-type animals are nearby and we're far from them
        friends = local_world.all(entity.type_id)
        if friends and all(e.distance > 4 for e in friends):
            nearest = min(friends, key=lambda e: e.distance)
            return Move(nearest.x, nearest.y, nearest.z, speed=spd), "Rejoining flock"

        return Move(*self.wander_target(entity, radius=10), speed=spd), "Strutting around"
