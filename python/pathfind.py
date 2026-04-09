"""pathfind.py — Block-grid A* pathfinding helper for entity behaviors.

All pathfinding logic lives here in Python. The server only validates
ActionProposals (speed, reachability); it has no pathfinding code.

Usage in a behavior
───────────────────
    from pathfind import Navigator

    class MyBehavior(Behavior):
        def __init__(self):
            self._nav = Navigator()

        def decide(self, entity, local_world):
            goal = (target_x, target_y, target_z)
            action = self._nav.navigate(entity, local_world, goal, speed=entity.walk_speed)
            if action:
                return action, self._nav.status
            return Move(entity.x, entity.y, entity.z), "Arrived"

API
───
Navigator.navigate(entity, local_world, goal, speed=2.0) → action or None
    Returns a MoveTo/Interact action to take this tick, or None when arrived.
    Call every tick. Re-plans automatically when goal changes or path is blocked.

Navigator.reset()
    Force re-plan on next navigate() call.

Navigator.status  → str
    Human-readable goal string (e.g. "Walking to (12, 5, 34)").

find_path(local_world, start_xyz, goal_xyz, max_nodes=2000) → list[tuple] or []
    Low-level A* — returns [(x,y,z), ...] waypoints from start to goal,
    or [] if unreachable within max_nodes. Uses local_world.get_block() to test solidity.
"""

from __future__ import annotations
from typing import Optional
from modcraft_engine import get_block, Move, Interact
import heapq
import math

# ── Block type helpers ────────────────────────────────────────────────────────

_SOLID_CACHE: dict[str, bool] = {}

# Blocks that are solid for movement purposes (entity cannot pass through)
_NON_SOLID = frozenset({
    "base:air", "base:water", "base:door", "base:door_open",
    "base:wheat", "base:farmland", "base:leaves",
    "",
})

# Blocks that are doors (passable but require Interact first)
_DOOR_TYPES = frozenset({"base:door"})


def _is_solid(type_id: str) -> bool:
    """Return True if a block type is solid (entity cannot occupy its space)."""
    if type_id in _SOLID_CACHE:
        return _SOLID_CACHE[type_id]
    result = type_id not in _NON_SOLID and not type_id.endswith(":air")
    _SOLID_CACHE[type_id] = result
    return result


def _is_door(type_id: str) -> bool:
    return type_id in _DOOR_TYPES


def _block(x: int, y: int, z: int) -> str:
    """Query block type at local_world position using the C++ chunk cache."""
    return get_block(x, y, z)


def _is_standable(x: int, y: int, z: int) -> bool:
    """True if an entity can stand with feet at (x, y, z).

    Requires:
      - solid block at y-1 (floor)
      - non-solid at y and y+1 (2-block entity body clearance)
      - non-solid or door at y (passable even if door, handled separately)
    """
    floor = _block(x, y - 1, z)
    if not _is_solid(floor):
        return False
    body0 = _block(x, y, z)
    body1 = _block(x, y + 1, z)
    # Body blocks must be passable (non-solid, or a door we can open)
    ok0 = not _is_solid(body0) or _is_door(body0)
    ok1 = not _is_solid(body1) or _is_door(body1)
    return ok0 and ok1


# ── A* implementation ─────────────────────────────────────────────────────────

# Move cost weights
_MOVE_COST  = 1.0   # flat step
_STEP_COST  = 1.5   # step-up (slightly penalised to prefer flat paths)
_FALL_COST  = 0.5   # falling is cheap
_DOOR_COST  = 3.0   # door adds penalty to prefer routes without doors
_MAX_FALL   = 4     # maximum consecutive downward blocks before giving up


def _heuristic(a: tuple, b: tuple) -> float:
    return abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2])


