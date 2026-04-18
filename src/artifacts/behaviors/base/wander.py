"""Wander — herd animal. Rule-list version.

Tuned so decides fire ~once/min per entity (C++ enforces a 60s floor in
DecisionQueue::kMinGapSec). Actions here pass explicit durations so the
animal commits to its choice for a visible amount of time instead of
churning through decide() every tick.

Priority:
  1. Flee threats (any non-same-species Living within flee_range)
  2. Evening/night — head home, then sleep for a long chunk
  3. Too far from home — return
  4. Rejoin herd if drifted
  5. 50% graze for 30s
  6. 20% short wander 3–10 blocks, commit 30s
  7. Otherwise rest for 30s
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
            (IsEveningOrNight(),                            Rest(message="Sleeping zzz", duration=120.0)),
            (FarFromHome(radius=35),                        GoHome(speed_mul=0.8, message="Wandering back home")),
            (FarFromFlock(range=6),                         Rejoin(close_dist=6, message="Joining herd")),
            (Chance(0.50),                                  Rest(message="Grazing", duration=30.0)),
            (Chance(0.40),                                  Wander(radius=10.0, min_radius=3.0,
                                                                   plan_duration=30.0)),
            (Always(),                                      Rest(message="Idle", duration=30.0)),
        ]
