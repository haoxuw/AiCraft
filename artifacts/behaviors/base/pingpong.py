"""Ping-pong — walk back and forth between two points for animation testing.

Walks 10 blocks east, then 10 blocks west, repeat forever.
No pathfinding, no scanning, no complex decisions — just Move.
"""
from modcraft_engine import Move
from behavior_base import Behavior
from local_world import SelfEntity, LocalWorld
from stats import stats


class PingpongBehavior(Behavior):

    def __init__(self):
        self._origin = None
        self._going_east = True

    def decide(self, entity: SelfEntity, local_world: LocalWorld):
        stats.inc("decide", entity.type)
        spd = entity.walk_speed

        if self._origin is None:
            self._origin = (entity.x, entity.y, entity.z)

        ox, oy, oz = self._origin
        east_target = (ox + 10, oy, oz)
        west_target = (ox - 10, oy, oz)

        if self._going_east:
            dist = abs(entity.x - east_target[0])
            if dist < 1.0:
                self._going_east = False
            return Move(*east_target, speed=spd), "Walking east"
        else:
            dist = abs(entity.x - west_target[0])
            if dist < 1.0:
                self._going_east = True
            return Move(*west_target, speed=spd), "Walking west"
