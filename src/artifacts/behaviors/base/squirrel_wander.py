"""Squirrel wander — scampers around logs; despawns if none nearby.

Biases wander around the nearest `logs` block within 32 units; if no tree
is present for 5 minutes the squirrel despawns so the world stays tidy.
"""
from rules import RulesBehavior
from conditions_lib import Threatened, Always
from actions_lib import Flee, Wander


class SquirrelWanderBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            (Threatened(range=5), Flee()),
            # Block id is "logs" (plural) — see artifacts/blocks/base/terrain.py.
            # plan_duration=60 matches the decide floor so one scamper leg
            # is one commit cycle.
            (Always(),            Wander(target_block="logs",
                                         radius=12.0,
                                         min_radius=3.0,
                                         search_radius=32.0,
                                         despawn_after=300.0,
                                         plan_duration=60.0,
                                         message="Scampering")),
        ]
