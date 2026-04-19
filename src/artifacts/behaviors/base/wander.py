"""Wander — herd animal. Rule-list version.

decide()/rules:
  1. Evening/night — head home, then sleep
  2. Too far from home — return
  3. Rejoin herd if drifted
  4. 50% graze for 30s
  5. 40% short wander 3–10 blocks, commit 30s
  6. Otherwise rest for 30s

react(signal):
  threat_nearby — flee the nearest non-same-species living. Flee's anchor
                  keeps the server scaring us away each tick without a
                  re-decide.
"""
from rules import RulesBehavior
from conditions_lib import (IsEveningOrNight, FarFromHome,
                             FarFromFlock, Chance, Always)
from actions_lib import Flee, GoHome, Rest, Rejoin, Wander
from signals import THREAT_NEARBY


class WanderBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            (IsEveningOrNight() & FarFromHome(radius=3),    GoHome(message="Heading home...")),
            (IsEveningOrNight(),                            Rest(message="Sleeping zzz", duration=120.0)),
            (FarFromHome(radius=35),                        GoHome(speed_mul=0.8, message="Wandering back home")),
            (FarFromFlock(range=6),                         Rejoin(close_dist=6, message="Joining herd")),
            (Chance(0.50),                                  Rest(message="Grazing", duration=30.0)),
            (Chance(0.40),                                  Wander(radius=10.0, min_radius=3.0,
                                                                   plan_duration=30.0)),
            (Always(),                                      Rest(message="Idle", duration=30.0)),
        ]
        self._flee = Flee(range=5)

    def react(self, e, w, signal):
        if signal.kind == THREAT_NEARBY:
            return self._flee.run(e, w, self.ctx)
        return None
