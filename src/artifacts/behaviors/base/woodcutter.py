"""Woodcutter — villager that chops trees during the day, deposits logs at
the nearest chest, and sleeps in place at night.

No server-assigned home/chest props: chests are discovered at decide() time
via `scan_entities("chest", ...)`, so any player-placed chest works.

High-level vs inner loop:
  decide() picks *where* to stand (the anchor near a tree) and declares the
  gather priority list. The C++ executor runs the tight inner loop — walk
  to anchor, scan a gather_radius sphere for blocks of the highest-priority
  type present, chop the nearest hit, repeat. Prioritization is enforced by
  the executor: logs are never chopped while any leaf exists in range.

Behavior-local tuning (class defaults, overridable via __init__):
  COLLECT_GOAL  — logs to collect before depositing (default 5)
  WORK_RADIUS   — max radius to search for trees (default 80)
  CHEST_RADIUS  — max radius to search for chests (default 120)

State machine:
  SLEEP   -> WORK     when morning / afternoon begins
  WORK    -> DEPOSIT  when inventory is full
  DEPOSIT -> WORK     when inventory is empty after depositing
  any     -> SLEEP    when evening / night begins
"""
import math
import traceback

from civcraft_engine import Move, scan_blocks, scan_entities
from actions import StoreItem, DropItem
from behavior_base import Behavior
from entity_log import log as elog
from local_world import SelfEntity, LocalWorld
from pathfind import Navigator
from stats import stats

STORE_RANGE  = 3.0   # must be <= server-side StoreItem range (5.0 blocks).
                     # Larger than Navigator's arrive radius (1.5) so the
                     # villager doesn't get stuck idling next to the chest.
CHOP_COOLDOWN = 1.0  # seconds between swings (executor-enforced)
GATHER_RADIUS = 6.0  # executor scans this sphere around the villager each swing


