"""Woodcutter — villager that chops trees during the day, deposits at its chest,
and sleeps at its assigned bed at night.

Server assigns these props at spawn (see server.h):
  home_x, home_z       — XZ position of this villager's bed (their "home")
  chest_entity_id      — entity ID of the assigned chest Structure entity
  chest_x, chest_y, chest_z — world-space position of the chest (for navigation)
  work_radius          — max radius to search for trees (default 80)
  collect_goal         — logs to collect before depositing (default 5)

State machine:
  SLEEP   ─► WORK     when morning / afternoon begins
  WORK    ─► DEPOSIT  when inventory reaches collect_goal logs
  DEPOSIT ─► WORK     when inventory is empty after depositing
  any     ─► SLEEP    when evening / night begins
"""
import json
import math
import os
import time

from modcraft_engine import Move, Convert, Interact, Block, scan_blocks
from actions import StoreItem, DropItem
from behavior_base import Behavior

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
    __slots__ = ("x", "y", "z", "distance", "type_id")
    def __init__(self, x, y, z, distance, type_id):
        self.x, self.y, self.z, self.distance, self.type_id = x, y, z, distance, type_id


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
        "type_id":    entity.type_id,
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
                {"type_id": b.type_id, "x": b.x, "y": b.y, "z": b.z, "distance": round(b.distance, 2)}
                for b in local_world.blocks
            ],
            "entities": [
                {"id": e.id, "type_id": e.type_id, "category": e.category,
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

    def __init__(self):
        self._state           = self.SLEEP
        self._home            = None   # cached (x, y, z) of bed / home position
        self._chest           = None   # cached (x, y, z) of chest position (for navigation)
        self._chest_entity_id = None   # entity ID of the chest Structure entity
        self._chop_cooldown   = 0.0    # seconds until next Convert is allowed
        self._chop_period     = None   # optional chop period override from props

    # ── Top-level decide ──────────────────────────────────────────────────────

    def decide(self, entity: "SelfEntity", local_world: "LocalWorld"):
        self._chop_cooldown -= local_world.dt
        self._ensure_props(entity, local_world)
        self._update_state(entity, local_world)

        if self._state == self.SLEEP:
            return self._sleep(entity, local_world)
        if self._state == self.DEPOSIT:
            return self._deposit(entity, local_world)
        return self._work(entity, local_world)

    # ── State transitions ─────────────────────────────────────────────────────

    def _update_state(self, entity, local_world):
        logs         = entity.inventory.count("base:trunk")
        collect_goal = int(entity.get("collect_goal", 5))
        is_day       = self.is_morning(local_world) or self.is_afternoon(local_world)
        is_night_now = self.is_night(local_world) or self.is_evening(local_world)

        if is_night_now:
            self._state = self.SLEEP
        elif self._state == self.SLEEP and is_day:
            self._state = self.WORK   # may advance immediately to DEPOSIT below

        # Checked independently so SLEEP→WORK→DEPOSIT can fire in one tick
        if self._state == self.WORK and logs >= collect_goal:
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

    def _sleep(self, entity, local_world):
        """Walk to bed; once there, stand still and rest."""
        spd = entity.walk_speed
        if not self.is_near(entity, self._home, threshold=2.0):
            return self._move_or_unstick(entity, local_world, self._home, spd, "Going home...")
        return Move(entity.x, entity.y, entity.z), "Sleeping zzz"

    # ── State: Work ───────────────────────────────────────────────────────────

    def _work(self, entity, local_world):
        """Find the nearest tree block and approach it horizontally.

        Chop is gated on horizontal (XZ) distance — tree height is irrelevant.
        The walk target is placed 1 block horizontally next to the target block
        so the woodcutter ends up flush with the trunk/leaf face before chopping.
        Prefers trunk (reachable from ground) and falls back to leaves.
        """
        spd          = entity.walk_speed
        work_radius  = float(entity.get("work_radius", 80.0))
        logs         = entity.inventory.count("base:trunk")
        collect_goal = int(entity.get("collect_goal", 5))

        # Scan_blocks distance is 3D, so max_dist must account for tree height.
        # Trunk base is at ground level → cheap and reliable to reach first.
        # Anchor the search at home (bed) so the woodcutter never wanders past
        # its work_radius even after walking partway toward a previous tree.
        results = scan_blocks("base:trunk", near=self._home,
                              max_dist=work_radius, max_results=1)
        label   = "trunk"
        if not results:
            results = scan_blocks("base:leaves", near=self._home,
                                  max_dist=work_radius, max_results=1)
            label   = "leaves"
        if not results:
            return self._search(entity, local_world, spd)

        t = results[0]
        tx, ty, tz = t["x"], t["y"], t["z"]

        # Horizontal vector from target to entity.
        dx = entity.x - (tx + 0.5)
        dz = entity.z - (tz + 0.5)
        horiz = math.hypot(dx, dz)

        # Adjacent on the XZ plane → chop, regardless of vertical offset.
        if horiz <= CHOP_RANGE:
            bt = _BlockTarget(tx, ty, tz, horiz, t["type"])
            return self._chop(entity, bt, logs, collect_goal, label)

        # Walk to a standing position 1 block horizontally next to the target,
        # on the side nearest the entity (so we don't circle the tree).
        if horiz > 1e-3:
            stand_x = (tx + 0.5) + (dx / horiz) * STAND_OFFSET
            stand_z = (tz + 0.5) + (dz / horiz) * STAND_OFFSET
        else:
            stand_x, stand_z = tx + 0.5 + STAND_OFFSET, tz + 0.5
        return self._move_or_unstick(
            entity, local_world,
            (stand_x, entity.y, stand_z),
            spd,
            "Walking to %s (%.0fm)" % (label, horiz),
        )

    def _chop(self, entity, target, logs, collect_goal, label):
        """Issue a Convert to chop the block, respecting cooldown."""
        if self._chop_cooldown <= 0:
            self._chop_cooldown = self._chop_period or CHOP_PERIOD
            return (
                Convert(
                    from_item=target.type_id,
                    to_item=target.type_id,
                    convert_from=Block(target.x, target.y, target.z),
                ),
                "Chopping %s! (%d/%d)" % (label, logs, collect_goal),
            )
        return Move(entity.x, entity.y, entity.z), "Chopping... (%d/%d)" % (logs, collect_goal)

    def _search(self, entity, local_world, spd):
        """No trees in range — wander outward to find some."""
        if self.check_stuck(entity, local_world.dt):
            self.reset_stuck()
        return (
            Move(*self.wander_target(entity, radius=20), speed=spd),
            "Searching for trees...",
        )

    # ── State: Deposit ────────────────────────────────────────────────────────

    def _deposit(self, entity, local_world):
        """Walk to the chest, open any door in the way, and store logs."""
        spd  = entity.walk_speed
        logs = entity.inventory.count("base:trunk")

        if self._chest_entity_id is None:
            # No chest assigned — drop items on the ground
            print("[woodcutter] entity %d: no chest_entity_id prop, dropping %d logs on ground" % (entity.id, logs))
            return DropItem("base:trunk", count=logs), "Dropping %d logs (no chest)" % logs

        dist_to_chest = self.dist2d(entity.x, entity.z, self._chest[0], self._chest[2])

        if dist_to_chest <= STORE_RANGE:
            return StoreItem(self._chest_entity_id), "Depositing %d logs" % logs

        # Open any closed door blocking the path before walking through
        door = local_world.get("base:door", max_dist=3.0)
        if door and door.distance < 2.0:
            return Interact(int(door.x), int(door.y), int(door.z)), "Opening door"

        return self._move_or_unstick(
            entity, local_world, self._chest, spd,
            "Carrying %d logs to chest (%.0fm)" % (logs, dist_to_chest),
        )

    # ── Movement helper with stuck recovery ───────────────────────────────────

    def _move_or_unstick(self, entity, local_world, target, speed, goal_text):
        """Move toward target; wander briefly if stuck."""
        if self.check_stuck(entity, local_world.dt):
            self.reset_stuck()
            return Move(*self.wander_target(entity, radius=5), speed=speed), "Stuck — finding way"
        return Move(*target, speed=speed), goal_text

    # ── Prop initialisation (called each tick until populated) ────────────────

    def _ensure_props(self, entity, local_world):
        """Read server-assigned props into cached fields (cheap after first call)."""
        if self._home is None:
            self._home = self.init_home(entity, self._home)
        if self._chest is None:
            self._chest = self.get_chest(entity, self._home)
        if self._chest_entity_id is None:
            eid = entity.get("chest_entity_id", 0)
            if eid: self._chest_entity_id = int(eid)
        if self._chop_period is None:
            self._chop_period = float(entity.get("chop_period", CHOP_PERIOD))
