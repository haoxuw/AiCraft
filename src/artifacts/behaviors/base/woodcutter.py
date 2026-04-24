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

  Capacity is the universal rule across the game: inventory_capacity ==
  max_hp == material_value. A villager's body is worth 20; it carries 20
  value worth of items. Collect until maxed, then haul to the chest.

Behavior-local tuning (class defaults, overridable via __init__):
  WORK_RADIUS   — max radius to search for trees (default 80)
  CHEST_RADIUS  — max radius to search for chests (default 120)

State machine:
  SLEEP   -> WORK     when morning / afternoon begins
  WORK    -> DEPOSIT  when one more log wouldn't fit
  DEPOSIT -> WORK     when inventory is empty after depositing
  any     -> SLEEP    when evening / night begins
"""
import math
import traceback

from civcraft_engine import Move, Navigator, material_value, scan_blocks, scan_entities
from actions import StoreItem, DropItem
from behavior_base import Behavior
from entity_log import log as elog
from local_world import SelfEntity, LocalWorld
from stats import stats

STORE_RANGE  = 5.0   # matches server-side StoreItem range. The slack (vs
                     # Navigator's arrive radius 1.5) covers cases where the
                     # planner's partial path stops just outside a wall with
                     # the chest one block inside — wall(1) + chest_offset(1)
                     # + collision(0.5) + heuristic_trap slack fits in 5.
CHOP_COOLDOWN = 1.0  # seconds between swings (executor-enforced)
GATHER_RADIUS = 6.0  # executor scans this sphere around the villager each swing


class WoodcutterBehavior(Behavior):

    SLEEP   = "sleep"
    WORK    = "work"
    DEPOSIT = "deposit"

    WORK_RADIUS  = 80.0
    CHEST_RADIUS = 120.0

    # Priority order for the executor's in-place scan. Index 0 = highest:
    # every swing, the executor scans the local volume for tier 0 first and
    # only falls through to tier 1 when tier 0 has zero hits in range.
    # Override on a subclass to change what a villager prefers.
    GATHER_PRIORITY = ["leaves", "logs"]

    # Humanoids route movement through the A* Navigator instead of straight-line
    # steering — they can't cleanly wall-hug past chests, doors, or tight gaps.
    # Subclasses (or a future EntityDef flag) can flip this off for simpler
    # mobs. Used to set `use_navigator` on emitted Move / harvest PlanSteps.
    USE_NAVIGATOR = True

    def __init__(self, work_radius: float = WORK_RADIUS):
        self._state           = self.SLEEP
        self._work_radius     = work_radius
        self._nav             = Navigator()
        self._chest_target    = None   # (eid, x, y, z) cache for current deposit trip
        self._prev_state      = None

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
            self._nav.clear()
            self._chest_target = None
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

        # Capacity-based deposit gate: once one more log wouldn't fit, the
        # body is "full enough" and we haul. inventory_capacity == max_hp ==
        # material_value for every living — the rule is uniform, humanoids
        # no longer exempt. Leaves picked up during chopping count toward
        # the total, so the exact log count at DEPOSIT varies (4–5 for a
        # villager body of 20). That's fine: always collect until maxed.
        full = not entity.inventory.can_accept(
            "logs", 1, entity.inventory_capacity)
        empty = entity.inventory.total_value() <= 0
        if self._state == self.WORK and full:
            self._state = self.DEPOSIT
        elif self._state == self.DEPOSIT and empty:
            self._state = self.WORK

    # -- State: Sleep --------------------------------------------------------

    def _sleep(self, entity: SelfEntity, local_world: LocalWorld):
        """Sleep in place — no home coordinate needed."""
        return Move(entity.x, entity.y, entity.z), "Sleeping zzz", 3.0

    # -- State: Work ---------------------------------------------------------

    # How many candidate anchors to hand the executor per decide cycle.
    # Executor cycles them in order, blacklisting wedged or picked-clean
    # slots. 16 is plenty to route around a cluster of unreachable trees
    # without walking 80 blocks of forest looking for the 17th.
    _CANDIDATE_LIMIT = 16

    def _work(self, entity: SelfEntity, local_world: LocalWorld):
        spd    = entity.walk_speed
        origin = (entity.x, entity.y, entity.z)
        logs   = entity.inventory.count("logs")

        # Count goal for this decide cycle: derive from remaining capacity so
        # the executor only loops until the body is (roughly) full. "Roughly"
        # because leaves that get swept up during the harvest also consume
        # capacity — the Python-side WORK→DEPOSIT gate re-checks can_accept
        # on the next decide() anyway.
        remaining_cap = max(0.0, entity.inventory_capacity - entity.inventory.total_value())
        logs_that_fit = max(1, int(remaining_cap / material_value("logs")))

        # Scan each gather tier and keep the highest-priority one that has any
        # hits. The executor owns "which anchor to stand at next" — this
        # function just declares where harvestable blocks exist and how many
        # logs we still need.
        #
        # No Y filter: chopping is intentionally infinite-height. A villager
        # can work its way up a tall tree by standing on the trunk it just
        # broke, so overhead leaves/logs are legitimate targets — the
        # executor's per-swing gather_radius sphere does the real reachability
        # check at chop time.
        candidates = []
        for tier in self.GATHER_PRIORITY:
            stats.inc("scan_blocks")
            hits = scan_blocks(tier, near=origin,
                               max_dist=self._work_radius,
                               max_results=self._CANDIDATE_LIMIT)
            if hits:
                candidates = hits
                break

        if not candidates:
            elog(entity.id,
                 "work: no tree in %.0f blocks, searching"
                 % self._work_radius)
            return self._search(entity, local_world, spd)

        # Candidates are the *hit* cells (leaf / log — often midair for leaves).
        # The C++ executor walks down the column to find the standable ground
        # under each hit when `ignore_height` is set, so decide() never needs
        # the chunk loaded to probe terrain — the server always has it.
        primary_hit = candidates[0]
        dist = ((primary_hit["x"] + 0.5 - entity.x)**2 +
                (primary_hit["z"] + 0.5 - entity.z)**2) ** 0.5
        elog(entity.id,
             "work: hit=(%d,%d,%d) self=(%.1f,%.1f,%.1f) "
             "d=%.1f logs=%d candidates=%d" % (
             int(primary_hit["x"]), int(primary_hit["y"]), int(primary_hit["z"]),
             entity.x, entity.y, entity.z, dist, logs, len(candidates)))

        plan = [{
            "type": "harvest",
            "candidates": [{"x": float(h["x"]) + 0.5,
                            "y": float(h["y"]) + 0.5,
                            "z": float(h["z"]) + 0.5} for h in candidates],
            "gather_types": list(self.GATHER_PRIORITY),
            "gather_radius": GATHER_RADIUS,
            "chop_cooldown": CHOP_COOLDOWN,
            # Let the executor keep chopping until we've collected the
            # remaining quota, so we don't ping decide() between every swing.
            "count_goal": logs_that_fit,
            # Conservative capacity gate: if one more log fits, everything
            # else in the priority list fits too (logs are the heaviest).
            "item": "logs",
            # Route approach via A* Navigator (humanoids).
            "use_navigator": self.USE_NAVIGATOR,
            # Navigate to the root, not the leaf cell.
            "ignore_height": True,
        }]
        return plan, "Chopping trees"

    def _search(self, entity: SelfEntity, local_world: LocalWorld, spd: float):
        tx, ty, tz = self.wander_target(entity, radius=20)
        self._nav.set_goal(int(tx), int(ty), int(tz))
        action = self._nav.next_action(entity)
        if self._nav.status() == "failed":
            reason = self._nav.failure_reason() or "no valid path"
            elog(entity.id, "search: nav failed — %s" % reason)
            return (Move(entity.x, entity.y, entity.z),
                    "No path while searching — %s" % reason, 10.0)
        action.speed = spd
        return action, "Searching for trees"

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
            self._nav.clear()

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
        self._nav.set_goal(int(cx), int(cy), int(cz))
        action = self._nav.next_action(entity)
        if self._nav.status() == "failed":
            reason = self._nav.failure_reason() or "no valid path"
            elog(entity.id, "deposit: nav failed — %s" % reason)
            # Drop the cached chest so the next cycle re-scans; stand in place
            # and show the failure as the goal text. Coords in `reason` are
            # clickable in the Inspect panel.
            self._chest_target = None
            return (Move(entity.x, entity.y, entity.z),
                    "Can't reach chest — %s" % reason, 10.0)
        action.speed = spd
        return action, "Carrying logs to chest"
