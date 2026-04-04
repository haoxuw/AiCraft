"""Villager — finds nearby trees and chops them down.

Villagers search for wood blocks within 16 blocks, walk to them,
stand and chop for 2 seconds, then rest before searching again.

State machine: searching → walking → chopping → resting → searching
"""

from agentica_engine import Idle, Wander, MoveTo, BreakBlock

SEARCH_RADIUS = 16.0

_state = "searching"
_timer = 0
_tree = None

goal = "Searching for trees"


def decide(self, world):
    """Called 4 times per second. Return what to do next."""
    global _state, _timer, _tree

    _timer -= world["dt"]

    # State: searching — look for wood blocks
    if _state == "searching":
        wood_blocks = [b for b in world["blocks"] if b["type"] == "base:wood"]
        if wood_blocks:
            _tree = min(wood_blocks, key=lambda b: b["distance"])
            _state = "walking"
            _timer = 8.0
            self["goal"] = "Found a tree!"
            return MoveTo(
                _tree["x"] + 0.5, _tree["y"], _tree["z"] + 0.5,
                speed=self["walk_speed"]
            )
        self["goal"] = "Searching for trees"
        if _timer <= 0:
            _timer = 2.0
        return Wander(speed=self["walk_speed"] * 0.7)

    # State: walking — walk toward tree
    if _state == "walking":
        dx = self["x"] - _tree["x"]
        dz = self["z"] - _tree["z"]
        dist = (dx * dx + dz * dz) ** 0.5
        if dist < 2.5:
            _state = "chopping"
            _timer = 2.0
            self["goal"] = "Chopping tree!"
            return Idle()
        if _timer <= 0:
            _state = "searching"
            _timer = 1.0
            self["goal"] = "Can't reach tree"
            return Idle()
        self["goal"] = f"Walking to tree ({dist:.0f}m)"
        return MoveTo(
            _tree["x"] + 0.5, _tree["y"], _tree["z"] + 0.5,
            speed=self["walk_speed"]
        )

    # State: chopping — break the tree block!
    if _state == "chopping":
        self["goal"] = "Chopping!"
        if _timer <= 0:
            _state = "resting"
            _timer = 3.0
            # Actually break the wood block
            if _tree:
                return BreakBlock(_tree["x"], _tree["y"], _tree["z"])
        return Idle()

    # State: resting — take a break
    if _state == "resting":
        self["goal"] = "Taking a break"
        if _timer <= 0:
            _state = "searching"
            _timer = 1.0
        return Idle()

    # Fallback
    _state = "searching"
    return Idle()
