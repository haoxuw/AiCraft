"""Follow — trail a nearby entity of a configurable tag.

Generic companion/trailing behavior. Not hardcoded to the player — works
for dogs trailing humanoids, villagers following a chief, shepherd dogs
herding sheep, baby ducks chasing mama duck, etc.

Parameters (set via entity dict, all optional):
  target_tag    — tag to match on a nearby entity (default "humanoid")
  follow_range  — preset stopping distance: "close" | "near" | "far"
                  (default "near")
                    close ≈ 1.5 blocks — at-heels, like a clingy pet
                    near  ≈ 3.0 blocks — social spacing, default companion
                    far   ≈ 8.0 blocks — wide escort, like a shepherd dog
  follow_dist   — explicit distance override in blocks; wins over preset
  patrol_range  — max range to detect targets (default 12.0)
"""
import random
from modcraft_engine import Move
from behavior_base import Behavior
from local_world import SelfEntity, LocalWorld


_FOLLOW_PRESETS = {
    "close": 1.5,
    "near":  3.0,
    "far":   8.0,
}


class FollowBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._target_id = None

    def decide(self, entity: SelfEntity, local_world: LocalWorld):
        self._home = self.init_home(entity, self._home)

        target_tag   = entity.get("target_tag", "humanoid")
        preset       = entity.get("follow_range", "near")
        follow_dist  = float(entity.get("follow_dist",
                                        _FOLLOW_PRESETS.get(preset, 3.0)))
        patrol_range = float(entity.get("patrol_range", 12.0))
        spd          = entity.walk_speed

        # Find nearest entity matching target_tag within patrol range
        target = None
        for e in local_world.entities:
            if not e.has_tag(target_tag):
                continue
            if e.distance > patrol_range:
                continue
            if target is None or e.distance < target.distance:
                target = e

        if target is None:
            self._target_id = None
            dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])
            if dist_home > patrol_range:
                return Move(*self._home, speed=spd * 0.5), "Heading home"
            return Move(*self.wander_target(entity, radius=4), speed=spd * 0.4), "Sniffing around"

        self._target_id = target.id
        name = target.type.split(":")[1] if ":" in target.type else target.type

        if target.distance <= follow_dist:
            return Move(entity.x, entity.y, entity.z), f"Guarding {name}"

        return Move(target.x, target.y, target.z, speed=spd), f"Following {name}"
