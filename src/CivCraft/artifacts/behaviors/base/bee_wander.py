"""Bee wander — hovers around flowers; despawns if none nearby.

Biases wander around the nearest `flower_red` annotation within 32 units;
if no flower exists for 2 minutes the bee despawns.

Uses the same Wander action as squirrel but with goalText "Flying" so the
client plays the flap animation (see entity_drawer.cpp::pickClip → "fly").
"""
from rules import RulesBehavior
from conditions_lib import Threatened, Always
from actions_lib import Flee, Wander


class BeeWanderBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            (Threatened(range=6), Flee()),
            # Long legs (plan_duration=6s) and a wide search so bees commit to
            # a flight across the meadow instead of redeciding every tick.
            (Always(),            Wander(target_block="flower_red",
                                         radius=12.0,
                                         search_radius=64.0,
                                         despawn_after=180.0,
                                         plan_duration=6.0,
                                         message="Flying")),
        ]
