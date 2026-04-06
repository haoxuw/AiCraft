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

from modcraft.api import Idle, Follow, MoveTo

goal = "Idle (auto-pilot off)"


def decide(self, world):
    """Called 4 times per second. Return what to do next."""
    self.goal = "Idle (auto-pilot off)"
    return Idle()


# -- Example: uncomment to auto-follow nearest pig --
#
# def decide(self, world):
#     pigs = world.get_entities_in_radius(
#         self.pos, 20.0, type="base:pig"
#     )
#     if pigs:
#         closest = min(pigs, key=lambda e: e.distance)
#         self.goal = f"Following pig ({closest.distance:.1f}m away)"
#         return Follow(closest.id, speed=self.walk_speed, min_distance=2.0)
#
#     self.goal = "Looking for pigs..."
#     return Idle()
