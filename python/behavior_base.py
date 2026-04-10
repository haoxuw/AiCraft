"""behavior_base.py — Base class for all entity AI behaviors.

Every behavior inherits from Behavior and overrides decide().

Contract
--------
    decide(entity: SelfEntity, local_world: LocalWorld) → (action, goal_str)

    action    — one of the four server-accepted primitives, constructed via the
                modcraft_engine helpers: Move(entity.x, entity.y, entity.z), Move(x,y,z,speed),
                Convert(…), StoreItem(id), PickupItem(id), DropItem(…),
                BreakBlock(x,y,z), Interact(x,y,z).
    goal_str  — non-empty human-readable string (shown above entity's head
                and in the right-click inspect panel). Never return "".

High-level strategies (Follow, Flee, Wander) are NOT action types.
They are Python helpers on this Behavior base class that compute a target
position and return Move.  See wander_target() and flee_pos() below.

World model
-----------
    LocalWorld  — agent-side cached subset of GlobalWorld. May be stale;
                  server validates all actions and rejects impossible ones.
    SelfEntity  — full state of the entity running this behavior.

    See python/local_world.py for the complete API.

Example
-------
    from modcraft_engine import Move
    from behavior_base import Behavior

    class GuardBehavior(Behavior):
        def __init__(self):
            self._home = None

        def decide(self, entity, local_world):
            self._home = self.init_home(entity, self._home)
            threat = local_world.nearest("hostile", max_dist=8)
            if threat:
                return Move(*self.flee_pos(entity, threat), speed=entity.walk_speed * 1.5), "Retreating!"
            return Move(entity.x, entity.y, entity.z), "Standing guard"
"""

import math
import random as _random
from local_world import LocalWorld, SelfEntity, BlockView, EntityView
from stats import stats


