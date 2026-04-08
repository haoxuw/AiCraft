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
            return Idle(), "Idle (WASD)"

        goal_pos = (goal["x"], goal["y"], goal["z"])
        if not hasattr(self, '_logged_goal') or self._logged_goal != goal_pos:
            print(f"[player_nav] Goal received: ({goal_pos[0]:.1f}, {goal_pos[1]:.1f}, {goal_pos[2]:.1f})"
                  f" entity at ({entity.x:.1f}, {entity.y:.1f}, {entity.z:.1f})")
            self._logged_goal = goal_pos

        action = self._nav.navigate(entity, local_world, goal_pos,
                                    speed=entity.walk_speed)
        if action is not None:
            return action, self._nav.status

        print(f"[player_nav] Arrived at goal")
        return Idle(), "Arrived"
