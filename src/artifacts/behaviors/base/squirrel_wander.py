"""Squirrel wander — scampers around logs; despawns if none nearby.

Biases wander around the nearest `logs` block within 32 units; if no tree
is present for 5 minutes the squirrel despawns so the world stays tidy.
"""
from rules import RulesBehavior
from conditions_lib import Always
from actions_lib import Flee, Wander
from signals import THREAT_NEARBY


class SquirrelWanderBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
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
        self._flee = Flee(range=5)

    def react(self, e, w, signal):
        if signal.kind == THREAT_NEARBY:
            return self._flee.run(e, w, self.ctx)
        return None