class Behavior:
    """Abstract base for all entity behaviors.

    Subclasses must implement decide(entity, local_world) → (action, goal_str).
    """

    def decide(self, entity: SelfEntity, local_world: LocalWorld) -> tuple:
        """Called at ~4 Hz. Must return (action, goal: str).

        Parameters
        ----------
        entity : SelfEntity
            Live entity state. Read-only. Access via attributes:
            entity.x, entity.y, entity.z, entity.hp, entity.walk_speed,
            entity.on_ground, entity.inventory.count("base:logs"),
            entity.get("work_radius", 80.0)   ← server-assigned custom props

        local_world : LocalWorld
            Spatial snapshot of what this agent can currently perceive.
            local_world.get("base:logs")           ← nearest block/entity of type
            local_world.get("base:logs", max_dist=40)
            local_world.all("base:logs")           ← all, nearest-first
            local_world.nearest("player")           ← nearest entity by category
            local_world.time                        ← 0.0–1.0 day fraction
            local_world.dt                          ← frame delta seconds
        """
        raise NotImplementedError(
            f"{type(self).__name__} must implement decide(entity, local_world) → (action, goal)"
        )

    # ── Time-of-day helpers ───────────────────────────────────────────────────

    @staticmethod
    def is_night(local_world: LocalWorld) -> bool:
        """True during night: midnight to dawn (0%–25%)."""
        return local_world.time < 0.25

    @staticmethod
    def is_morning(local_world: LocalWorld) -> bool:
        """True during morning: dawn to noon (25%–50%)."""
        return 0.25 <= local_world.time < 0.50

    @staticmethod
    def is_afternoon(local_world: LocalWorld) -> bool:
        """True during afternoon: noon to dusk (50%–75%)."""
        return 0.50 <= local_world.time < 0.75

    @staticmethod
    def is_evening(local_world: LocalWorld) -> bool:
        """True during evening: dusk to midnight (75%–100%)."""
        return local_world.time >= 0.75

    # ── Distance helpers ──────────────────────────────────────────────────────

    @staticmethod
    def dist2d(ax: float, az: float, bx: float, bz: float) -> float:
        """Horizontal (XZ) distance between two positions."""
        dx, dz = ax - bx, az - bz
        return (dx * dx + dz * dz) ** 0.5

    # ── Proximity helpers ─────────────────────────────────────────────────────

    @staticmethod
    def is_near(entity: SelfEntity, pos, threshold: float = 2.5) -> bool:
        """True if entity is within threshold horizontal distance of pos.

        pos may be a BlockView, EntityView, (x, y, z) tuple, or plain dict.
        """
        if isinstance(pos, (BlockView, EntityView)):
            tx, tz = pos.x, pos.z
        elif isinstance(pos, dict):
            tx, tz = pos["x"], pos["z"]
        else:
            tx, tz = pos[0], pos[2]
        dx = entity.x - tx
        dz = entity.z - tz
        return (dx * dx + dz * dz) < threshold * threshold

    # ── Home / chest helpers ──────────────────────────────────────────────────

    def init_home(self, entity: SelfEntity, home: tuple) -> tuple:
        """Return home (x, y, z), initialising from server props if not set yet.

        Uses home_x / home_y / home_z server-assigned props when present,
        otherwise records the entity's current spawn position as home.
        """
        if home is not None:
            return home
        hx = entity.get("home_x")
        hy = entity.get("home_y")
        hz = entity.get("home_z")
        if hx is not None and hz is not None:
            return (float(hx), float(hy) if hy is not None else entity.y, float(hz))
        return (entity.x, entity.y, entity.z)

    @staticmethod
    def get_chest(entity: SelfEntity, home: tuple) -> tuple:
        """Return (x, y, z) of the entity's assigned chest, or fall back to home."""
        cx = entity.get("chest_x")
        cy = entity.get("chest_y")
        cz = entity.get("chest_z")
        if cx is not None and cz is not None:
            return (float(cx), float(cy) if cy is not None else home[1], float(cz))
        return home

    # ── Stuck detection ───────────────────────────────────────────────────────

    def check_stuck(self, entity: SelfEntity, dt: float,
                    move_threshold: float = 1.0, timeout: float = 6.0) -> bool:
        """Return True when the entity hasn't moved enough in the last timeout seconds.

        Call every tick while navigating. Resets automatically when movement
        is detected. Use reset_stuck() to restart after a goal change.
        """
        pos = (entity.x, entity.z)
        if not hasattr(self, "_stuck_ref") or self._stuck_ref is None:
            self._stuck_ref     = pos
            self._stuck_elapsed = 0.0

        self._stuck_elapsed += dt
        if self._stuck_elapsed >= timeout:
            moved = self.dist2d(pos[0], pos[1], self._stuck_ref[0], self._stuck_ref[1])
            if moved < move_threshold:
                return True
            self._stuck_ref     = pos
            self._stuck_elapsed = 0.0
        return False

    def reset_stuck(self):
        """Reset stuck tracking (call after a deliberate goal change)."""
        self._stuck_ref     = None
        self._stuck_elapsed = 0.0

    # ── Movement helpers (return (x, y, z) — wrap with Move) ───────────────

    @staticmethod
    def wander_target(entity: SelfEntity, radius: float = 8.0) -> tuple:
        """Pick a random walk target within radius of entity's current position.

        Usage:
            tx, ty, tz = self.wander_target(entity, radius=10)
            return Move(tx, ty, tz, speed=spd), "Wandering"
        """
        angle = _random.uniform(0, 2 * math.pi)
        dist  = _random.uniform(radius * 0.3, radius)
        return (entity.x + math.cos(angle) * dist,
                entity.y,
                entity.z + math.sin(angle) * dist)

    @staticmethod
    def flee_pos(entity: SelfEntity, threat, distance: float = 12.0) -> tuple:
        """Compute a position to flee to, moving directly away from threat.

        threat may be a BlockView, EntityView, or any object with .x and .z.

        Usage:
            return Move(*self.flee_pos(entity, threat), speed=spd * 1.8), "Fleeing!"
        """
        dx = entity.x - threat.x
        dz = entity.z - threat.z
        d  = (dx * dx + dz * dz) ** 0.5 or 1.0
        return (entity.x + dx / d * distance,
                entity.y,
                entity.z + dz / d * distance)
