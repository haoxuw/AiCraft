"""Idle — default behavior. Just stands still.

Assign this to any creature to make it do nothing.
When auto-pilot is enabled on a player, this is the default.
Edit this to make your character do things automatically!

Ideas to try:
  - Make the player follow the nearest pig
  - Auto-mine blocks in front of you
  - Patrol between two positions
  - Build a simple structure automatically
"""
from modcraft_engine import Move
from behavior_base import Behavior


class IdleBehavior(Behavior):

    def decide(self, entity, local_world):
        return Move(entity.x, entity.y, entity.z), "Idle"


# -- Example: uncomment to auto-follow nearest pig --
#
# from modcraft_engine import Move
#
# class IdleBehavior(Behavior):
#     def decide(self, entity, local_world):
#         pig = local_world.get("base:pig", max_dist=20)
#         if pig:
#             if pig.distance > 2:
#                 return Move(pig.x, pig.y, pig.z, speed=entity.walk_speed), \
#                        "Following pig (%.1fm)" % pig.distance
#         return Move(entity.x, entity.y, entity.z), "Looking for pigs..."
