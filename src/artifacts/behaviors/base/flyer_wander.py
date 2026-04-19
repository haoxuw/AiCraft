"""Flyer wander — shared hover/fly behavior for airborne creatures.

Biases the wander around the nearest `flower_red` annotation within 128
blocks; falls back to wandering around self when no flower is reachable.
goalText "Flying" makes the client play the flap animation
(entity_drawer.cpp::pickClip → "fly").

Used by: bee, owl. Bees gravitate toward flowers for pollination flavor;
owls will orbit anything colourful too — harmless, and keeps the behavior
single-source.

gravity_scale=0 on each living def keeps them airborne; the wander action
just picks XZ waypoints.
"""
from rules import RulesBehavior
from conditions_lib import Always
from actions_lib import Flee, Wander
from signals import THREAT_NEARBY


class FlyerWanderBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            # plan_duration=60 matches the C++ decide floor (kMinGapSec) so
            # one flight leg == one decide cycle; radius/search_radius are
            # sized for flyers to cross a meadow per commit.
            (Always(),            Wander(target_block="flower_red",
                                         radius=40.0,
                                         min_radius=10.0,
                                         search_radius=128.0,
                                         plan_duration=60.0,
                                         message="Flying")),
        ]
        self._flee = Flee(range=6)

    def react(self, e, w, signal):
        if signal.kind == THREAT_NEARBY:
            return self._flee.run(e, w, self.ctx)
        return None
