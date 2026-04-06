"""Woodcutter — villager that chops wood and returns home at night.

Day cycle:
  1. Search for nearby wood blocks (trees)
  2. Walk to the closest one
  3. Chop it (BreakBlock)
  4. Pick up the dropped wood item
  5. Return home to deposit
  6. Rest briefly, maybe socialize, then repeat

Night cycle:
  - Walk home and sleep until dawn

Parameters (readable via self dict, set by server at spawn):
  home_x, home_z  — home position (assigned from village house layout)
  work_radius     — how far to search for wood (default 35)
  social_chance   — probability of socializing per rest (default 0.3)
"""
import random

from agentica_engine import Idle, Wander, MoveTo, BreakBlock, PickupItem

_state = "searching"
_timer = 0.0
_target_block = None
_home = None
_trips = 0
_social_target = None
_stuck_pos = None
_stuck_timer = 0.0


def _dist2d(ax, az, bx, bz):
    dx, dz = ax - bx, az - bz
    return (dx * dx + dz * dz) ** 0.5


def decide(self, world):
    global _state, _timer, _target_block, _home, _trips, _social_target
    global _stuck_pos, _stuck_timer

    dt = world["dt"]
    _timer -= dt
    time = world.get("time", 0.5)
    is_night = time > 0.75 or time < 0.25

    # Initialize home from entity attributes assigned by server at spawn.
    # Falls back to spawn position if no house was assigned.
    if _home is None:
        hx = self.get("home_x")
        hz = self.get("home_z")
        if hx is not None and hz is not None:
            _home = (float(hx), self["y"], float(hz))
        else:
            _home = (self["x"], self["y"], self["z"])

    work_radius = float(self.get("work_radius", 35))
    social_chance = float(self.get("social_chance", 0.3))

    # ── Night: walk home and sleep ───────────────────────────────────────
    if is_night:
        dist_home = _dist2d(self["x"], self["z"], _home[0], _home[2])
        if dist_home > 3:
            self["goal"] = "Heading home..."
            _state = "sleeping"
            return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"])
        self["goal"] = "Sleeping zzz"
        _state = "sleeping"
        return Idle()

    # ── Wake up ──────────────────────────────────────────────────────────
    if _state == "sleeping":
        _state = "searching"
        _timer = 1.0
        self["goal"] = "Good morning!"
        return Idle()

    # ── Opportunistic item pickup (any state except sleeping) ─────────────
    # After a BreakBlock, the dropped item entity spawns at the block position.
    # Grab it before moving on.
    nearby_items = [e for e in world["nearby"]
                    if e["category"] == "item" and e["distance"] < 2.5]
    if nearby_items:
        closest = min(nearby_items, key=lambda e: e["distance"])
        self["goal"] = "Picking up item"
        return PickupItem(closest["id"])

    # ── Greet nearby players ─────────────────────────────────────────────
    if _state in ("searching", "resting") and _timer <= 0:
        players = [e for e in world["nearby"]
                   if e["category"] == "player" and e["distance"] < 6]
        if players and random.random() < 0.05:
            _timer = 2.0
            self["goal"] = "*waves* Hello!"
            return Idle()

    # ── State: searching for a tree ──────────────────────────────────────
    if _state == "searching":
        blocks = [b for b in world["blocks"]
                  if b["type"] == "base:wood" and b["distance"] < work_radius]
        if blocks:
            _target_block = min(blocks, key=lambda b: b["distance"])
            _state = "walking"
            _timer = 20.0
            _stuck_pos = (self["x"], self["z"])
            _stuck_timer = 5.0
            self["goal"] = "Found wood! Going to chop."
            return MoveTo(_target_block["x"] + 0.5, _target_block["y"],
                          _target_block["z"] + 0.5, speed=self["walk_speed"])
        self["goal"] = "Searching for trees..."
        if _timer <= 0:
            _timer = 2.0
        return Wander(speed=self["walk_speed"] * 0.7)

    # ── State: walking to target block ───────────────────────────────────
    if _state == "walking":
        if _target_block is None:
            _state = "searching"
            return Idle()
        dist = _dist2d(self["x"], self["z"],
                       _target_block["x"], _target_block["z"])

        # Stuck detection: if barely moved in 5 seconds, give up and re-search
        _stuck_timer -= dt
        if _stuck_timer <= 0:
            _stuck_timer = 5.0
            if _stuck_pos is not None:
                moved = _dist2d(self["x"], self["z"], _stuck_pos[0], _stuck_pos[1])
                if moved < 1.0:
                    _state = "searching"
                    _target_block = None
                    self["goal"] = "Stuck — searching elsewhere"
                    return Idle()
            _stuck_pos = (self["x"], self["z"])

        if dist < 2.5:
            _state = "chopping"
            _timer = 1.5
            self["goal"] = "Chopping!"
            return Idle()
        if _timer <= 0:
            _state = "searching"
            _target_block = None
            self["goal"] = "Can't reach — trying another"
            return Idle()
        self["goal"] = "Walking to tree (%dm)" % int(dist)
        return MoveTo(_target_block["x"] + 0.5, _target_block["y"],
                      _target_block["z"] + 0.5, speed=self["walk_speed"])

    # ── State: chopping ──────────────────────────────────────────────────
    if _state == "chopping":
        self["goal"] = "Chopping!"
        if _timer <= 0:
            _trips += 1
            _state = "returning"
            _timer = 20.0
            if _target_block:
                return BreakBlock(_target_block["x"], _target_block["y"],
                                  _target_block["z"])
        return Idle()

    # ── State: returning home ────────────────────────────────────────────
    if _state == "returning":
        dist_home = _dist2d(self["x"], self["z"], _home[0], _home[2])
        if dist_home < 3 or _timer <= 0:
            _state = "resting"
            _timer = 3.0
            self["goal"] = "Home! Depositing..."
            return Idle()
        self["goal"] = "Bringing resources home (%dm)" % int(dist_home)
        return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"])

    # ── State: resting (maybe socialize) ────────────────────────────────
    if _state == "resting":
        self["goal"] = "Taking a break"
        if _timer <= 0:
            villagers = [e for e in world["nearby"]
                         if e["type_id"] == "base:villager"
                         and e["id"] != self["id"] and e["distance"] < 10]
            if villagers and random.random() < social_chance:
                _social_target = min(villagers, key=lambda e: e["distance"])
                _state = "socializing"
                _timer = 4.0
                self["goal"] = "Chatting with neighbor..."
                return MoveTo(_social_target["x"], _social_target["y"],
                              _social_target["z"], speed=self["walk_speed"] * 0.6)
            _state = "searching"
            _timer = 1.0
        return Idle()

    # ── State: socializing ───────────────────────────────────────────────
    if _state == "socializing":
        if _timer <= 0:
            _state = "searching"
            _social_target = None
            self["goal"] = "Back to work!"
            return Idle()
        self["goal"] = "Chatting :)"
        return Idle()

    # Fallback
    _state = "searching"
    return Idle()
