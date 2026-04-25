"""Woodcutter — villager that chops trees during the day, hauls whatever
it picked up to the nearest chest, and sleeps in place at night.

High-level planning only: pick a state (SLEEP / WORK / DEPOSIT), declare
gather priority, point the executor at candidate anchors. The executor
owns the chop loop.

State machine:
  SLEEP   -> WORK     when morning / afternoon begins
  WORK    -> DEPOSIT  when one more of the heaviest gather target wouldn't fit
  DEPOSIT -> WORK     when inventory is empty after depositing
  any     -> SLEEP    when evening / night begins
"""
import math
import traceback

from civcraft_engine import (Move, Relocate, Ground, STORE_RANGE,
                             material_value, scan_blocks, scan_entities,
                             entity_log as elog)
from actions import StoreItem
from behavior_base import Behavior
from local_world import SelfEntity, LocalWorld
from plan_steps import HarvestStep, Vec3
from stats import stats


class WoodcutterBehavior(Behavior):

    SLEEP   = "sleep"
    WORK    = "work"
    DEPOSIT = "deposit"

    WORK_RADIUS  = 80.0
    CHEST_RADIUS = 120.0

    # Priority order for the executor's in-place scan. Index 0 = highest.
    # Subclasses override to change what a villager prefers.
    GATHER_PRIORITY = ["leaves"]

    # Humanoids route movement through the A* Navigator instead of straight-line
    # steering — they can't cleanly wall-hug past chests, doors, or tight gaps.
    USE_NAVIGATOR = True

    # Candidate anchors per decide cycle. Executor cycles + blacklists; 16 is
    # plenty to route around a cluster of unreachable trees.
    _CANDIDATE_LIMIT = 16

    def __init__(self, work_radius: float = WORK_RADIUS):
        self._state           = self.SLEEP
        self._work_radius     = work_radius
        self._chest_target    = None   # (eid, x, y, z) cache for current deposit trip
        self._prev_state      = None

    # -- Top-level decide_plan ----------------------------------------------

    def decide_plan(self, entity: SelfEntity, local_world: LocalWorld):
        try:
            return self._decide_inner(entity, local_world)
        except Exception:
            elog(entity.id, "decide CRASH pos=(%r,%r,%r) inv_cap=%r time=%r\n%s" % (
                entity.x, entity.y, entity.z,
                getattr(entity, "inventory_capacity", "??"),
                getattr(local_world, "time", "??"),
                traceback.format_exc()))
            raise

    def _decide_inner(self, entity: SelfEntity, local_world: LocalWorld):
        stats.inc("decide", entity.type)
        self._update_state(entity, local_world)

        if self._state != self._prev_state:
            elog(entity.id, "state %s -> %s (inv=%.1f cap=%.1f)" % (
                self._prev_state, self._state,
                entity.inventory.total_value(), entity.inventory_capacity))
            self._chest_target = None
            self._prev_state = self._state

        if self._state == self.SLEEP:
            return self._sleep(entity)
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

        # Capacity gate: deposit when the heaviest target no longer fits,
        # return to work when the inventory is empty.
        heaviest = max(self.GATHER_PRIORITY, key=material_value)
        full  = not entity.inventory.can_accept(
            heaviest, 1, entity.inventory_capacity)
        empty = entity.inventory.total_value() <= 0
        if self._state == self.WORK and full:
            self._state = self.DEPOSIT
        elif self._state == self.DEPOSIT and empty:
            self._state = self.WORK

    # -- State: Sleep --------------------------------------------------------

    def _sleep(self, entity: SelfEntity):
        return Move(entity.x, entity.y, entity.z), "Sleeping zzz", 3.0

    # -- State: Work ---------------------------------------------------------

    def _work(self, entity: SelfEntity, local_world: LocalWorld):
        # Scan each tier in priority order and hand the executor the first
        # non-empty list. Executor owns "which anchor next" + "chop until
        # full"; Python just declares where the blocks are.
        #
        # No Y filter — chopping is infinite-height. The executor's per-swing
        # gather_radius sphere does the real reachability check.
        origin = (entity.x, entity.y, entity.z)
        heaviest = max(self.GATHER_PRIORITY, key=material_value)
        for tier in self.GATHER_PRIORITY:
            stats.inc("scan_blocks")
            hits = scan_blocks(tier, near=origin,
                               max_dist=self._work_radius,
                               max_results=self._CANDIDATE_LIMIT)
            if hits:
                return [HarvestStep(
                    candidates=[Vec3(x=float(h["x"]) + 0.5,
                                     y=float(h["y"]) + 0.5,
                                     z=float(h["z"]) + 0.5) for h in hits],
                    gather_types=list(self.GATHER_PRIORITY),
                    # Capacity-gate hint: the executor exits when one more of
                    # `item` won't fit. Lighter tiers swept up along the way
                    # also consume capacity — the WORK→DEPOSIT flip in
                    # _update_state catches that on the next decide().
                    item=heaviest,
                    use_navigator=self.USE_NAVIGATOR,
                    ignore_height=True,  # navigate to root, not the leaf cell
                )], "Chopping trees"

        elog(entity.id, "work: no tree in %.0f blocks, searching" % self._work_radius)
        return self._search(entity, local_world)

    def _search(self, entity: SelfEntity, local_world: LocalWorld):
        # If the last wander hop failed A*, back off for 10s so we don't
        # thrash the planner; the next decide picks a fresh random target.
        if local_world.last_nav_failed:
            return (Move(entity.x, entity.y, entity.z),
                    "No path while searching — %s" % local_world.last_reason,
                    10.0)
        tx, ty, tz = self.wander_target(entity, radius=20)
        return (Move(int(tx), int(ty), int(tz), use_navigator=True),
                "Searching for trees")

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
        total = entity.inventory.total_value()

        # Nothing to deposit — flip back to WORK without issuing a 0-count
        # Relocate (server rejects empty-inventory transfers).
        if total <= 0:
            elog(entity.id, "deposit: empty inventory, resuming work")
            self._state = self.WORK
            return Move(entity.x, entity.y, entity.z), "Idle (nothing to deposit)"

        # Previous tick's Move(use_navigator=True) gave up — drop the cached
        # chest so we re-scan, then idle with the reason surfaced in the goal.
        if local_world.last_nav_failed:
            elog(entity.id, "deposit: nav failed last tick (%s, %s)"
                 % (local_world.last_state, local_world.last_reason))
            self._chest_target = None
            return (Move(entity.x, entity.y, entity.z),
                    "Can't reach chest — %s" % local_world.last_reason, 10.0)

        # (Re)acquire the nearest chest. Re-scan if the cached one vanished.
        if self._chest_target is None:
            self._chest_target = self._find_chest(entity)

        if self._chest_target is None:
            # No chest reachable — dump the whole inventory at our feet.
            # Relocate without item_id moves every stack to Ground in one call.
            elog(entity.id, "no chest in %.0f blocks, dropping inventory (value=%.1f)"
                 % (self.CHEST_RADIUS, total))
            return Relocate(relocate_to=Ground()), "Dropping inventory (no chest)"

        eid, cx, cy, cz = self._chest_target
        dx, dy, dz = entity.x - cx, entity.y - cy, entity.z - cz
        dist_to_chest = math.sqrt(dx*dx + dy*dy + dz*dz)

        if dist_to_chest <= STORE_RANGE:
            # StoreItem with no item_id moves the full inventory into the chest.
            elog(entity.id, "deposit: StoreItem chest=%d value=%.1f d=%.2f"
                 % (eid, total, dist_to_chest))
            stats.inc("deposit", entity.type)
            return StoreItem(eid), "Depositing inventory"

        elog(entity.id, "deposit: walking chest=%d d=%.2f value=%.1f"
             % (eid, dist_to_chest, total))
        return (Move(int(cx), int(cy), int(cz), use_navigator=True),
                "Carrying inventory to chest")
