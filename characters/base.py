"""
Base classes for AiCraft character definitions.

A character is a collection of body parts (colored boxes) with
animation parameters. Each part can swing around a pivot point
for walk/idle animations.
"""

from dataclasses import dataclass, field
from typing import Tuple


@dataclass
class Part:
    """A single body part (rendered as a colored box)."""
    name: str
    offset: Tuple[float, float, float]      # center position relative to feet
    size: Tuple[float, float, float]         # full width, height, depth
    color: Tuple[float, float, float, float] # RGBA

    # Animation: swings around pivot
    pivot: Tuple[float, float, float] = (0, 0, 0)
    swing_axis: Tuple[float, float, float] = (1, 0, 0)
    swing_amplitude: float = 0.0   # degrees
    swing_phase: float = 0.0       # radians (PI = opposite phase)
    swing_speed: float = 1.0       # multiplier


@dataclass
class Stats:
    """RPG stats, each 1-5 stars."""
    strength: int = 3
    stamina: int = 3
    agility: int = 3
    intelligence: int = 3


@dataclass
class Character:
    """Complete character definition."""
    id: str
    name: str
    description: str = ""
    height: float = 2.0
    walk_cycle_speed: float = 3.0
    walk_bob_amount: float = 0.05
    idle_bob_amount: float = 0.012
    jump_velocity: float = 17.0       # upward velocity on jump
    stats: Stats = field(default_factory=Stats)
    parts: list = field(default_factory=list)
