"""Squirrel wander — scampers around logs; despawns if none nearby.

Biases wander around the nearest `log` block within 32 units; if no log is
present for 2 minutes the squirrel despawns so the world stays tidy.
"""
from rules import RulesBehavior
from conditions_lib import Threatened, Always
from actions_lib import Flee, Wander


class SquirrelWanderBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            (Threatened(range=5), Flee()),
            (Always(),            Wander(target_block="log",
                                         search_radius=32.0,
                                         despawn_after=120.0,
                                         message="Scampering")),
        ]
