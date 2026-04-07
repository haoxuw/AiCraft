"""Woodcutter — villager that chops wood all day and deposits at home chest.

Day cycle (non-stop work):
  1. Search for nearby wood blocks
  2. Walk to closest one
  3. Chop it (BreakBlock)
  4. Pick up the dropped log
  5. When inventory reaches collect_goal, return home and StoreItem in chest
  6. Immediately start next trip

Evening: force home before dark (emergency — bypasses cooldown).
Night:   sleep at home until dawn.
Damaged: clear cooldown immediately so the villager can react.

Goal transition rules
---------------------
Non-emergency goal changes (e.g. "too far → return home") are guarded by
a cooldown timer.  Once a goal is committed, it stays for at least
`_goal_cooldown` seconds.  This prevents rapid flickering when the
villager is near a radius boundary.

Emergency goals (evening/night/damage) always fire immediately.

Hysteresis for "too far from home"
-----------------------------------
The returning-home flag is set when dist_home > max_radius, but only
cleared when dist_home < max_radius * 0.5.  This creates a dead-band
that stops the villager oscillating at the boundary.

Entity props (set by server at spawn):
  home_x, home_z           — home position (falls back to spawn)
  chest_x, chest_y, chest_z — chest to deposit in (falls back to home)
  work_radius               — max tree search distance (default 80)
  max_radius                — distance before returning home (default 100)
  collect_goal              — logs to collect before depositing (default 5)
  item_search_radius        — how far to look for dropped items (default 25)
"""
import random
from modcraft_engine import Idle, Wander, MoveTo, BreakBlock, PickupItem, StoreItem
from behavior_base import Behavior

# How long (seconds) a non-emergency goal is locked before it can change.
GOAL_LOCK_SECS = 6.0

# Fraction of max_radius at which _returning_home hysteresis clears.
HOME_HYSTERESIS = 0.5


