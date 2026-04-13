"""Woodcutter — villager that chops trees during the day, deposits logs at
the nearest chest, and sleeps in place at night.

No server-assigned home/chest props: chests are discovered at decide() time
via `scan_entities("base:chest", ...)`, so any player-placed chest works.

Behavior-local tuning (class defaults, overridable via __init__):
  COLLECT_GOAL  — logs to collect before depositing (default 5)
  WORK_RADIUS   — max radius to search for trees (default 80)
  CHEST_RADIUS  — max radius to search for chests (default 120)
  CHOP_PERIOD   — seconds between chop actions (default 0.5)

State machine:
  SLEEP   ─► WORK     when morning / afternoon begins
  WORK    ─► DEPOSIT  when inventory is full
  DEPOSIT ─► WORK     when inventory is empty after depositing
  any     ─► SLEEP    when evening / night begins
"""
import math
import random

from modcraft_engine import Move, Convert, Block, scan_blocks, scan_entities, get_block
from actions import StoreItem, DropItem
from behavior_base import Behavior
from entity_log import log as elog
from local_world import SelfEntity, LocalWorld
from pathfind import Navigator
from stats import stats

STORE_RANGE  = 3.0   # must be <= server-side StoreItem range (5.0 blocks).
                     # Larger than Navigator's arrive radius (1.5) so the
                     # villager doesn't get stuck idling next to the chest.
CHOP_RANGE   = 1.8   # horizontal; tree height is irrelevant to reach
CHOP_PERIOD  = 0.5


