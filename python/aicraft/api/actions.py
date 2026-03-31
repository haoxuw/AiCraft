"""Action base class -- the ONLY way to mutate the World.

Actions are discrete events: mining a block, a sheep eating grass,
TNT exploding. They validate preconditions, execute mutations,
and can chain into further Actions.
"""

from __future__ import annotations
from typing import Any, TYPE_CHECKING
from dataclasses import dataclass, field
from enum import Enum

from aicraft.api.types import Vec3, BlockPos, EntityId

if TYPE_CHECKING:
    from aicraft.api.world_view import WorldView


class ActionResult(Enum):
    SUCCESS = "success"
    FAILED_VALIDATION = "failed_validation"
    FAILED_EXECUTION = "failed_execution"


@dataclass
class ActionMeta:
    """Static definition of an action type."""

    id: str                          # "namespace:name" e.g. "base:mine"
    display_name: str = ""
    description: str = ""
    category: str = "misc"           # player, entity, world, item

    # Constraints
    cooldown: float = 0.0            # seconds between uses
    range: float = 5.0               # max distance
    duration: float = 0.0            # 0 = instant

    # Feedback (name strings -- renderer resolves)
    animation: str = ""
    sound: str = ""
    particles: str = ""


class Action:
    """Base class for all Actions.

    Subclasses must define:
      - meta: ActionMeta
      - validate(world) -> bool
      - execute(world) -> None

    Actions are the ONLY way to change the World.
    """

    meta: ActionMeta  # subclasses must define this

    def __init__(self, **kwargs: Any):
        for key, val in kwargs.items():
            setattr(self, key, val)

    def validate(self, world: WorldView) -> bool:
        """Check if this action can happen right now.
        Must be read-only -- no mutations."""
        return True

    def execute(self, world: WorldView) -> None:
        """Apply the action to the world.
        Use world.set_block(), world.spawn_entity(), etc.
        May call world.emit_action() to chain further actions."""
        pass

    def __repr__(self) -> str:
        attrs = {k: v for k, v in self.__dict__.items() if not k.startswith('_')}
        return f"{type(self).__name__}({attrs})"
