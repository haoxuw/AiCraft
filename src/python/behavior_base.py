"""behavior_base.py — Base class for all entity AI behaviors.

Every behavior inherits from Behavior and overrides decide().

NEW CONTRACT (Plan-based)
-------------------------
    decide(entity: SelfEntity, local_world: LocalWorld) → (plan, goal_str)

    plan      — a list of PlanStep dicts, each with a "type" key:
                  {"type": "move", "x": ..., "y": ..., "z": ...}
                  {"type": "harvest", "x": ..., "y": ..., "z": ...}
                  {"type": "attack", "entity_id": ...}
                  {"type": "relocate", "from": Container, "to": Container,
                   "item": "logs", "count": 1}
    goal_str  — non-empty human-readable string (shown above entity's head).

    The old single-action return (Move(...), goal) is DEPRECATED.
    Behaviors should return a list of plan steps that the AgentClient
    executes sequentially.

Event-driven loop
-----------------
    decide() is called ONLY when the previous plan terminates or is
    interrupted — not on a polling timer. The plan executor runs each
    frame and classifies each step as InProgress/Success/Failed from
    observable world state. Terminal outcomes enqueue a re-decide.

    This means the returned plan is immutable until it completes, so
    targets are automatically "sticky" — no need to cache them in self.

    local_world.last_outcome / last_goal / last_reason describe why the
    previous plan ended, so decide() can branch:

      last_outcome == "success" — plan finished normally
      last_outcome == "failed"  — plan aborted; last_reason gives detail
                                  ("stuck", "target_gone",
                                   "interrupt:hp", "interrupt:proximity",
                                   "interrupt:time_of_day")
      last_outcome == "none"    — first decide() for this entity

Example
-------
    from behavior_base import Behavior

    class WoodcutterBehavior(Behavior):
        def decide(self, entity, local_world):
            tree = local_world.get("logs", max_dist=40)
            if tree:
                return [
                    {"type": "move", "x": tree.x, "y": tree.y, "z": tree.z},
                    {"type": "harvest", "x": tree.x, "y": tree.y, "z": tree.z},
                ], "Chopping wood"
            return [{"type": "move", **self._wander_target(entity)}], "Wandering"
"""

import math
import random as _random
from local_world import LocalWorld, SelfEntity, BlockView, EntityView
from stats import stats


class Behavior:
    """Abstract base for all entity behaviors.

    Subclasses must implement decide(entity, local_world) → (plan, goal_str).
    """

    def decide(self, entity: SelfEntity, local_world: LocalWorld) -> tuple:
        """Called at ~4 Hz. Must return (plan: list[dict], goal: str).

        Parameters
        ----------
        entity : SelfEntity
            Live entity state (read-only). Attributes:
            entity.x, entity.y, entity.z, entity.hp, entity.walk_speed,
            entity.on_ground, entity.inventory.count("logs"),
            entity.get("work_radius", 80.0)

        local_world : LocalWorld
            Spatial snapshot of what this agent can currently perceive.
            local_world.get("logs")
            local_world.all("logs", max_dist=40)
            # "Any playable character" — filter by tag, not a hardcoded id:
            [x for x in local_world.entities if x.has_tag("playable")]
            local_world.time
            local_world.dt

        Returns
        -------
        (plan, goal_str) where:
            plan = list of PlanStep dicts, e.g.:
                [{"type": "move", "x": 10, "y": 4, "z": 20}]
                [{"type": "move", ...}, {"type": "harvest", "x": 5, "y": 4, "z": 8}]
                [{"type": "attack", "entity_id": 42}]
                [{"type": "relocate", "from": Self(), "to": Block(x,y,z),
                  "item": "logs", "count": 1}]
            goal_str = non-empty human-readable string
        """
        raise NotImplementedError(
            f"{type(self).__name__} must implement decide(entity, local_world) → (plan, goal)"
        )

    # ── Time-of-day helpers ───────────────────────────────────────────────────

    @staticmethod
    def is_night(local_world: LocalWorld) -> bool:
        return local_world.time < 0.25

    @staticmethod
    def is_morning(local_world: LocalWorld) -> bool:
        return 0.25 <= local_world.time < 0.50

    @staticmethod
    def is_afternoon(local_world: LocalWorld) -> bool:
        return 0.50 <= local_world.time < 0.75

    @staticmethod
    def is_evening(local_world: LocalWorld) -> bool:
        return local_world.time >= 0.75

    # ── Distance helpers ──────────────────────────────────────────────────────

    @staticmethod
    def dist2d(ax: float, az: float, bx: float, bz: float) -> float:
        dx, dz = ax - bx, az - bz
        return (dx * dx + dz * dz) ** 0.5

    # ── Proximity helpers ─────────────────────────────────────────────────────

    @staticmethod
    def is_near(entity: SelfEntity, pos, threshold: float = 2.5) -> bool:
        if isinstance(pos, (BlockView, EntityView)):
            tx, tz = pos.x, pos.z
        elif isinstance(pos, dict):
            tx, tz = pos["x"], pos["z"]
        else:
            tx, tz = pos[0], pos[2]
        dx = entity.x - tx
        dz = entity.z - tz
        return (dx * dx + dz * dz) < threshold * threshold

    # ── Home helper ───────────────────────────────────────────────────────────
    # Behaviors like prowl use a "home" (territory anchor) — here it's simply
    # the entity's first-observed position. The server no longer assigns
    # home_x/home_z spawn props: anything that used to be "assigned home/chest"
    # is now discovered at decide() time (e.g. scan_entities("chest")).

    def init_home(self, entity: SelfEntity, home: tuple) -> tuple:
        if home is not None:
            return home
        return (entity.x, entity.y, entity.z)

    # ── Movement helpers (return (x, y, z) tuple — used with Move()) ───────

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

        Usage:
            return Move(*self.flee_pos(entity, threat), speed=spd * 1.8), "Fleeing!"
        """
        dx = entity.x - threat.x
        dz = entity.z - threat.z
        d  = (dx * dx + dz * dz) ** 0.5 or 1.0
        return (entity.x + dx / d * distance,
                entity.y,
                entity.z + dz / d * distance)
