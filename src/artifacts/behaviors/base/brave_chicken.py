"""Brave Chicken — fearless hen. Rule-list version.

Follows players, fights cats (by staying near them without fleeing),
only fears dogs. Lays golden eggs when near player.

decide()/rules:
  1. Night — go home, then roost
  2. Near player + random chance — lay an egg
  3. Near player — follow
  4. Drifted from flock — rejoin
  5. Default — wander

react(signal):
  threat_nearby — flee only if the incoming threat is a dog. Other
                  species (cats, giants) are shrugged off.
"""
from rules import RulesBehavior
from conditions_lib import (IsEveningOrNight, FarFromHome,
                             FarFromFlock, NearPlayer, Chance, Always)
from actions_lib import Flee, GoHome, Rest, Rejoin, Wander, Follow, LayEgg
from signals import THREAT_NEARBY


class BraveChickenBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
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
        self._flee_dog = Flee(range=6, types=["dog"], distance=12,
                               speed_mul=2.0, message="EEK! Dog!")

    def react(self, e, w, signal):
        if signal.kind == THREAT_NEARBY:
            # Only dogs scare us — LayEgg handles "near player" flavor in decide().
            return self._flee_dog.run(e, w, self.ctx)
        return None
