"""Woodcutter — villager that gathers resources, farms, and socializes.

Day cycle:
  1. Search for wood or stone blocks
  2. Walk to the nearest one
  3. Chop/mine it (BreakBlock)
  4. Walk back toward home to "deposit"
  5. Rest briefly, socialize with nearby villagers
  6. Repeat (alternates between wood and stone)

Night cycle:
  - Return home and sleep until dawn

Socializing:
  - Occasionally walks to a nearby villager and idles ("Chatting")
  - Waves at nearby players (brief greeting)

Parameters (optional via self dict):
  work_radius   — how far to search for resources (default 30)
  social_chance  — probability of socializing per rest (default 0.3)
"""
from agentica_engine import Idle, Wander, MoveTo, BreakBlock

_state = "searching"
_timer = 0
_target_block = None
_home = None
_trips = 0
_social_target = None

_rng_seeded = False

def decide(self, world):
    global _state, _timer, _target_block, _home, _trips, _social_target, _rng_seeded
    if not _rng_seeded:
        random.seed(self["id"] * 31337 + 42)
        _rng_seeded = True
    _timer -= world["dt"]

    work_radius = self.get("work_radius", 30)
    social_chance = self.get("social_chance", 0.3)

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

    # Waking up
    if _state == "sleeping":
        _state = "searching"
        _timer = 0
        self["goal"] = "Good morning!"

    # ── Greet nearby players (brief wave) ──
    players = [e for e in world["nearby"]
               if e["category"] == "player" and e["distance"] < 6]
    if players and _state in ("searching", "resting") and _timer <= 0:
        import random
        if random.random() < 0.05:
            _timer = 2.0
            self["goal"] = "*waves* Hello!"
            return Idle()

    # ── State: searching ──
    if _state == "searching":
        target_type = "base:stone" if _trips % 3 == 2 else "base:wood"
        blocks = [b for b in world["blocks"]
                  if b["type"] == target_type and b["distance"] < work_radius]
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

    # ── State: returning home ──
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

    # ── State: resting (may socialize) ──
    if _state == "resting":
        self["goal"] = "Taking a break"
        if _timer <= 0:
            # Maybe socialize with a nearby villager
            import random
            villagers = [e for e in world["nearby"]
                         if e["type_id"] == "base:villager"
                         and e["id"] != self["id"] and e["distance"] < 10]
            if villagers and random.random() < social_chance:
                _social_target = min(villagers, key=lambda e: e["distance"])
                _state = "socializing"
                _timer = 4.0
                self["goal"] = "Walking to chat..."
                return MoveTo(_social_target["x"], _social_target["y"],
                              _social_target["z"], speed=self["walk_speed"] * 0.6)
            _state = "searching"
            _timer = 1.0
        return Idle()

    # ── State: socializing ──
    if _state == "socializing":
        if _timer <= 0:
            _state = "searching"
            _social_target = None
            self["goal"] = "Back to work!"
            return Idle()
        self["goal"] = "Chatting"
        return Idle()

    _state = "searching"
    return Idle()
