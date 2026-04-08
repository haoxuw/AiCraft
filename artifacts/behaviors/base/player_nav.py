"""player_nav — Navigation behavior for player entities.

Assigned to player characters so they can be RTS/RPG click-to-moved.
Idle when no goal is set (defers to GUI WASD control).
When a goal arrives via C_SET_GOAL, uses Navigator to pathfind + navigate.
"""

from pathfind import Navigator
from modcraft_engine import Move, Idle

class Behavior:
    def __init__(self):
        self._nav = Navigator()

    def decide(self, entity, local_world):
        goal = local_world.goal
        if goal is None:
            self._nav.reset()
            return Idle(), "Idle (WASD)"

        goal_pos = (goal["x"], goal["y"], goal["z"])
        action = self._nav.navigate(entity, local_world, goal_pos,
                                    speed=entity.walk_speed)
        if action is not None:
            return action, self._nav.status

        # Arrived
        return Idle(), "Arrived"