class WoodcutterBehavior(Behavior):

    def __init__(self):
        self._state = "searching"
        self._timer = 0.0
        self._goal_cooldown = 0.0  # non-emergency goal lock
        self._target_block = None
        self._home = None
        self._chest = None
        self._chest_entity_id = None  # cached chest entity ID for StoreItem
        self._resting = False
        self._returning_home = False  # hysteresis flag for "too far"
        self._stuck_pos = None
        self._stuck_timer = 0.0
        self._prev_hp = -1           # damage detection

    # ── Goal transition helpers ───────────────────────────────────────────────

    def _commit(self, new_state, lock=GOAL_LOCK_SECS):
        """Non-emergency state change.  Starts the goal cooldown."""
        if self._state != new_state:
            print("[Woodcutter] %s → %s (lock=%.1fs)" % (self._state, new_state, lock))
            self._state = new_state
            self._goal_cooldown = lock

    def _emergency(self, new_state):
        """Emergency state change.  Clears the cooldown immediately."""
        if self._state != new_state:
            print("[Woodcutter] EMERGENCY %s → %s" % (self._state, new_state))
        self._state = new_state
        self._goal_cooldown = 0.0

    # ── Main decide loop ──────────────────────────────────────────────────────

    def decide(self, entity, world):
        dt = world["dt"]
        self._timer        -= dt
        self._goal_cooldown -= dt

        self._home = self.init_home(entity, self._home)

        if self._chest is None:
            cx = float(entity.get("chest_x", self._home[0]))
            cy = float(entity.get("chest_y", self._home[1]))
            cz = float(entity.get("chest_z", self._home[2]))
            self._chest = (cx, cy, cz)
            # Try to get pre-assigned chest entity ID from entity props
            ceid = entity.get("chest_entity_id", None)
            if ceid is not None:
                self._chest_entity_id = int(ceid)

        work_radius        = float(entity.get("work_radius",        80))
        max_radius         = float(entity.get("max_radius",        100))
        collect_goal       = int(entity.get("collect_goal",          5))
        item_search_radius = float(entity.get("item_search_radius",  25))
        spd          = entity["walk_speed"]

        dist_home  = self.dist2d(entity["x"], entity["z"],
                                 self._home[0],  self._home[2])
        dist_chest = self.dist2d(entity["x"], entity["z"],
                                 self._chest[0], self._chest[2])

        inventory  = entity.get("inventory", {})
        log_count  = inventory.get("base:log", 0) + inventory.get("base:wood", 0)
        if log_count > 0 and log_count != getattr(self, '_last_log_count', 0):
            self._last_log_count = log_count
            print("[Woodcutter] Inventory update: %d/%d logs %s" % (log_count, collect_goal, dict(inventory)))

        # ── EMERGENCY: damage clears goal lock so we can react ───────────────
        current_hp = entity.get("hp", 999)
        if self._prev_hp >= 0 and current_hp < self._prev_hp:
            self._goal_cooldown = 0.0
        self._prev_hp = current_hp

        # ── EMERGENCY: night → sleep at home ────────────────────────────────
        if self.is_night(world):
            self._resting = True
            self._emergency("sleeping")
            if dist_home > 3:
                return (MoveTo(self._home[0], self._home[1], self._home[2],
                               speed=spd),
                        "Heading home...")
            return Idle(), "Sleeping zzz"

        # ── EMERGENCY: evening → go home before dark ─────────────────────────
        if self.is_evening(world):
            self._resting = True
            self._emergency("evening")
            if dist_home > 3:
                return (MoveTo(self._home[0], self._home[1], self._home[2],
                               speed=spd),
                        "Heading home (evening)...")
            return Idle(), "Home for the night"

        # ── Wake up ──────────────────────────────────────────────────────────
        if self._resting:
            self._resting = False
            self._returning_home = False
            self._state = "searching"
            self._goal_cooldown = 1.0
            return Idle(), "Good morning!"

        # ── Hysteresis: too far from home ─────────────────────────────────────
        # Set flag at max_radius; only clear it when back inside max_radius*0.5.
        # This dead-band prevents oscillation at the boundary.
        if dist_home > max_radius:
            self._returning_home = True
        if self._returning_home and dist_home < max_radius * HOME_HYSTERESIS:
            self._returning_home = False

        # ── Non-emergency state triggers (respect goal cooldown) ──────────────
        if self._goal_cooldown <= 0:

            # Inventory full → go deposit
            if log_count >= collect_goal and self._state != "depositing":
                print("[Woodcutter] Inventory full (%d/%d logs) — heading to chest" % (log_count, collect_goal))
                self._commit("depositing", lock=0.0)  # no extra lock; deposit is fast

            # Too far → return home (hysteresis prevents re-triggering immediately)
            elif self._returning_home and self._state not in ("returning", "depositing"):
                self._commit("returning", lock=GOAL_LOCK_SECS)

        # ── State: depositing ─────────────────────────────────────────────────
        if self._state == "depositing":
            # Find chest entity in nearby (radius 64, so always visible)
            if self._chest_entity_id is None:
                chests = [e for e in world["nearby"] if e["category"] == "chest"]
                if chests:
                    # Pick the chest closest to our assigned chest position
                    def chest_dist(e):
                        dx = e["x"] - self._chest[0]
                        dz = e["z"] - self._chest[2]
                        return dx*dx + dz*dz
                    self._chest_entity_id = min(chests, key=chest_dist)["id"]
                    print("[Woodcutter] Found chest entity %d" % self._chest_entity_id)

            if dist_chest < 20:
                if self._chest_entity_id is not None:
                    print("[Woodcutter] Depositing %d logs into chest entity %d" % (
                        log_count, self._chest_entity_id))
                    self._commit("searching", lock=1.0)
                    return (StoreItem(self._chest_entity_id),
                            "Depositing %d logs" % log_count)
                else:
                    print("[Woodcutter] At chest but no entity found yet, waiting...")
            return (MoveTo(self._chest[0], self._chest[1], self._chest[2], speed=spd),
                    "Taking logs home (%d/%d)" % (log_count, collect_goal))

        # ── State: returning home ─────────────────────────────────────────────
        if self._state == "returning":
            if dist_home < max_radius * HOME_HYSTERESIS:
                self._commit("searching", lock=2.0)
            else:
                return (MoveTo(self._home[0], self._home[1], self._home[2], speed=spd),
                        "Heading back (%dm from home)" % int(dist_home))

        # ── Opportunistic item pickup ─────────────────────────────────────────
        # Search within item_search_radius (default 25) to catch logs that fell
        # from treetops.  Walk within 5 blocks so the server accepts the pickup
        # (server-wide pickupRange defaults to 16; 5 blocks is a safe margin).
        items = [e for e in world["nearby"]
                 if e["category"] == "item" and e["distance"] < item_search_radius]
        if items:
            closest = min(items, key=lambda e: e["distance"])
            if closest["distance"] < 5.0:
                return PickupItem(closest["id"]), "Picking up item"
            else:
                return (MoveTo(closest["x"], closest["y"], closest["z"], speed=spd),
                        "Moving to item (%dm)" % int(closest["distance"]))

        # ── Greet nearby players (very brief, never interrupts a locked goal) ─
        if self._state == "searching" and self._timer <= 0:
            players = [e for e in world["nearby"]
                       if e["category"] == "player" and e["distance"] < 6]
            if players and random.random() < 0.05:
                self._timer = 1.5
                return Idle(), "*waves* Hello!"

        # ── State: searching ──────────────────────────────────────────────────
        if self._state in ("searching", "returning"):
            # (returning with cleared hysteresis lands here to resume searching)
            all_blocks = world["blocks"]
            if not hasattr(self, '_block_log_timer'):
                self._block_log_timer = 0.0
            self._block_log_timer -= dt
            if self._block_log_timer <= 0:
                self._block_log_timer = 10.0
                wood_types = [b["type"] for b in all_blocks if "wood" in b["type"] or "log" in b["type"]]
                print("[Woodcutter] Block scan: %d types total, wood/log: %s (pos=%.0f,%.0f,%.0f)" % (
                    len(all_blocks), wood_types, entity["x"], entity["y"], entity["z"]))
            blocks = [b for b in all_blocks
                      if b["type"] == "base:wood" and b["distance"] < work_radius]
            if blocks:
                self._target_block = min(blocks, key=lambda b: b["distance"])
                # Natural transition — no goal lock needed for in-task steps
                self._state = "walking"
                self._timer = 20.0
                self._stuck_pos = (entity["x"], entity["z"])
                self._stuck_timer = 5.0
                return (MoveTo(self._target_block["x"] + 0.5,
                               self._target_block["y"],
                               self._target_block["z"] + 0.5, speed=spd),
                        "Found wood! Going to chop.")
            if self._timer <= 0:
                self._timer = 2.0
            return Wander(speed=spd * 0.7), "Searching for trees..."

        # ── State: walking to tree ────────────────────────────────────────────
        if self._state == "walking":
            if self._target_block is None:
                self._state = "searching"
                return Idle(), "Lost target — re-searching"
            dist = self.dist2d(entity["x"], entity["z"],
                               self._target_block["x"], self._target_block["z"])
            self._stuck_timer -= dt
            if self._stuck_timer <= 0:
                self._stuck_timer = 5.0
                if self._stuck_pos is not None:
                    moved = self.dist2d(entity["x"], entity["z"],
                                        self._stuck_pos[0], self._stuck_pos[1])
                    if moved < 1.0:
                        self._state = "searching"
                        self._target_block = None
                        return Idle(), "Stuck — searching elsewhere"
                self._stuck_pos = (entity["x"], entity["z"])

            if dist < 2.5:
                self._state = "chopping"
                self._timer = 0.4
                return Idle(), "Chopping!"
            if self._timer <= 0:
                self._state = "searching"
                self._target_block = None
                return Idle(), "Can't reach — trying another"
            return (MoveTo(self._target_block["x"] + 0.5,
                           self._target_block["y"],
                           self._target_block["z"] + 0.5, speed=spd),
                    "Walking to tree (%dm)" % int(dist))

        # ── State: chopping ───────────────────────────────────────────────────
        if self._state == "chopping":
            if self._timer <= 0:
                if self._target_block:
                    bx, by, bz = (self._target_block["x"],
                                  self._target_block["y"],
                                  self._target_block["z"])
                    # After breaking, look for more tree blocks within chop_radius.
                    # This keeps the villager working through the whole tree before wandering.
                    # chop_radius must stay under server BreakBlock range (8 blocks 3D)
                    chop_radius = float(entity.get("chop_radius", 7))
                    nearby_tree = [b for b in world["blocks"]
                                   if b["type"] in ("base:wood", "base:leaves")
                                   and b["distance"] < chop_radius]
                    if nearby_tree:
                        # More tree blocks nearby — stay in chopping state for next block.
                        # The scanner will have updated after the BreakBlock is processed.
                        self._target_block = min(nearby_tree, key=lambda b: b["distance"])
                        self._timer = 0.4
                        return (BreakBlock(bx, by, bz), "Chopping tree! (%d blocks left)" % len(nearby_tree))
                    else:
                        # Tree cleared — resume searching.
                        self._state = "searching"
                        self._timer = 0.5
                        return (BreakBlock(bx, by, bz), "Felled a tree!")
                else:
                    self._state = "searching"
                    self._timer = 0.5
            return Idle(), "Chopping!"

        # Fallback
        self._state = "searching"
        return Idle(), "Idle"