class WoodcutterBehavior(Behavior):

    SLEEP   = "sleep"
    WORK    = "work"
    DEPOSIT = "deposit"

    COLLECT_GOAL = 5
    WORK_RADIUS  = 80.0
    CHEST_RADIUS = 120.0

    def __init__(self, collect_goal: int = COLLECT_GOAL,
                 work_radius: float = WORK_RADIUS,
                 chop_period: float = CHOP_PERIOD):
        self._state           = self.SLEEP
        self._chop_cooldown   = 0.0
        self._collect_goal    = collect_goal
        self._work_radius     = work_radius
        self._chop_period     = chop_period
        self._nav             = Navigator()
        self._tree_target     = None   # (x, y, z, type) cache
        self._chest_target    = None   # (eid, x, y, z) cache for current deposit trip
        self._prev_state      = None

    # ── Top-level decide ──────────────────────────────────────────────────────

    def decide(self, entity: SelfEntity, local_world: LocalWorld):
        stats.inc("decide", entity.type)
        self._chop_cooldown -= local_world.dt
        self._update_state(entity, local_world)

        if self._state != self._prev_state:
            self._nav.reset()
            self._tree_target = None
            self._chest_target = None
            self._prev_state = self._state

        if self._state == self.SLEEP:
            return self._sleep(entity, local_world)
        if self._state == self.DEPOSIT:
            return self._deposit(entity, local_world)
        return self._work(entity, local_world)

    # ── State transitions ─────────────────────────────────────────────────────

    def _update_state(self, entity: SelfEntity, local_world: LocalWorld):
        is_day   = self.is_morning(local_world) or self.is_afternoon(local_world)
        is_night = self.is_night(local_world) or self.is_evening(local_world)

        if is_night:
            self._state = self.SLEEP
        elif self._state == self.SLEEP and is_day:
            self._state = self.WORK

        cap  = entity.inventory_capacity
        full = not entity.inventory.can_accept("base:logs", 1, cap)
        empty = entity.inventory.total_value() <= 0
        if self._state == self.WORK and full:
            self._state = self.DEPOSIT
        elif self._state == self.DEPOSIT and empty:
            self._state = self.WORK

    # ── State: Sleep ──────────────────────────────────────────────────────────

    def _sleep(self, entity: SelfEntity, local_world: LocalWorld):
        """Sleep in place — no home coordinate needed."""
        return Move(entity.x, entity.y, entity.z), "Sleeping zzz", 3.0

    # ── State: Work ───────────────────────────────────────────────────────────

    def _work(self, entity: SelfEntity, local_world: LocalWorld):
        spd  = entity.walk_speed
        logs = entity.inventory.count("base:logs")

        if self._tree_target is not None:
            ttx, tty, ttz, ttid = self._tree_target
            actual = get_block(ttx, tty, ttz)
            if actual != ttid:
                self._tree_target = None
                self._nav.reset()

        if self._tree_target is None:
            stats.inc("scan_blocks")
            origin = (entity.x, entity.y, entity.z)
            prefer  = "base:leaves" if random.random() < 0.5 else "base:logs"
            fallback = "base:logs" if prefer == "base:leaves" else "base:leaves"
            results = scan_blocks(prefer, near=origin,
                                  max_dist=self._work_radius, max_results=1)
            if not results:
                results = scan_blocks(fallback, near=origin,
                                      max_dist=self._work_radius, max_results=1)
            if not results:
                return self._search(entity, local_world, spd)
            t = results[0]
            self._tree_target = (t["x"], t["y"], t["z"], t["type"])
            self._nav.reset()

        tx, ty, tz, tid = self._tree_target
        label = "leaves" if tid == "base:leaves" else "log"

        dx = entity.x - (tx + 0.5)
        dz = entity.z - (tz + 0.5)
        horiz = math.hypot(dx, dz)

        if horiz <= CHOP_RANGE:
            return self._chop(entity, tx, ty, tz, tid, logs, label)

        goal = (int(tx), int(entity.y), int(tz))
        action = self._nav.navigate(entity, local_world, goal, speed=spd)
        if action:
            return action, "Walking to %s" % label
        return Move(tx + 0.5, entity.y, tz + 0.5, speed=spd), \
               "Walking to %s" % label

    def _chop(self, entity: SelfEntity, tx: int, ty: int, tz: int,
              tid: str, logs: int, label: str):
        if self._chop_cooldown <= 0:
            self._chop_cooldown = self._chop_period
            stats.inc("chop", tid)
            return (
                Convert(
                    from_item=tid,
                    to_item=tid,
                    convert_from=Block(tx, ty, tz),
                ),
                "Chopping %s" % label,
            )
        return Move(entity.x, entity.y, entity.z), \
               "Chopping %s" % label, self._chop_cooldown

    def _search(self, entity: SelfEntity, local_world: LocalWorld, spd: float):
        tx, ty, tz = self.wander_target(entity, radius=20)
        goal = (int(tx), int(ty), int(tz))
        action = self._nav.navigate(entity, local_world, goal, speed=spd)
        if action:
            return action, "Searching for trees"
        return Move(tx, ty, tz, speed=spd), "Searching for trees"

    # ── State: Deposit ────────────────────────────────────────────────────────

    def _find_chest(self, entity: SelfEntity):
        """Locate the nearest chest entity. Returns (eid, x, y, z) or None."""
        stats.inc("scan_entities", "base:chest")
        origin = (entity.x, entity.y, entity.z)
        hits = scan_entities("base:chest", near=origin,
                             max_dist=self.CHEST_RADIUS, max_results=1)
        if not hits:
            return None
        c = hits[0]
        return (int(c["id"]), float(c["x"]), float(c["y"]), float(c["z"]))

    def _deposit(self, entity: SelfEntity, local_world: LocalWorld):
        spd  = entity.walk_speed
        logs = entity.inventory.count("base:logs")

        # Nothing to deposit — flip back to WORK without issuing a 0-count
        # StoreItem/DropItem (server rejects those as ItemNotInInventory).
        if logs <= 0:
            elog(entity.id, "deposit: empty inventory, resuming work")
            self._state = self.WORK
            return Move(entity.x, entity.y, entity.z), "Idle (nothing to deposit)"

        # (Re)acquire the nearest chest. Re-scan if the cached one vanished.
        if self._chest_target is None:
            self._chest_target = self._find_chest(entity)
            self._nav.reset()

        if self._chest_target is None:
            elog(entity.id, "no chest in %.0f blocks, dropping %d logs"
                 % (self.CHEST_RADIUS, logs))
            return DropItem("base:logs", count=logs), "Dropping logs (no chest)"

        eid, cx, cy, cz = self._chest_target
        dx, dy, dz = entity.x - cx, entity.y - cy, entity.z - cz
        dist_to_chest = math.sqrt(dx*dx + dy*dy + dz*dz)

        if dist_to_chest <= STORE_RANGE:
            stats.inc("deposit", entity.type)
            return StoreItem(eid), "Depositing logs"

        goal = (int(cx), int(cy), int(cz))
        action = self._nav.navigate(entity, local_world, goal, speed=spd)
        if action:
            return action, "Carrying logs to chest"
        return Move(entity.x, entity.y, entity.z), "Waiting near chest"
