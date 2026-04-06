"""Woodcutter — villager that chops wood and returns home each evening.

Day cycle:
  1. Search for nearby wood blocks (trees)
  2. Walk to the closest one
  3. Chop it (BreakBlock)
  4. Pick up the dropped wood item
  5. Return home to deposit
  6. Rest briefly, maybe socialize, then repeat

Evening (time > 0.65): head home before dark.
Night (time > 0.75):   sleep at home until dawn.

Entity props (set by server at spawn):
  home_x, home_z  — home position (falls back to spawn position)
  work_radius     — how far to search for wood (default 60)
  max_radius      — max distance from home before returning (default 50)
  social_chance   — probability of socializing per rest (default 0.3)
"""
import random
from modcraft_engine import Idle, Wander, MoveTo, BreakBlock, PickupItem
from behavior_base import Behavior


class WoodcutterBehavior(Behavior):

    def __init__(self):
        self._state = "searching"
        self._timer = 0.0
        self._target_block = None
        self._home = None
        self._trips = 0
        self._social_target = None
        self._stuck_pos = None
        self._stuck_timer = 0.0

    def decide(self, entity, world):
        self._timer -= world["dt"]
        self._home = self.init_home(entity, self._home)

        work_radius = float(entity.get("work_radius", 60))
        max_radius  = float(entity.get("max_radius", 50))
        social_chance = float(entity.get("social_chance", 0.3))
        spd = entity["walk_speed"]

        dist_home = self.dist2d(entity["x"], entity["z"],
                                self._home[0], self._home[2])

        # ── Night: go home and sleep ────────────────────────────────────────
        if self.is_night(world):
            self._state = "sleeping"
            if dist_home > 3:
                return (MoveTo(self._home[0], self._home[1], self._home[2],
                               speed=spd),
                        "Heading home...")
            return Idle(), "Sleeping zzz"

        # ── Evening: head home before dark ──────────────────────────────────
        if self.is_evening(world):
            self._state = "returning"
            if dist_home > 3:
                return (MoveTo(self._home[0], self._home[1], self._home[2],
                               speed=spd),
                        "Heading home (evening)...")
            return Idle(), "Home for the night"

        # ── Wake up ─────────────────────────────────────────────────────────
        if self._state == "sleeping":
            self._state = "searching"
            self._timer = 1.0
            return Idle(), "Good morning!"

        # ── Too far from home ────────────────────────────────────────────────
        if dist_home > max_radius:
            self._state = "returning"
            return (MoveTo(self._home[0], self._home[1], self._home[2],
                           speed=spd),
                    "Too far — heading back (%dm)" % int(dist_home))

        # ── Opportunistic item pickup ────────────────────────────────────────
        items = [e for e in world["nearby"]
                 if e["category"] == "item" and e["distance"] < 2.5]
        if items:
            closest = min(items, key=lambda e: e["distance"])
            return PickupItem(closest["id"]), "Picking up item"

        # ── Greet nearby players ────────────────────────────────────────────
        if self._state in ("searching", "resting") and self._timer <= 0:
            players = [e for e in world["nearby"]
                       if e["category"] == "player" and e["distance"] < 6]
            if players and random.random() < 0.05:
                self._timer = 2.0
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
            # Stuck detection
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
                self._trips += 1
                self._state = "returning"
                self._timer = 20.0
                if self._target_block:
                    return (BreakBlock(self._target_block["x"],
                                       self._target_block["y"],
                                       self._target_block["z"]),
                            "Chopping!")
            return Idle(), "Chopping!"

        # ── State: returning home ───────────────────────────────────────────
        if self._state == "returning":
            if dist_home < 3 or self._timer <= 0:
                self._state = "resting"
                self._timer = 3.0
                return Idle(), "Home! Depositing..."
            return (MoveTo(self._home[0], self._home[1], self._home[2], speed=spd),
                    "Bringing resources home (%dm)" % int(dist_home))

        # ── State: resting ──────────────────────────────────────────────────
        if self._state == "resting":
            if self._timer <= 0:
                villagers = [e for e in world["nearby"]
                             if e["type_id"] == "base:villager"
                             and e["id"] != entity["id"] and e["distance"] < 10]
                if villagers and random.random() < social_chance:
                    self._social_target = min(villagers, key=lambda e: e["distance"])
                    self._state = "socializing"
                    self._timer = 4.0
                    return (MoveTo(self._social_target["x"],
                                   self._social_target["y"],
                                   self._social_target["z"], speed=spd * 0.6),
                            "Chatting with neighbor...")
                self._state = "searching"
                self._timer = 1.0
            return Idle(), "Taking a break"

        # ── State: socializing ──────────────────────────────────────────────
        if self._state == "socializing":
            if self._timer <= 0:
                self._state = "searching"
                self._social_target = None
                return Idle(), "Back to work!"
            return Idle(), "Chatting :)"

        # Fallback
        self._state = "searching"
        return Idle(), "Idle"
