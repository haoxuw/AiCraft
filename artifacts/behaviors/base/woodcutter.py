"""Woodcutter — chops nearby tree trunks and deposits logs at home chest.

Decision logic (simple, linear priority):
  1. Night / evening → go home and sleep.
  2. Inventory full  → walk to chest and deposit all logs.
  3. Too far from home → return home.
  4. Found a trunk within work_radius → walk to it and chop.
  5. Otherwise → wander and keep searching.

Entity props (set by server at spawn):
  home_x, home_z           — home XZ (falls back to spawn position)
  chest_x, chest_y, chest_z — deposit chest position (falls back to home)
  work_radius               — max trunk search distance (default 80)
  max_radius                — max distance from home before returning (default 100)
  collect_goal              — logs to collect before depositing (default 5)
"""
from modcraft_engine import Idle, Wander, MoveTo, ConvertObject, StoreItem
from behavior_base import Behavior


class WoodcutterBehavior(Behavior):

    def __init__(self):
        self._home             = None
        self._chest            = None
        self._chest_entity_id  = None  # cached chest entity ID for StoreItem
        self._chop_cooldown    = 0.0   # seconds until next ConvertObject allowed
        self._depositing       = False  # latched True when full, cleared when empty
        self._search_log_timer = 0.0

    # ── Main decision loop ────────────────────────────────────────────────────

    def decide(self, entity, world):
        dt           = world.dt
        self._chop_cooldown    -= dt
        self._search_log_timer -= dt

        self._home  = self.init_home(entity, self._home)
        self._chest = self.get_chest(entity, self._home)
        spd          = entity.walk_speed
        collect_goal = int(entity.get("collect_goal", 5))
        work_radius  = float(entity.get("work_radius", 80))
        max_radius   = float(entity.get("max_radius", 100))

        logs = entity.inventory.count("base:trunk")

        # ── 1. Night / evening: go home ───────────────────────────────────────
        if self.is_night(world) or self.is_evening(world):
            self._depositing = False
            if self.is_near(entity, self._home, threshold=3):
                return Idle(), "Sleeping zzz"
            return MoveTo(*self._home, speed=spd), "Heading home"

        # ── 2. Latch deposit intent: full → True, empty → False ───────────────
        if logs >= collect_goal:
            self._depositing = True
        elif logs == 0:
            self._depositing = False

        if self._depositing:
            return self._deposit(entity, world, spd, logs, collect_goal)

        # ── 3. Too far from home: return ──────────────────────────────────────
        dist_home = self.dist2d(entity.x, entity.z, self._home[0], self._home[2])
        if dist_home > max_radius:
            return MoveTo(*self._home, speed=spd), \
                   "Too far — heading back (%.0fm)" % dist_home

        # ── 4. Find nearest choppable block ──────────────────────────────────
        # Try trunk first (preferred); fall back to leaves if no trunk visible.
        # Parametrized on chop_type so ConvertObject and goal text are generic.
        to_chop = None
        chop_type = None
        for object_type in ("base:trunk", "base:leaves"):
            candidate = world.get(object_type, max_dist=work_radius)
            if candidate:
                to_chop = candidate
                chop_type = object_type
                break

        if to_chop:
            bx, by, bz = to_chop.x, to_chop.y, to_chop.z
            label = chop_type.split(":")[1]   # "trunk" or "leaves"
            if self.is_near(entity, to_chop, threshold=2.5):
                if self._chop_cooldown <= 0:
                    self._chop_cooldown = 0.4   # 1 chop per 0.4 s; server roundtrip
                    return (ConvertObject(from_item=chop_type, to_item=chop_type,
                                         block_pos=(bx, by, bz),
                                         convert_from_block=True, direct=True),
                            "Chopping %s! (%d/%d)" % (label, logs, collect_goal))
                return Idle(), "Chopping..."
            # Not close yet — walk toward block
            if self.check_stuck(entity, dt):
                self.reset_stuck()
                return Wander(speed=spd), "Stuck — wandering"
            return MoveTo(bx + 0.5, by, bz + 0.5, speed=spd), \
                   "Walking to %s (%.0fm)" % (label, to_chop.distance)

        # ── 5. No trunks visible — wander and keep searching ──────────────────
        self._log_searching(entity, world, work_radius)
        return Wander(speed=spd * 0.7), "Searching for trees..."

    # ── Deposit sub-routine ───────────────────────────────────────────────────

    def _deposit(self, entity, world, spd, logs, collect_goal):
        chest_ent = world.nearest("chest")
        if chest_ent:
            self._chest_entity_id = chest_ent.id

        if self._chest_entity_id and self.is_near(entity, self._chest, threshold=20):
            print("[Woodcutter #%d] Depositing %d logs into chest entity %d" % (
                entity.id, logs, self._chest_entity_id))
            return StoreItem(self._chest_entity_id), \
                   "Depositing %d logs" % logs

        return MoveTo(*self._chest, speed=spd), \
               "Carrying logs home (%d/%d)" % (logs, collect_goal)

    # ── Diagnostic logging ────────────────────────────────────────────────────

    def _log_searching(self, entity, world, work_radius):
        if self._search_log_timer > 0:
            return
        self._search_log_timer = 10.0

        trunks = world.all("base:trunk")
        leaves = world.all("base:leaves")
        total  = len(world.blocks)

        print("[Woodcutter #%d] Searching: %d blocks visible | trunks=%d nearest=%.0fm"
              " | leaves=%d (pos=%.0f,%.0f,%.0f work_radius=%.0f)" % (
            entity.id, total,
            len(trunks), trunks[0].distance if trunks else 999,
            len(leaves),
            entity.x, entity.y, entity.z, work_radius))

        if leaves and not trunks:
            print("[Woodcutter #%d] WARNING: leaves visible but no trunks — "
                  "forest depleted or chunks still loading. Wandering further." % entity.id)
