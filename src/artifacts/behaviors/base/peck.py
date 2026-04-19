"""Peck — timid ground-feeding bird. Rule-list version.

decide()/rules (normal life):
  1. Evening/night — roost at home
  2. Too far from home — return
  3. Flock if drifted
  4. Peck ground / wander

react(signal) (event-driven, rate-limited to 1/0.5s, anchor-immune):
  threat_nearby — lay egg (cooldown-gated) then flee. Flee's anchor keeps
                  the server re-aiming away from the threat each tick
                  without a re-decide.
"""
from rules import RulesBehavior
from conditions_lib import (IsEveningOrNight, FarFromHome,
                             FarFromFlock, Chance, Always)
from actions_lib import Flee, GoHome, Rest, Rejoin, Wander, LayEgg
from signals import THREAT_NEARBY


class PeckBehavior(RulesBehavior):
    def __init__(self):
        super().__init__()
        self.rules = [
            (IsEveningOrNight() & FarFromHome(radius=3),     GoHome(message="Heading home to roost...")),
            (IsEveningOrNight(),                             Rest(message="Roosting zzz", duration=120.0)),
            (FarFromHome(radius=25),                         GoHome(speed_mul=0.8, message="Wandering back home")),
            (FarFromFlock(range=4),                          Rejoin(close_dist=4, message="Rejoining flock")),
            (Chance(0.50),                                   Rest(message="Foraging", duration=30.0)),
            (Chance(0.40),                                   Wander(radius=8, min_radius=2, speed_mul=0.6,
                                                                    plan_duration=30.0, message="Foraging")),
            (Always(),                                       Rest(message="Idle", duration=30.0)),
        ]
        # Instanced once so cooldown + hp-gate state persists across reacts.
        self._lay_egg = LayEgg(chance=0.80, cooldown=10.0,
                                message="BAWK!! *lays egg!*")
        self._flee    = Flee(range=4, speed_mul=2.4, distance=12,
                              message="BAWK!! Scattering!")

    def react(self, e, w, signal):
        if signal.kind == THREAT_NEARBY:
            # Keep LayEgg's timer ticking whether or not the egg lays.
            self._lay_egg.tick(w.dt)
            egg = self._lay_egg.run(e, w, self.ctx)
            if egg is not None:
                return egg
            return self._flee.run(e, w, self.ctx)
        return None
