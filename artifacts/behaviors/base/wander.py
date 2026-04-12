"""Wander — herd animal. Rule-list version.

Priority:
  1. Evening/night — head home, then rest
  2. Too far from home — return
  3. Flee threats (any non-same-species Living within flee_range)
  4. Rejoin herd if drifted
  5. Graze sometimes
  6. Wander

Entity props (optional):
  flee_range   default 5
  group_range  default 6
  graze_chance default 0.25
  home_radius  default 35
"""
from rules import RulesBehavior
from conditions_lib import (Threatened, IsEveningOrNight, FarFromHome,
                             FarFromFlock, Chance, Always)
from actions_lib import Flee, GoHome, Rest, Rejoin, Wander


class WanderBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            (Threatened(range=5),                           Flee()),
            (IsEveningOrNight() & FarFromHome(radius=3),    GoHome(message="Heading home...")),
            (IsEveningOrNight(),                            Rest(message="Sleeping zzz", duration=3.0)),
            (FarFromHome(radius=35),                        GoHome(speed_mul=0.8, message="Wandering back home")),
            (FarFromFlock(range=6),                         Rejoin(close_dist=6, message="Joining herd")),
            (Chance(0.25),                                  Rest(message="Grazing", duration=3.0)),
            (Always(),                                      Wander(radius=12)),
        ]
