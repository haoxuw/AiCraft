"""behavior_base.py — Base class for all entity AI behaviors.

Every behavior is a class that inherits from Behavior and overrides decide().

Contract
--------
  decide(entity, world) must return a 2-tuple: (action, goal_str)

  action    — result of Idle(), Wander(), MoveTo(x,y,z), Follow(id),
               Flee(id), BreakBlock(x,y,z), DropItem(type,n), PickupItem(id)
  goal_str  — non-empty human-readable string describing the current intent.
               This is shown as floating text above the entity's head and in
               the right-click inspect panel.  Never return an empty string.

Example
-------
    from modcraft_engine import *
    from behavior_base import Behavior

    class IdleGuard(Behavior):
        def __init__(self):
            self._home = None

        def decide(self, entity, world):
            if self._home is None:
                self._home = (entity["x"], entity["y"], entity["z"])
            return Idle(), "Standing guard"

The Python bridge instantiates one instance per entity, so instance
variables (self._timer, self._state, etc.) are safely isolated between
entities of the same type.
"""


class Behavior:
    """Abstract base for all entity behaviors.

    Subclasses must implement decide(entity, world) → (action, goal_str).
    """

    def decide(self, entity: dict, world: dict) -> tuple:
        """Called at ~4 Hz. Must return (action, goal: str).

        Parameters
        ----------
        entity : dict
            Live entity state.  Read-only fields: id, type_id, x, y, z,
            yaw, hp, walk_speed, on_ground.  Custom props set by the server
            (e.g. home_x, home_z, work_radius) are also present.
        world : dict
            World snapshot: "nearby" (list of entity dicts), "blocks"
            (list of block dicts), "dt" (frame delta seconds),
            "time" (0.0–1.0 day fraction).

        Returns
        -------
        (action, goal_str)
            action    — one of the modcraft_engine action constructors.
            goal_str  — non-empty human-readable string.
        """
        raise NotImplementedError(
            f"{type(self).__name__} must implement decide(entity, world) -> (action, goal)"
        )

    # ── Shared utility methods ────────────────────────────────────────────────

    @staticmethod
    def dist2d(ax: float, az: float, bx: float, bz: float) -> float:
        """Horizontal distance between two XZ positions."""
        dx, dz = ax - bx, az - bz
        return (dx * dx + dz * dz) ** 0.5

    @staticmethod
    def is_night(world: dict) -> bool:
        """True during night: midnight to dawn (0%–25%)."""
        return world.get("time", 0.5) < 0.25

    @staticmethod
    def is_morning(world: dict) -> bool:
        """True during morning: dawn to noon (25%–50%)."""
        return 0.25 <= world.get("time", 0.5) < 0.50

    @staticmethod
    def is_afternoon(world: dict) -> bool:
        """True during afternoon: noon to dusk (50%–75%)."""
        return 0.50 <= world.get("time", 0.5) < 0.75

    @staticmethod
    def is_evening(world: dict) -> bool:
        """True during evening: dusk to midnight (75%–100%)."""
        return world.get("time", 0.5) >= 0.75

    def init_home(self, entity: dict, home):
        """Return the home tuple, initialising it from entity props if None.

        Uses home_x / home_z server-assigned props when present,
        otherwise records the entity's current position as home.
        """
        if home is not None:
            return home
        hx = entity.get("home_x")
        hz = entity.get("home_z")
        if hx is not None and hz is not None:
            return (float(hx), entity["y"], float(hz))
        return (entity["x"], entity["y"], entity["z"])
