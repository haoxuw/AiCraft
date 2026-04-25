"""behavior_base.py — Base class for all entity AI behaviors.

Every behavior inherits from Behavior and overrides decide().

NEW CONTRACT (Plan-based)
-------------------------
    decide(entity: SelfEntity, local_world: LocalWorld) → (plan, goal_str)

    plan      — a list of strongly-typed plan_steps.* Pydantic models:
                  HarvestStep(candidates=[Vec3(...), ...],
                              gather_types=["logs"], item="logs")
                  AttackStep(entity_id=...)
                  RelocateStep(item="logs", count=1)
                All movement is implicit — plan steps resolve their own
                target (nearest candidate, target entity, chest) and the
                executor walks there. There is no "move to coordinate"
                primitive; for one-off movement use Move(...) PyAction.
    goal_str  — non-empty human-readable string (shown above entity's head).

    The old single-action return (Move(...), goal) is still supported for
    idle / wander / follow — see the PyAction path below.

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
    from civcraft_engine import Move
    from plan_steps import HarvestStep, Vec3

    class WoodcutterBehavior(Behavior):
        def decide(self, entity, local_world):
            trees = local_world.all("logs", max_dist=40)
            if trees:
                return [HarvestStep(
                    candidates=[Vec3(x=t.x, y=t.y, z=t.z) for t in trees],
                    gather_types=["logs"],
                    item="logs",
                )], "Chopping wood"
            tx, ty, tz = self.wander_target(entity)
            return Move(int(tx), int(ty), int(tz), use_navigator=True), "Wandering"
"""

import math
import random as _random
from local_world import LocalWorld, SelfEntity, BlockView, EntityView
from stats import stats


class Behavior:
    """Abstract base for all entity behaviors.

    Subclasses must implement decide(entity, local_world) → (plan, goal_str).
    """

    def react(self, entity: SelfEntity, local_world: LocalWorld, signal):
        """Handle an out-of-band event signal (threat_nearby, …).

        Called by AgentClient when a notable world event is detected near
        this entity. Return the same shape as decide() to override the
        current plan, or None to ignore the signal and let the existing
        plan keep running.

        Parameters
        ----------
        entity, local_world
            Same as decide() — a fresh snapshot at the moment the signal
            fired.
        signal : types.SimpleNamespace
            signal.kind    — str, one of signals.THREAT_NEARBY, …
            signal.payload — dict with event-specific fields

        Returns
        -------
        (action, goal_str[, duration])  — override current plan, or
        list of plan-step dicts            same shape as decide()
        None                            — keep current plan

        Default implementation ignores every signal.
        """
        return None

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
            plan = list of plan_steps.* Pydantic models, e.g.:
                [HarvestStep(candidates=[...], gather_types=["logs"],
                             item="logs")]
                [AttackStep(entity_id=42)]
                [RelocateStep(item="logs", count=1)]
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
