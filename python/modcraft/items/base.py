"""
Base classes for item visual definitions (CLIENT-SIDE only).

These define how items LOOK when equipped -- geometry, colors, and
particle effects. They do NOT define game mechanics (that's server-side
in modcraft.api and modcraft.actions).

Architecture separation:
  - Server knows: "player has jetpack" → enables mid-air thrust
  - Client knows: "jetpack looks like twin tanks with flame effects"
  - They communicate through entity properties: server sets
    "jetpack_active=1", client reads it and emits flames.

Players can create custom item visuals by subclassing ItemVisual
and defining pieces + effects. The client loads these at startup
or hot-reloads them during play.
"""

from dataclasses import dataclass, field
from typing import Tuple, List, Optional


@dataclass
class ItemPiece:
    """One box of an equipped item's visual model."""
    name: str
    color: Tuple[float, float, float, float]  # RGBA
    offset: Tuple[float, float, float] = (0, 0, 0)  # from slot center
    size: Tuple[float, float, float] = (0.05, 0.05, 0.05)  # half extents


@dataclass
class ParticleEmitter:
    """A particle stream emitted by an active item.

    The emitter fires when the entity has the trigger property set to
    a truthy value. Multiple emitters per item are supported (e.g.
    jetpack has 2 nozzles = 2 emitters).
    """
    offset: Tuple[float, float, float]      # position relative to slot center
    rate: int = 4                            # particles per frame
    velocity: Tuple[float, float, float] = (0, -5, 0)  # base velocity
    velocity_spread: float = 0.5             # random spread added to velocity
    colors: List[Tuple[float, float, float, float]] = field(default_factory=lambda: [
        (1.0, 0.95, 0.8, 1.0),   # core: white-hot
        (1.0, 0.75, 0.15, 1.0),  # inner: yellow
        (1.0, 0.35, 0.05, 0.9),  # outer: orange-red
    ])
    life_range: Tuple[float, float] = (0.08, 0.25)  # min/max lifetime
    size_range: Tuple[float, float] = (0.03, 0.06)  # min/max particle size


@dataclass
class ActiveEffect:
    """Particle effects triggered by an entity property.

    When the entity's trigger prop is truthy, all emitters fire.
    This is how the client knows to show jetpack flames, torch fire,
    weapon trails, etc. -- without knowing any game logic.
    """
    trigger: str                              # entity property name, e.g. "jetpack_active"
    emitters: List[ParticleEmitter] = field(default_factory=list)


@dataclass
class ItemVisual:
    """Complete client-side visual definition for an equipped item.

    Defines:
      - Which slot it occupies (back, head, right_hand, etc.)
      - What it looks like (list of colored box pieces)
      - What effects it produces when active (particle emitters)

    This is the Python source of truth for item appearance.
    C++ builtin mirrors these definitions until pybind11 integration.
    """
    id: str                                   # "base:jetpack"
    name: str = ""
    slot: str = "back"                        # equipment slot name
    pieces: List[ItemPiece] = field(default_factory=list)
    effects: List[ActiveEffect] = field(default_factory=list)