class WoodcutterBehavior(Behavior):

    SLEEP   = "sleep"
    WORK    = "work"
    DEPOSIT = "deposit"

    COLLECT_GOAL = 5
    WORK_RADIUS  = 80.0
    CHEST_RADIUS = 120.0

    # Priority order for the executor's in-place scan. Index 0 = highest:
    # every swing, the executor scans the local volume for tier 0 first and
    # only falls through to tier 1 when tier 0 has zero hits in range.
    # Override on a subclass to change what a villager prefers.
    GATHER_PRIORITY = ["leaves", "logs"]

    def __init__(self, collect_goal: int = COLLECT_GOAL,
                 work_radius: float = WORK_RADIUS):
        self._state           = self.SLEEP
        self._collect_goal    = collect_goal
        self._work_radius     = work_radius
        self._nav             = Navigator()
        self._chest_target    = None   # (eid, x, y, z) cache for current deposit trip
        self._prev_state      = None
        # Unreachable-anchor blacklist. Populated when the villager picks the
        # same anchor for N consecutive ticks without getting closer OR
        # collecting anything — the executor's walk step is wedged and no
        # retry will help. Cleared every time WORK state is entered fresh.
        self._failed_anchors     = set()
        self._last_anchor_key    = None
        self._last_anchor_dist   = None
        self._last_logs_at_work  = None
        self._stuck_ticks        = 0

    # -- Top-level decide ----------------------------------------------------

    def decide(self, entity: SelfEntity, local_world: LocalWorld):
        try:
            return self._decide_inner(entity, local_world)
        except Exception as e:
            elog(entity.id, "decide CRASH pos=(%r,%r,%r) inv_cap=%r time=%r\n%s" % (
                entity.x, entity.y, entity.z,
                getattr(entity, "inventory_capacity", "??"),
                getattr(local_world, "time", "??"),
                traceback.format_exc()))
            raise

    def _decide_inner(self, entity: SelfEntity, local_world: LocalWorld):
        stats.inc("decide", entity.type)
        prev_state = self._state
        self._update_state(entity, local_world)

        if self._state != self._prev_state:
            elog(entity.id, "state %s -> %s (inv=%.1f cap=%.1f logs=%d)" % (
                self._prev_state, self._state,
                entity.inventory.total_value(), entity.inventory_capacity,
                entity.inventory.count("logs")))
            self._nav.reset()
            self._chest_target = None
            # Fresh WORK cycle: clear the blacklist so trees that failed
            # last trip (different position, different terrain) get another
            # chance after deposit.
            if self._state == self.WORK:
                self._failed_anchors.clear()
                self._last_anchor_key   = None
                self._last_anchor_dist  = None
                self._last_logs_at_work = None
                self._stuck_ticks       = 0
            self._prev_state = self._state

        if self._state == self.SLEEP:
            return self._sleep(entity, local_world)
        if self._state == self.DEPOSIT:
            return self._deposit(entity, local_world)
        return self._work(entity, local_world)

    # -- State transitions ---------------------------------------------------

    def _update_state(self, entity: SelfEntity, local_world: LocalWorld):
        is_day   = self.is_morning(local_world) or self.is_afternoon(local_world)
        is_night = self.is_night(local_world) or self.is_evening(local_world)

        if is_night:
            self._state = self.SLEEP
        elif self._state == self.SLEEP and is_day:
            self._state = self.WORK

        # Gate on `logs >= collect_goal`, NOT on `not can_accept(...)`.
        # Humanoids have `inventory_capacity = inf` (intentional, so players
        # never see "Inventory full"), which makes `can_accept` always True
        # and DEPOSIT unreachable. The collect_goal gate is both correct for
        # civilian villagers and robust to the inf-cap design.
        logs_held = entity.inventory.count("logs")
        ready_to_deposit = logs_held >= self._collect_goal
        empty = entity.inventory.total_value() <= 0
        if self._state == self.WORK and ready_to_deposit:
            self._state = self.DEPOSIT
        elif self._state == self.DEPOSIT and empty:
            self._state = self.WORK

    # -- State: Sleep --------------------------------------------------------

    def _sleep(self, entity: SelfEntity, local_world: LocalWorld):
        """Sleep in place — no home coordinate needed."""
        return Move(entity.x, entity.y, entity.z), "Sleeping zzz", 3.0

    # -- State: Work ---------------------------------------------------------

    # Consecutive no-progress ticks before an anchor is blacklisted.
    # A decide tick is ~0.5–1s for woodcutters, so 3 ticks ≈ 2–4s of
    # no motion + no chop, which is comfortably past Navigator arrival
    # jitter but short enough to abandon a wedged spot quickly.
    _STUCK_TICK_LIMIT = 3

    def _work(self, entity: SelfEntity, local_world: LocalWorld):
        spd    = entity.walk_speed
        origin = (entity.x, entity.y, entity.z)

        # Scan multiple candidates per tier so we can skip anchors the
        # executor has already failed on. `max_results=16` is enough to
        # route around a single unreachable tree without walking 80 blocks
        # of forest looking for the 17th.
        anchor = None
        for tier in self.GATHER_PRIORITY:
            stats.inc("scan_blocks")
            hits = scan_blocks(tier, near=origin,
                               max_dist=self._work_radius, max_results=16)
            for h in hits:
                key = (int(h["x"]), int(h["y"]), int(h["z"]))
                if key in self._failed_anchors:
                    continue
                anchor = h
                break
            if anchor is not None:
                break

        if anchor is None:
            # Either nothing in range, or every candidate is blacklisted.
            # Reset the blacklist so the villager can retry after wandering
            # to a new spot — terrain can change (trees grow, holes fill),
            # and the stuck trigger will re-blacklist if still unreachable.
            if self._failed_anchors:
                elog(entity.id, "work: all %d candidates blacklisted, "
                     "resetting and wandering" % len(self._failed_anchors))
                self._failed_anchors.clear()
            else:
                elog(entity.id, "work: no anchor in %.0f blocks, searching" %
                     self._work_radius)
            return self._search(entity, local_world, spd)

        anchor_key = (int(anchor["x"]), int(anchor["y"]), int(anchor["z"]))
        dist = ((anchor["x"] + 0.5 - entity.x)**2 +
                (anchor["z"] + 0.5 - entity.z)**2) ** 0.5
        logs = entity.inventory.count("logs")

        # Stuck detector: same anchor, not getting closer, not chopping.
        if anchor_key == self._last_anchor_key:
            got_closer = (self._last_anchor_dist is None or
                          dist < self._last_anchor_dist - 0.2)
            chopped = (self._last_logs_at_work is not None and
                       logs > self._last_logs_at_work)
            if not (got_closer or chopped):
                self._stuck_ticks += 1
            else:
                self._stuck_ticks = 0
        else:
            self._stuck_ticks = 0

        self._last_anchor_key   = anchor_key
        self._last_anchor_dist  = dist
        self._last_logs_at_work = logs

        if self._stuck_ticks >= self._STUCK_TICK_LIMIT:
            elog(entity.id, "work: blacklisting unreachable anchor=(%d,%d,%d) "
                 "after %d stuck ticks (d=%.1f, logs=%d)" % (
                 anchor_key[0], anchor_key[1], anchor_key[2],
                 self._stuck_ticks, dist, logs))
            self._failed_anchors.add(anchor_key)
            self._stuck_ticks     = 0
            self._last_anchor_key = None
            # Wander — not idle. A stuck villager in a pit needs to
            # physically displace; otherwise the next scan returns the
            # same cluster of nearby-but-overhead trees.
            return self._search(entity, local_world, spd)

        elog(entity.id, "work: anchor=(%d,%d,%d) self=(%.1f,%.1f,%.1f) "
             "d=%.1f logs=%d" % (
             int(anchor["x"]), int(anchor["y"]), int(anchor["z"]),
             entity.x, entity.y, entity.z, dist, logs))

        plan = [{
            "type": "harvest",
            "x": float(anchor["x"]) + 0.5,
            "y": float(anchor["y"]) + 0.5,
            "z": float(anchor["z"]) + 0.5,
            "gather_types": list(self.GATHER_PRIORITY),
            "gather_radius": GATHER_RADIUS,
            "chop_cooldown": CHOP_COOLDOWN,
            # Conservative capacity gate: if one more log fits, everything
            # else in the priority list fits too (logs are the heaviest).
            "item": "logs",
        }]
        return plan, "Chopping trees"

    def _search(self, entity: SelfEntity, local_world: LocalWorld, spd: float):
        tx, ty, tz = self.wander_target(entity, radius=20)
        goal = (int(tx), int(ty), int(tz))
        action = self._nav.navigate(entity, local_world, goal, speed=spd)
        if action:
            return action, "Searching for trees"
        return Move(tx, ty, tz, speed=spd), "Searching for trees"

    # -- State: Deposit ------------------------------------------------------

    def _find_chest(self, entity: SelfEntity):
        """Locate the nearest chest entity. Returns (eid, x, y, z) or None."""
        stats.inc("scan_entities", "chest")
        origin = (entity.x, entity.y, entity.z)
        hits = scan_entities("chest", near=origin,
                             max_dist=self.CHEST_RADIUS, max_results=1)
        if not hits:
            return None
        c = hits[0]
        return (int(c["id"]), float(c["x"]), float(c["y"]), float(c["z"]))

    def _deposit(self, entity: SelfEntity, local_world: LocalWorld):
        spd  = entity.walk_speed
        logs = entity.inventory.count("logs")

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
            return DropItem("logs", count=logs), "Dropping logs (no chest)"

        eid, cx, cy, cz = self._chest_target
        dx, dy, dz = entity.x - cx, entity.y - cy, entity.z - cz
        dist_to_chest = math.sqrt(dx*dx + dy*dy + dz*dz)

        if dist_to_chest <= STORE_RANGE:
            elog(entity.id, "deposit: StoreItem chest=%d logs=%d d=%.2f"
                 % (eid, logs, dist_to_chest))
            stats.inc("deposit", entity.type)
            return StoreItem(eid), "Depositing logs"

        elog(entity.id, "deposit: walking chest=%d d=%.2f logs=%d"
             % (eid, dist_to_chest, logs))
        goal = (int(cx), int(cy), int(cz))
        action = self._nav.navigate(entity, local_world, goal, speed=spd)
        if action:
            return action, "Carrying logs to chest"
        return Move(entity.x, entity.y, entity.z), "Waiting near chest"
