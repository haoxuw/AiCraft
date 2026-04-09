"""Woodcutter — villager that chops trees during the day, deposits at its chest,
and sleeps at its assigned bed at night.

Server assigns these props at spawn (see server.h):
  home_x, home_z       — XZ position of this villager's bed (their "home")
  chest_x, chest_y, chest_z — world-space position of their assigned chest block
  work_radius          — max radius to search for trees (default 80)
  collect_goal         — logs to collect before depositing (default 5)

State machine:
  SLEEP   ─► WORK     when morning / afternoon begins
  WORK    ─► DEPOSIT  when inventory reaches collect_goal logs
  DEPOSIT ─► WORK     when inventory is empty after depositing
  any     ─► SLEEP    when evening / night begins
"""
import json
import os
import time

from modcraft_engine import Move, Convert, Interact, Block, scan_blocks
from actions import StoreItem
from behavior_base import Behavior

STORE_RANGE  = 1.8   # must be <= server-side StoreItem range check (2.0 blocks)
CHOP_RANGE   = 2.5   # how close to stand before issuing Convert
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
        self._state         = self.SLEEP
        self._home          = None   # cached (x, y, z) of bed / home position
        self._chest         = None   # cached (x, y, z) of chest block position
        self._chop_cooldown = 0.0    # seconds until next Convert is allowed

    # ── Top-level decide ──────────────────────────────────────────────────────

    def decide(self, entity, local_world):
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
        """Find trees and chop them. Walk to a tree-rich area, then cut nearby blocks.
        Prefers leaves (clears canopy first), falls back to trunk.
        """
        spd         = entity.walk_speed
        work_radius = float(entity.get("work_radius", 80.0))
        logs        = entity.inventory.count("base:trunk")
        collect_goal= int(entity.get("collect_goal", 5))

        # First check: anything close enough to chop right now?
        # Leaves are above (canopy), so search wider range for them.
        # Trunk is at ground level, only check close range.
        nearby = scan_blocks("base:leaves", max_dist=8.0, max_results=1)
        if not nearby:
            nearby = scan_blocks("base:trunk", max_dist=CHOP_RANGE + 1, max_results=1)
        if nearby:
            t = nearby[0]
            bt = _BlockTarget(t["x"], t["y"], t["z"], t["distance"], t["type"])
            label = t["type"].split(":")[1]
            return self._chop(entity, bt, logs, collect_goal, label)

        # Nothing nearby — find a tree area to walk to (richest chunk first)
        far = scan_blocks("base:leaves", max_dist=work_radius, max_results=1)
        if not far:
            far = scan_blocks("base:trunk", max_dist=work_radius, max_results=1)
        if not far:
            return self._search(entity, local_world, spd)

        t = far[0]
        label = t["type"].split(":")[1]
        return self._move_or_unstick(
            entity, local_world,
            (t["x"] + 0.5, t["y"], t["z"] + 0.5),
            spd,
            "Walking to %s (%.0fm)" % (label, t["distance"]),
        )

    def _chop(self, entity, target, logs, collect_goal, label):
        """Issue a Convert to chop the block, respecting cooldown."""
        if self._chop_cooldown <= 0:
            self._chop_cooldown = CHOP_PERIOD
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
            Move(*self.wander_target(entity, radius=20), speed=spd * 0.7),
            "Searching for trees...",
        )

    # ── State: Deposit ────────────────────────────────────────────────────────

    def _deposit(self, entity, local_world):
        """Walk to the chest, open any door in the way, and store logs."""
        spd  = entity.walk_speed
        logs = entity.inventory.count("base:trunk")

        if self._chest is None:
            return Move(*self._home, speed=spd), "Looking for chest..."

        dist_to_chest = self.dist2d(entity.x, entity.z, self._chest[0], self._chest[2])

        if dist_to_chest <= STORE_RANGE:
            # StoreItem now takes block position, not entity ID
            cx, cy, cz = int(self._chest[0]), int(self._chest[1]), int(self._chest[2])
            return StoreItem(cx, cy, cz), "Depositing %d logs" % logs

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
