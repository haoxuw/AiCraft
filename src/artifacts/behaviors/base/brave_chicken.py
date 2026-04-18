"""Brave Chicken — fearless hen. Rule-list version.

Follows players, fights cats (by staying near them without fleeing),
only fears dogs. Lays golden eggs when near player.

Priority:
  1. Dog nearby — flee (the one thing it fears)
  2. Night — go home, then roost
  3. Near player + random chance — lay an egg
  4. Near player — follow
  5. Drifted from flock — rejoin
  6. Default — wander
"""
from rules import RulesBehavior
from conditions_lib import (Threatened, IsEveningOrNight, FarFromHome,
                             FarFromFlock, NearPlayer, Chance, Always)
from actions_lib import Flee, GoHome, Rest, Rejoin, Wander, Follow, LayEgg


class BraveChickenBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            (Threatened(range=6, types=["dog"]),         Flee(distance=12, speed_mul=2.0,
                                                                    message="EEK! Dog!")),
            (IsEveningOrNight() & FarFromHome(radius=3),      GoHome(message="Heading home to roost...")),
            (IsEveningOrNight(),                              Rest(message="Roosting zzz", duration=120.0)),
            (NearPlayer(range=3),                        LayEgg(chance=0.15, cooldown=8.0,
                                                                      message="*happy cluck* Laid an egg!")),
            (NearPlayer(range=20),                       Follow(target_tag="playable",
                                                                      close_dist=3,
                                                                      at_target_msg="Sitting by player",
                                                                      following_msg="Following player")),
            (FarFromFlock(range=4),                           Rejoin(close_dist=4, message="Rejoining flock")),
            (Chance(0.50),                                    Rest(message="Strutting", duration=30.0)),
            (Always(),                                        Wander(radius=10, min_radius=3,
                                                                     plan_duration=30.0,
                                                                     message="Strutting around")),
        ]