def _neighbors(x: int, y: int, z: int):
    """Yield (nx, ny, nz, cost, is_door) for all reachable neighbours."""
    for dx, dz in ((1,0),(-1,0),(0,1),(0,-1)):
        nx, nz = x + dx, z + dz

        # ── Walk flat ────────────────────────────────────────────────
        if _is_standable(nx, y, nz):
            body = _block(nx, y, nz)
            door = _is_door(body) or _is_door(_block(nx, y+1, nz))
            yield (nx, y, nz, _MOVE_COST + (_DOOR_COST if door else 0), door)
            continue

        # ── Step up (one block) ───────────────────────────────────────
        if _is_standable(nx, y + 1, nz):
            # Need 3 blocks of clearance above current position to step up
            if not _is_solid(_block(x, y + 2, z)):
                body = _block(nx, y+1, nz)
                door = _is_door(body) or _is_door(_block(nx, y+2, nz))
                yield (nx, y+1, nz, _STEP_COST + (_DOOR_COST if door else 0), door)
                continue

        # ── Fall down (up to MAX_FALL blocks) ────────────────────────
        for drop in range(1, _MAX_FALL + 1):
            ny = y - drop
            if _is_solid(_block(nx, ny - 1, nz)):
                # Landed — check body clearance at landing position
                if _is_standable(nx, ny, nz):
                    body = _block(nx, ny, nz)
                    door = _is_door(body)
                    yield (nx, ny, nz, _FALL_COST * drop + (_DOOR_COST if door else 0), door)
                break
            # Still falling — keep dropping


def find_path(start: tuple, goal: tuple, max_nodes: int = 2000) -> list:
    """A* path from start to goal on the block grid.

    Parameters
    ----------
    start, goal : (x, y, z) integers — entity feet position
    max_nodes   : search budget (higher = longer paths, more CPU)

    Returns
    -------
    List of (x, y, z) waypoints from start+1 to goal (inclusive),
    or [] if unreachable within budget.
    """
    start = (int(start[0]), int(start[1]), int(start[2]))
    goal  = (int(goal[0]),  int(goal[1]),  int(goal[2]))

    if start == goal:
        return []

    # open heap: (f, g, node, parent_node)
    open_heap: list = []
    heapq.heappush(open_heap, (0.0, 0.0, start, None))

    came_from: dict  = {}   # node → parent
    g_score:   dict  = {start: 0.0}
    closed:    set   = set()
    expanded = 0

    while open_heap and expanded < max_nodes:
        f, g, current, parent = heapq.heappop(open_heap)
        if current in closed:
            continue
        closed.add(current)
        came_from[current] = parent
        expanded += 1

        if current == goal:
            # Reconstruct path (skip start, include goal)
            path = []
            node = goal
            while node is not None and node != start:
                path.append(node)
                node = came_from.get(node)
            path.reverse()
            return path

        cx, cy, cz = current
        for nx, ny, nz, cost, _ in _neighbors(cx, cy, cz):
            nb = (nx, ny, nz)
            if nb in closed:
                continue
            tentative_g = g + cost
            if tentative_g < g_score.get(nb, math.inf):
                g_score[nb] = tentative_g
                h = _heuristic(nb, goal)
                heapq.heappush(open_heap, (tentative_g + h, tentative_g, nb, current))

    return []  # unreachable within budget


# ── Navigator — stateful path-follower ───────────────────────────────────────

_ARRIVE_DIST   = 1.2   # horizontal distance to consider a waypoint reached
_REPLANN_DIST  = 4.0   # re-plan if entity is this far from the expected waypoint
_STUCK_TIMEOUT = 6.0   # seconds without progress before re-planning


