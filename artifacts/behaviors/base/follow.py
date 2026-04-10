"""Follow — loyal companion that trails the nearest humanoid.

Dogs and similar pets use this behavior to stay near players and
villagers. When no humanoid is in range, the entity wanders idly.

Parameters (set via entity dict, all optional):
  follow_dist   — stop following when within this distance (default 3.0)
  patrol_range  — max range to detect humanoids (default 12.0)
"""
import random
from modcraft_engine import Move
from behavior_base import Behavior


class FollowBehavior(Behavior):

    def __init__(self):
        self._home = None
        self._target_id = None

    def decide(self, entity, local_world):
        self._home = self.init_home(entity, self._home)

        follow_dist  = float(entity.get("follow_dist", 3.0))
        patrol_range = float(entity.get("patrol_range", 12.0))
        spd          = entity.walk_speed

        # Find nearest humanoid within patrol range
        target = None
        for e in local_world.entities:
            if not e.has_tag("humanoid"):
                continue
            if e.distance > patrol_range:
                continue
            if target is None or e.distance < target.distance:
                target = e

        if target is None:
            self._target_id = None
            # Wander near home
            dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])
            if dist_home > patrol_range:
                return Move(*self._home, speed=spd * 0.5), "Heading home"
            return Move(*self.wander_target(entity, radius=4), speed=spd * 0.4), "Sniffing around"

        self._target_id = target.id
        name = target.type.split(":")[1] if ":" in target.type else target.type

        if target.distance <= follow_dist:
            return Move(entity.x, entity.y, entity.z), f"Guarding {name}"

        return Move(target.x, target.y, target.z, speed=spd), f"Following {name}"
