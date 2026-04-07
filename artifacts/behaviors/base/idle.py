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
from modcraft_engine import Idle
from behavior_base import Behavior


class IdleBehavior(Behavior):

    def decide(self, entity, world):
        return Idle(), "Idle"


# -- Example: uncomment to auto-follow nearest pig --
#
# from modcraft_engine import Idle, Follow
#
# class IdleBehavior(Behavior):
#     def decide(self, entity, world):
#         pigs = [e for e in world["nearby"]
#                 if e["type_id"] == "base:pig" and e["distance"] < 20]
#         if pigs:
#             closest = min(pigs, key=lambda e: e["distance"])
#             return (Follow(closest["id"], speed=entity["walk_speed"], min_distance=2.0),
#                     "Following pig (%.1fm away)" % closest["distance"])
#         return Idle(), "Looking for pigs..."