class Navigator:
    """Stateful path-follower. One Navigator per behavior instance.

    Call navigate() every decide() tick. It:
      - Runs A* on the first call (or when goal changes / entity is stuck).
      - Steps through waypoints each tick, emitting MoveTo actions.
      - Emits Interact when the next waypoint is a door.
      - Returns None when the entity has arrived.
    """

    def __init__(self):
        self._path:    list  = []   # remaining waypoints [(x,y,z), ...]
        self._goal:    Optional[tuple] = None
        self._status:  str   = "Idle"
        self._door_wait = 0.0       # ticks to wait after opening a door
        self._stuck_pos:   Optional[tuple] = None
        self._stuck_timer: float = 0.0

    @property
    def status(self) -> str:
        return self._status

    def reset(self):
        """Force re-plan on next navigate() call."""
        self._path = []
        self._goal = None
        self._stuck_pos = None
        self._stuck_timer = 0.0

    def navigate(self, entity, local_world, goal, speed: float = 2.0):
        """Return an action to take this tick toward goal, or None if arrived.

        Parameters
        ----------
        entity : SelfEntity
        local_world  : LocalWorld
        goal   : (x, y, z) float or int — destination (block feet position)
        speed  : movement speed passed to MoveTo
        """
        if goal is None:
            self._status = "No goal"
            return None

        gx, gy, gz = int(goal[0]), int(goal[1]), int(goal[2])
        goal_i = (gx, gy, gz)

        ex, ey, ez = int(entity.x), int(entity.y), int(entity.z)
        start_i = (ex, ey, ez)

        # ── Arrival check ─────────────────────────────────────────────────
        dx = entity.x - (gx + 0.5)
        dz = entity.z - (gz + 0.5)
        if (dx*dx + dz*dz) < _ARRIVE_DIST * _ARRIVE_DIST:
            self._path = []
            self._goal = None
            self._status = "Arrived"
            return None

        # ── Re-plan when goal changes ──────────────────────────────────────
        if goal_i != self._goal:
            self._goal = goal_i
            self._path = find_path(start_i, goal_i)
            self._stuck_pos = None
            self._stuck_timer = 0.0
            print(f"[Navigator] A* from ({ex},{ey},{ez}) → ({gx},{gy},{gz}): "
                  f"{len(self._path)} waypoints")
            if self._path:
                print(f"[Navigator]   first={self._path[0]} last={self._path[-1]}")
            if not self._path:
                self._status = f"No path to ({gx},{gy},{gz})"
                print(f"[Navigator] No path found — falling back to direct move")
                return Move(gx + 0.5, gy + 1.0, gz + 0.5, speed)

        # ── Stuck detection — re-plan if entity hasn't moved ──────────────
        dt = getattr(local_world, 'dt', 0.25)
        pos2 = (entity.x, entity.z)
        if self._stuck_pos is None:
            self._stuck_pos  = pos2
            self._stuck_timer = 0.0
        else:
            ddx = pos2[0] - self._stuck_pos[0]
            ddz = pos2[1] - self._stuck_pos[1]
            moved = (ddx*ddx + ddz*ddz) ** 0.5
            self._stuck_timer += dt
            if moved > 1.0:
                self._stuck_pos   = pos2
                self._stuck_timer = 0.0
            elif self._stuck_timer > _STUCK_TIMEOUT:
                # Re-plan from current position
                self._path = find_path(start_i, goal_i)
                self._stuck_pos   = pos2
                self._stuck_timer = 0.0
                if not self._path:
                    self._status = f"Stuck, no path to ({gx},{gy},{gz})"
                    return Move(gx + 0.5, gy + 1.0, gz + 0.5, speed)

        # ── Door waiting ───────────────────────────────────────────────────
        if self._door_wait > 0:
            self._door_wait -= dt
            self._status = "Opening door..."
            return Move(entity.x, entity.y, entity.z, speed)

        # ── Pop waypoints that are already behind us ───────────────────────
        while self._path:
            wx, wy, wz = self._path[0]
            ddx = entity.x - (wx + 0.5)
            ddz = entity.z - (wz + 0.5)
            if (ddx*ddx + ddz*ddz) < _ARRIVE_DIST * _ARRIVE_DIST:
                self._path.pop(0)
            else:
                break

        if not self._path:
            # Path exhausted, try direct move for the last stretch
            self._status = f"Near ({gx},{gy},{gz})"
            return Move(gx + 0.5, gy + 1.0, gz + 0.5, speed)

        # ── Next waypoint ─────────────────────────────────────────────────
        wx, wy, wz = self._path[0]

        # Check if next waypoint is a door — open it first
        body0 = _block(wx, wy, wz)
        body1 = _block(wx, wy + 1, wz)
        if _is_door(body0) or _is_door(body1):
            door_y = wy if _is_door(body0) else wy + 1
            self._door_wait = 0.5   # wait 0.5 s for server to open door
            self._status = "Opening door"
            return Interact(wx, door_y, wz)

        self._status = f"Walking to ({wx},{wy},{wz})"
        return Move(wx + 0.5, wy + 1.0, wz + 0.5, speed)
