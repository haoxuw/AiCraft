"""Peck — timid ground-feeding bird. Rule-list version.

Priority:
  1. Threatened — try to lay egg (80% chance, 10s cooldown), else flee
  2. Evening/night — roost at home
  3. Too far from home — return
  4. Flock if drifted
  5. Peck ground / wander

Entity props (optional):
  scatter_range default 4
  peck_chance   default 0.45
  home_radius   default 25
  egg_chance    default 0.80
  egg_cooldown  default 10
"""
from rules import RulesBehavior
from conditions_lib import (Threatened, IsEveningOrNight, FarFromHome,
                             FarFromFlock, Chance, Always)
from actions_lib import Flee, GoHome, Rest, Rejoin, Wander, LayEgg


class PeckBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            # Startled: drop an egg on the way out if cooldown is ready.
            # LayEgg returns None when cooldown/chance/hp checks fail,
            # so the next rule (Flee) fires instead.
            (Threatened(range=4),                            LayEgg(chance=0.80, cooldown=10.0,
                                                                    message="BAWK!! *lays egg!*")),
            (Threatened(range=4),                            Flee(speed_mul=2.4, distance=12,
                                                                  message="BAWK!! Scattering!")),
            (IsEveningOrNight() & FarFromHome(radius=3),     GoHome(message="Heading home to roost...")),
            (IsEveningOrNight(),                             Rest(message="Roosting zzz")),
            (FarFromHome(radius=25),                         GoHome(speed_mul=0.8, message="Wandering back home")),
            (FarFromFlock(range=4),                          Rejoin(close_dist=4, message="Rejoining flock")),
            (Chance(0.45),                                   Rest(message="Foraging", duration=1.0)),
            (Always(),                                       Wander(radius=8, speed_mul=0.6, message="Foraging")),
        ]
