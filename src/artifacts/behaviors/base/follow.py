"""Follow — trail a nearby entity by tag. Rule-list version.

Generic companion. Works for dogs following humanoids, baby animals
following mama, shepherd dogs herding, etc.

Entity props (optional):
  target_tag    match tag on nearby entity (default "humanoid")
  follow_range  preset "close"|"near"|"far" (default "near")
  follow_dist   explicit override in blocks — wins over preset
  patrol_range  max detection range (default 12)
"""
from rules import RulesBehavior
from conditions_lib import Always, FarFromHome
from actions_lib import Follow, GoHome, Wander


_PRESETS = {"close": 1.5, "near": 3.0, "far": 8.0}


class FollowBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        # Param reads happen at __init__ time, so each entity instance
        # captures its own prop values. This is fine because AgentClient
        # creates one Behavior instance per entity.
        # (`entity.get(...)` needs a live entity — resolved in decide_plan().)
        self.rules = []

    def decide_plan(self, entity, world):
        # Lazy rule build so we can read entity props.
        if not self.rules:
            preset = entity.get("follow_range", "near")
            dist   = float(entity.get("follow_dist", _PRESETS.get(preset, 3.0)))
            patrol = float(entity.get("patrol_range", 12.0))
            tag    = entity.get("target_tag", "humanoid")
            # Rule order matters: Follow.run returns None when no target is
            # in range, so it falls through. Putting FarFromHome/Wander BEFORE
            # Follow would short-circuit target acquisition; putting Always
            # before FarFromHome (old ordering) made the patrol-home rule
            # dead code.
            self.rules = [
                (Always(),                   Follow(target_tag=tag,
                                                    close_dist=dist,
                                                    range=patrol,
                                                    at_target_msg="Guarding")),
                (FarFromHome(radius=patrol), GoHome(speed_mul=0.5, message="Heading home")),
                (Always(),                   Wander(radius=6, min_radius=2,
                                                    speed_mul=0.4,
                                                    plan_duration=30.0,
                                                    message="Sniffing around")),
            ]
        return super().decide_plan(entity, world)
