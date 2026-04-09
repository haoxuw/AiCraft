"""player_nav — Navigation behavior for player entities.

Assigned to player characters so they can be RTS/RPG click-to-moved.
Idle when no goal is set (defers to GUI WASD control).
When a goal arrives via C_SET_GOAL, uses Navigator to pathfind + navigate.

NOTE: The class MUST inherit from Behavior (not shadow it).
"""

from pathfind import Navigator
from modcraft_engine import Move, Idle
from behavior_base import Behavior


class PlayerNavBehavior(Behavior):
    def __init__(self):
        self._nav = Navigator()
        self._logged_goal = None

    def decide(self, entity: "SelfEntity", local_world: "LocalWorld"):
        goal = getattr(local_world, 'goal', None)
        if goal is None:
            self._nav.reset()
            self._logged_goal = None
            return Idle(), "Idle (WASD)"

        goal_pos = (goal["x"], goal["y"], goal["z"])
        if self._logged_goal != goal_pos:
            print(f"[player_nav] Goal: ({goal_pos[0]:.1f}, {goal_pos[1]:.1f}, {goal_pos[2]:.1f})"
                  f" from ({entity.x:.1f}, {entity.y:.1f}, {entity.z:.1f})")
            self._logged_goal = goal_pos

        action = self._nav.navigate(entity, local_world, goal_pos,
                                    speed=entity.walk_speed)
        if action is not None:
            # Log the action type and target (once per new status)
            status = self._nav.status
            if not hasattr(self, '_last_status') or self._last_status != status:
                print(f"[player_nav] Action: {action.type} → status='{status}'")
                self._last_status = status
            return action, status

        print(f"[player_nav] Arrived")
        self._logged_goal = None
        return Idle(), "Arrived"
