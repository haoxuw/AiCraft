"""Woodcutter — villager that chops trees during the day, deposits at its chest,
and sleeps at its assigned bed at night.

Server-assigned spawn props (see server.h):
  home_x, home_z                 — XZ position of this villager's bed
  chest_entity_id                — entity ID of the assigned chest
  chest_x, chest_y, chest_z     — world-space position of the chest

Behavior-local tuning (class defaults, overridable via __init__):
  COLLECT_GOAL  — logs to collect before depositing (default 5)
  WORK_RADIUS   — max radius to search for trees (default 80)
  CHOP_PERIOD   — seconds between chop actions (default 0.5)

State machine:
  SLEEP   ─► WORK     when morning / afternoon begins
  WORK    ─► DEPOSIT  when inventory reaches COLLECT_GOAL logs
  DEPOSIT ─► WORK     when inventory is empty after depositing
  any     ─► SLEEP    when evening / night begins
"""
import json
import math
import os
import time

from modcraft_engine import Move, Convert, Interact, Block, scan_blocks, get_block
from actions import StoreItem, DropItem
from behavior_base import Behavior
from local_world import SelfEntity, LocalWorld
from pathfind import Navigator
from stats import stats

STORE_RANGE  = 1.8   # must be <= server-side StoreItem range check (2.0 blocks)
# Chop is gated on HORIZONTAL distance only (tree height is irrelevant).
# 1.8 blocks ≈ standing directly adjacent to the target block on any cardinal side.
CHOP_RANGE   = 1.8
# When walking to a target, stand this far from the block center on the horizontal plane.
# 1.2 keeps the entity flush with the block face (entity body radius ~0.4 + block half ~0.5 + slop).
STAND_OFFSET = 1.2
CHOP_PERIOD  = 0.5   # seconds between successive chop actions

_DIAG_INTERVAL = 30.0          # minimum seconds between dumps per entity
_diag_last: dict[int, float] = {}  # entity_id → last dump timestamp


class _BlockTarget:
    """Lightweight stand-in for scan_blocks dict results, compatible with Behavior helpers."""
    __slots__ = ("x", "y", "z", "distance", "type")
    def __init__(self, x, y, z, distance, type):
        self.x, self.y, self.z, self.distance, self.type = x, y, z, distance, type


def dump_for_diagnoses(entity, local_world):
    """Dump LocalWorld + entity state to /tmp/modcraft_diag_<id>.json.

    Rate-limited to once per DIAG_INTERVAL seconds per entity to avoid
    flooding /tmp at 4 Hz when no trees are found.
    """
    now = time.monotonic()
    eid = entity.id
    if now - _diag_last.get(eid, 0.0) < _DIAG_INTERVAL:
        return
    _diag_last[eid] = now

    data = {
        "entity_id":  eid,
        "type":    entity.type,
        "position":   {"x": entity.x, "y": entity.y, "z": entity.z},
        "hp":         entity.hp,
        "walk_speed": entity.walk_speed,
        "on_ground":  entity.on_ground,
        "inventory":  dict(entity.inventory.items),
        "props":      {k: v for k, v in entity.props.items()
                       if k not in ("x", "y", "z", "hp", "walk_speed", "on_ground")},
        "local_world": {
            "time": local_world.time,
            "dt":   local_world.dt,
            "blocks": [
                {"type": b.type, "x": b.x, "y": b.y, "z": b.z, "distance": round(b.distance, 2)}
                for b in local_world.blocks
            ],
            "entities": [
                {"id": e.id, "type": e.type, "category": e.category,
                 "x": e.x, "y": e.y, "z": e.z, "hp": e.hp, "distance": round(e.distance, 2)}
                for e in local_world.entities
            ],
        },
    }

    path = f"/tmp/modcraft_diag_{eid}.json"
    try:
        with open(path, "w") as f:
            json.dump(data, f, indent=2)
        print(f"[woodcutter] diag dump → {path}  (entity #{eid}, {len(local_world.blocks)} blocks, {len(local_world.entities)} entities)")
    except OSError as exc:
        print(f"[woodcutter] diag dump failed: {exc}")


