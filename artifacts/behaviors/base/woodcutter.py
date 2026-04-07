"""Woodcutter — villager that chops wood all day and deposits at home chest.

Day cycle (no breaks — works non-stop):
  1. Search for nearby wood blocks
  2. Walk to closest one
  3. Chop it (BreakBlock)
  4. Pick up the dropped wood item
  5. When inventory reaches collect_goal, return home and StoreItem in chest
  6. Immediately start next trip

Evening: head home before dark.
Night:   sleep at home until dawn.

Entity props (set by server at spawn):
  home_x, home_z       — home position (falls back to spawn position)
  chest_x, chest_y, chest_z — chest to deposit in (defaults to home position)
  work_radius          — how far to search for wood (default 60)
  max_radius           — max distance from home before returning (default 50)
  collect_goal         — how many logs to collect before depositing (default 5)
"""
import random
from modcraft_engine import Idle, Wander, MoveTo, BreakBlock, PickupItem, StoreItem
from behavior_base import Behavior


class WoodcutterBehavior(Behavior):

    def __init__(self):
        self._state = "searching"
        self._timer = 0.0
        self._target_block = None
        self._home = None
        self._chest = None
        self._resting = False
        self._stuck_pos = None
        self._stuck_timer = 0.0

    def decide(self, entity, world):
        self._timer -= world["dt"]
        self._home = self.init_home(entity, self._home)

        # Chest: use chest_x/y/z if set, otherwise fall back to home position
        if self._chest is None:
            cx = float(entity.get("chest_x", self._home[0]))
            cy = float(entity.get("chest_y", self._home[1]))
            cz = float(entity.get("chest_z", self._home[2]))
            self._chest = (cx, cy, cz)

        work_radius  = float(entity.get("work_radius", 60))
        max_radius   = float(entity.get("max_radius",  50))
        collect_goal = int(entity.get("collect_goal",   5))
        spd = entity["walk_speed"]

        dist_home = self.dist2d(entity["x"], entity["z"],
                                self._home[0], self._home[2])
        dist_chest = self.dist2d(entity["x"], entity["z"],
                                 self._chest[0], self._chest[2])

        inventory = entity.get("inventory", {})
        log_count = inventory.get("base:log", 0) + inventory.get("base:wood", 0)

        # ── Night: go home and sleep ────────────────────────────────────────
        if self.is_night(world):
            self._resting = True
            self._state = "sleeping"
            if dist_home > 3:
                return (MoveTo(self._home[0], self._home[1], self._home[2],
                               speed=spd),
                        "Heading home...")
            return Idle(), "Sleeping zzz"

        # ── Evening: head home before dark ──────────────────────────────────
        if self.is_evening(world):
            self._resting = True
            self._state = "returning"
            if dist_home > 3:
                return (MoveTo(self._home[0], self._home[1], self._home[2],
                               speed=spd),
                        "Heading home (evening)...")
            return Idle(), "Home for the night"

        # ── Wake up ─────────────────────────────────────────────────────────
        if self._resting:
            self._resting = False
            self._state = "searching"
            self._timer = 0.5
            return Idle(), "Good morning!"

        # ── Too far from home ────────────────────────────────────────────────
        if dist_home > max_radius:
            self._state = "returning"
            return (MoveTo(self._home[0], self._home[1], self._home[2],
                           speed=spd),
                    "Too far — heading back (%dm)" % int(dist_home))

        # ── Deposit: inventory full → go store at chest ─────────────────────
        if log_count >= collect_goal and self._state not in ("depositing",):
            self._state = "depositing"

        if self._state == "depositing":
            if dist_chest < 3:
                # At chest — deposit and go back to work immediately
                self._state = "searching"
                self._timer = 0.5
                return (StoreItem(self._chest[0], self._chest[1], self._chest[2]),
                        "Depositing %d logs" % log_count)
            return (MoveTo(self._chest[0], self._chest[1], self._chest[2], speed=spd),
                    "Taking logs home (%d/%d)" % (log_count, collect_goal))

        # ── Opportunistic item pickup ────────────────────────────────────────
        items = [e for e in world["nearby"]
                 if e["category"] == "item" and e["distance"] < 2.5]
        if items:
            closest = min(items, key=lambda e: e["distance"])
            return PickupItem(closest["id"]), "Picking up item"

        # ── Greet nearby players (brief, then right back to work) ────────────
        if self._state in ("searching",) and self._timer <= 0:
            players = [e for e in world["nearby"]
                       if e["category"] == "player" and e["distance"] < 6]
            if players and random.random() < 0.05:
                self._timer = 1.5
                return Idle(), "*waves* Hello!"

        # ── State: searching ────────────────────────────────────────────────
        if self._state == "searching":
            blocks = [b for b in world["blocks"]
                      if b["type"] == "base:wood" and b["distance"] < work_radius]
            if blocks:
                self._target_block = min(blocks, key=lambda b: b["distance"])
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

        # ── State: walking to tree ──────────────────────────────────────────
        if self._state == "walking":
            if self._target_block is None:
                self._state = "searching"
                return Idle(), "Lost target — re-searching"
            dist = self.dist2d(entity["x"], entity["z"],
                               self._target_block["x"], self._target_block["z"])
            self._stuck_timer -= world["dt"]
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
                self._timer = 1.5
                return Idle(), "Chopping!"
            if self._timer <= 0:
                self._state = "searching"
                self._target_block = None
                return Idle(), "Can't reach — trying another"
            return (MoveTo(self._target_block["x"] + 0.5,
                           self._target_block["y"],
                           self._target_block["z"] + 0.5, speed=spd),
                    "Walking to tree (%dm)" % int(dist))

        # ── State: chopping ─────────────────────────────────────────────────
        if self._state == "chopping":
            if self._timer <= 0:
                self._state = "searching"   # immediately look for next tree
                self._timer = 0.5
                if self._target_block:
                    return (BreakBlock(self._target_block["x"],
                                       self._target_block["y"],
                                       self._target_block["z"]),
                            "Chopping!")
            return Idle(), "Chopping!"

        # Fallback
        self._state = "searching"
        return Idle(), "Idle"
