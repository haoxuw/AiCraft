"""Flyer wander — airborne variant of wander.

Same rule list as wander, but the wander action emits goalText "Flying"
instead of "Wandering" so the client plays the flap animation (see
entity_drawer.cpp::pickClip → "fly" clip in owl.py).

Full take-off/landing state machine is a future step; for now flyers
just hover + flap. gravity_scale=0 in C++ builtin.cpp keeps them airborne.
"""
from rules import RulesBehavior
from conditions_lib import Threatened, Always
from actions_lib import Flee, Wander


class FlyerWanderBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            (Threatened(range=6), Flee()),
            (Always(),            Wander(radius=10, message="Flying")),
        ]