class WoodcutterBehavior(Behavior):

    SLEEP   = "sleep"
    WORK    = "work"
    DEPOSIT = "deposit"

    # Behavior-local tuning (not server props — these belong to the behavior)
    COLLECT_GOAL = 5      # logs to collect before depositing
    WORK_RADIUS  = 80.0   # max radius to search for trees

    def __init__(self, collect_goal: int = COLLECT_GOAL,
                 work_radius: float = WORK_RADIUS,
                 chop_period: float = CHOP_PERIOD):
        self._state           = self.SLEEP
        self._home            = None   # cached (x, y, z) of bed / home position
        self._chest           = None   # cached (x, y, z) of chest position (for navigation)
        self._chest_entity_id = None   # entity ID of the chest Structure entity
        self._chop_cooldown   = 0.0    # seconds until next Convert is allowed
        self._collect_goal    = collect_goal
        self._work_radius     = work_radius
        self._chop_period     = chop_period
        self._nav             = Navigator()  # A* path-follower (replans on goal change or stuck)
        self._tree_target     = None   # cached (x, y, z, type) of current tree block
        self._prev_state      = None   # detect state transitions to reset nav

    # ── Top-level decide ──────────────────────────────────────────────────────

    def decide(self, entity: SelfEntity, local_world: LocalWorld):
        stats.inc("decide", entity.type)
        self._chop_cooldown -= local_world.dt
        self._ensure_props(entity)
        self._update_state(entity, local_world)

        # Reset nav + tree cache on state transitions
        if self._state != self._prev_state:
            self._nav.reset()
            self._tree_target = None
            self._prev_state = self._state

        if self._state == self.SLEEP:
            return self._sleep(entity, local_world)
        if self._state == self.DEPOSIT:
            return self._deposit(entity, local_world)
        return self._work(entity, local_world)

    # ── State transitions ─────────────────────────────────────────────────────

    def _update_state(self, entity: SelfEntity, local_world: LocalWorld):
        logs     = entity.inventory.count("base:logs")
        is_day   = self.is_morning(local_world) or self.is_afternoon(local_world)
        is_night = self.is_night(local_world) or self.is_evening(local_world)

        if is_night:
            self._state = self.SLEEP
        elif self._state == self.SLEEP and is_day:
            self._state = self.WORK   # may advance immediately to DEPOSIT below

        # Checked independently so SLEEP→WORK→DEPOSIT can fire in one tick
        if self._state == self.WORK and logs >= self._collect_goal:
            self._state = self.DEPOSIT
        elif self._state == self.DEPOSIT and logs == 0:
            self._state = self.WORK

    # ── Compatibility property for tests and external inspection ──────────────

    @property
    def _depositing(self):
        return self._state == self.DEPOSIT

    @_depositing.setter
    def _depositing(self, value):
        self._state = self.DEPOSIT if value else self.WORK

    # ── State: Sleep ──────────────────────────────────────────────────────────

    def _sleep(self, entity: SelfEntity, local_world: LocalWorld):
        """Walk to bed; once there, stand still and rest."""
        spd = entity.walk_speed
        if not self.is_near(entity, self._home, threshold=2.0):
            goal = (int(self._home[0]), int(self._home[1]), int(self._home[2]))
            action = self._nav.navigate(entity, local_world, goal, speed=spd)
            if action:
                return action, "Going home..."
        return Move(entity.x, entity.y, entity.z), "Sleeping zzz", 3.0

    # ── State: Work ───────────────────────────────────────────────────────────

    def _work(self, entity: SelfEntity, local_world: LocalWorld):
        """Find the nearest tree block and approach it.

        Caches the target tree block — only re-scans when the cached target
        is gone (chopped by self or another entity) or no target exists yet.
        Uses Navigator (A*) for pathfinding to avoid obstacles.
        """
        spd  = entity.walk_speed
        logs = entity.inventory.count("base:logs")

        # Validate cached tree target — is the block still there?
        if self._tree_target is not None:
            ttx, tty, ttz, ttid = self._tree_target
            actual = get_block(ttx, tty, ttz)
            if actual != ttid:
                self._tree_target = None
                self._nav.reset()

        # Only scan for a new tree when cache is empty
        if self._tree_target is None:
            stats.inc("scan_blocks")
            results = scan_blocks("base:leaves", near=self._home,
                                  max_dist=self._work_radius, max_results=1)
            label = "leaves"
            if not results:
                results = scan_blocks("base:logs", near=self._home,
                                      max_dist=self._work_radius, max_results=1)
                label = "log"
            if not results:
                return self._search(entity, local_world, spd)
            t = results[0]
            self._tree_target = (t["x"], t["y"], t["z"], t["type"])
            self._nav.reset()

        tx, ty, tz, tid = self._tree_target
        label = "leaves" if "leaves" in tid else "log"

        # Horizontal distance to target
        dx = entity.x - (tx + 0.5)
        dz = entity.z - (tz + 0.5)
        horiz = math.hypot(dx, dz)

        # Adjacent on the XZ plane → chop
        if horiz <= CHOP_RANGE:
            bt = _BlockTarget(tx, ty, tz, horiz, tid)
            return self._chop(entity, bt, logs, label)

        # Navigate to ground level adjacent to the tree (not to the canopy Y)
        goal = (int(tx), int(entity.y), int(tz))
        action = self._nav.navigate(entity, local_world, goal, speed=spd)
        if action:
            return action, "Walking to %s (%.0fm)" % (label, horiz)
        return Move(tx + 0.5, entity.y, tz + 0.5, speed=spd), \
               "Approaching %s (%.0fm)" % (label, horiz)

    def _chop(self, entity: SelfEntity, target: _BlockTarget, logs: int, label: str):
        """Issue a Convert to chop the block, respecting cooldown."""
        if self._chop_cooldown <= 0:
            self._chop_cooldown = self._chop_period
            stats.inc("chop", target.type)
            return (
                Convert(
                    from_item=target.type,
                    to_item=target.type,
                    convert_from=Block(target.x, target.y, target.z),
                ),
                "Chopping %s! (%d/%d)" % (label, logs, self._collect_goal),
            )
        return Move(entity.x, entity.y, entity.z), \
               "Chopping... (%d/%d)" % (logs, self._collect_goal), self._chop_cooldown

    def _search(self, entity: SelfEntity, local_world: LocalWorld, spd: float):
        """No trees in range — wander outward to find some."""
        if self.check_stuck(entity, local_world.dt):
            self.reset_stuck()
        return (
            Move(*self.wander_target(entity, radius=20), speed=spd),
            "Searching for trees...",
        )

    # ── State: Deposit ────────────────────────────────────────────────────────

    def _deposit(self, entity: SelfEntity, local_world: LocalWorld):
        """Walk to the chest and store logs. Uses Navigator for pathfinding."""
        spd  = entity.walk_speed
        logs = entity.inventory.count("base:logs")

        if self._chest_entity_id is None:
            print("[woodcutter] entity %d: no chest, dropping %d logs" % (entity.id, logs))
            return DropItem("base:logs", count=logs), "Dropping %d logs (no chest)" % logs

        dist_to_chest = self.dist2d(entity.x, entity.z, self._chest[0], self._chest[2])

        if dist_to_chest <= STORE_RANGE:
            stats.inc("deposit", entity.type)
            return StoreItem(self._chest_entity_id), "Depositing %d logs" % logs

        goal = (int(self._chest[0]), int(self._chest[1]), int(self._chest[2]))
        action = self._nav.navigate(entity, local_world, goal, speed=spd)
        if action:
            return action, "Carrying %d logs to chest (%.0fm)" % (logs, dist_to_chest)
        return Move(self._chest[0], self._chest[1], self._chest[2], speed=spd), \
               "Approaching chest (%.0fm)" % dist_to_chest

    # ── Prop initialisation (reads server-injected spawn props once) ──────────

    def _ensure_props(self, entity: SelfEntity):
        """Cache server-assigned spawn props (home_x, chest_entity_id, etc.)."""
        if self._home is None:
            self._home = self.init_home(entity, self._home)
        if self._chest is None:
            self._chest = self.get_chest(entity, self._home)
        if self._chest_entity_id is None:
            eid = entity.get("chest_entity_id", 0)
            if eid: self._chest_entity_id = int(eid)
