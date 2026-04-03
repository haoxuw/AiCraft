"""Woodcutter — villager that gathers resources by day and sleeps at night.

Day cycle:
  1. Search for wood or stone blocks
  2. Walk to the nearest one
  3. Chop it down (BreakBlock)
  4. Walk back toward spawn/chest area to "deposit"
  5. Rest briefly, then repeat

Night cycle:
  - Return home (spawn area) and sleep until dawn

State machine: searching → walking → chopping → returning → resting → searching
At night: sleeping (overrides all other states)
"""
from agentworld_engine import Idle, Wander, MoveTo, BreakBlock

_state = "searching"
_timer = 0
_target_block = None
_home = None
_trips = 0  # count gather trips for variety

def decide(self, world):
    global _state, _timer, _target_block, _home, _trips
    _timer -= world["dt"]

    # Remember home (first position = spawn area near chest)
    if _home is None:
        _home = (self["x"], self["y"], self["z"])

    time = world.get("time", 0.5)
    is_night = time > 0.75 or time < 0.25

    # ── Night: go home and sleep ──
    if is_night:
        dx = self["x"] - _home[0]
        dz = self["z"] - _home[2]
        dist_home = (dx * dx + dz * dz) ** 0.5
        if dist_home > 4:
            self["goal"] = "Heading home..."
            _state = "sleeping"
            return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"])
        self["goal"] = "Sleeping zzz"
        _state = "sleeping"
        return Idle()

    # Waking up after night
    if _state == "sleeping":
        _state = "searching"
        _timer = 0
        self["goal"] = "Good morning!"

    # ── State: searching ──
    if _state == "searching":
        # Alternate between wood and stone
        target_type = "base:stone" if _trips % 3 == 2 else "base:wood"
        blocks = [b for b in world["blocks"] if b["type"] == target_type]
        if blocks:
            _target_block = min(blocks, key=lambda b: b["distance"])
            _state = "walking"
            _timer = 10.0
            name = target_type.split(":")[1]
            self["goal"] = "Found %s!" % name
            return MoveTo(
                _target_block["x"] + 0.5, _target_block["y"],
                _target_block["z"] + 0.5, speed=self["walk_speed"])
        self["goal"] = "Looking for resources..."
        if _timer <= 0:
            _timer = 2.0
        return Wander(speed=self["walk_speed"] * 0.7)

    # ── State: walking to target ──
    if _state == "walking":
        if _target_block is None:
            _state = "searching"
            return Idle()
        dx = self["x"] - _target_block["x"]
        dz = self["z"] - _target_block["z"]
        dist = (dx * dx + dz * dz) ** 0.5
        if dist < 2.5:
            _state = "chopping"
            _timer = 1.5
            self["goal"] = "Mining!"
            return Idle()
        if _timer <= 0:
            _state = "searching"
            self["goal"] = "Can't reach, trying another"
            return Idle()
        self["goal"] = "Walking to resource (%dm)" % int(dist)
        return MoveTo(
            _target_block["x"] + 0.5, _target_block["y"],
            _target_block["z"] + 0.5, speed=self["walk_speed"])

    # ── State: chopping ──
    if _state == "chopping":
        self["goal"] = "Mining!"
        if _timer <= 0:
            _state = "returning"
            _timer = 12.0
            _trips += 1
            if _target_block:
                return BreakBlock(
                    _target_block["x"], _target_block["y"], _target_block["z"])
        return Idle()

    # ── State: returning to chest/home ──
    if _state == "returning":
        dx = self["x"] - _home[0]
        dz = self["z"] - _home[2]
        dist_home = (dx * dx + dz * dz) ** 0.5
        if dist_home < 3:
            _state = "resting"
            _timer = 2.0
            self["goal"] = "Depositing at chest"
            return Idle()
        if _timer <= 0:
            _state = "resting"
            _timer = 1.0
            return Idle()
        self["goal"] = "Bringing resources home"
        return MoveTo(_home[0], _home[1], _home[2], speed=self["walk_speed"])

    # ── State: resting ──
    if _state == "resting":
        self["goal"] = "Taking a break"
        if _timer <= 0:
            _state = "searching"
            _timer = 1.0
        return Idle()

    _state = "searching"
    return Idle()
