"""pathfind.py — Direct-line movement helper.

Pathfinding has been intentionally removed: entities walk straight toward
their goal and may get stuck on walls. Collision avoidance is the concern
of server-side steering / physics, not decide().

API kept for backward compatibility with existing behaviors:
    Navigator.navigate(entity, local_world, goal, speed) → Move action or None
    Navigator.status  → str
    Navigator.reset()
    find_path(start, goal) → [] (stub)
"""

from __future__ import annotations
from typing import Optional
from modcraft_engine import Move


def find_path(start: tuple, goal: tuple, max_nodes: int = 0) -> list:
    return []


_ARRIVE_DIST = 1.2


class Navigator:
    def __init__(self):
        self._goal: Optional[tuple] = None
        self._status: str = "Idle"

    @property
    def status(self) -> str:
        return self._status

    def reset(self):
        self._goal = None
        self._status = "Idle"

    def navigate(self, entity, local_world, goal, speed: float = 2.0):
        if goal is None:
            self._status = "No goal"
            return None

        gx, gy, gz = float(goal[0]), float(goal[1]), float(goal[2])

        # Arrival check (horizontal)
        dx = entity.x - (gx + 0.5)
        dz = entity.z - (gz + 0.5)
        if (dx * dx + dz * dz) < _ARRIVE_DIST * _ARRIVE_DIST:
            self._goal = None
            self._status = "Arrived"
            return None

        self._goal = (gx, gy, gz)
        self._status = f"Walking to ({int(gx)},{int(gy)},{int(gz)})"
        return Move(gx + 0.5, gy + 1.0, gz + 0.5, speed)
