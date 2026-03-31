"""WorldView -- sandboxed interface to the World.

Objects and Actions receive a WorldView scoped to a region.
They can read and write within that region, but not beyond.

In production, WorldView is implemented in C++ and exposed via pybind11.
This Python class defines the interface and serves as a reference impl.
"""

from __future__ import annotations
from typing import Any, TYPE_CHECKING

from aicraft.api.types import Vec3, BlockPos, EntityId

if TYPE_CHECKING:
    from aicraft.api.base import Object, ActiveObject
    from aicraft.api.actions import Action


class WorldView:
    """Sandboxed, region-scoped view of the world.

    Provided to Action.execute() and ActiveObject.decide().
    All reads are live. All writes are queued as mutations
    and applied atomically after the action completes.
    """

    # --- Block operations ---

    def get_block(self, pos: BlockPos) -> Object | None:
        """Get the block object at a position, or None for air."""
        raise NotImplementedError("Implemented by C++ engine")

    def set_block(self, pos: BlockPos, block: Object | None) -> None:
        """Set a block. Pass None to remove (set to air)."""
        raise NotImplementedError("Implemented by C++ engine")

    def get_blocks_in_radius(self, center: BlockPos, radius: int
                             ) -> list[tuple[BlockPos, Object]]:
        """Get all non-air blocks within radius."""
        raise NotImplementedError("Implemented by C++ engine")

    # --- Entity operations ---

    def get_entity(self, entity_id: EntityId) -> ActiveObject | None:
        """Get an entity by ID."""
        raise NotImplementedError("Implemented by C++ engine")

    def get_entities_in_radius(self, center: Vec3, radius: float
                               ) -> list[ActiveObject]:
        """Get all entities within radius of a point."""
        raise NotImplementedError("Implemented by C++ engine")

    def spawn_entity(self, pos: Vec3, type_id: str, **attrs: Any) -> EntityId:
        """Spawn a new entity. Returns its ID."""
        raise NotImplementedError("Implemented by C++ engine")

    def remove_entity(self, entity_id: EntityId) -> None:
        """Remove an entity from the world."""
        raise NotImplementedError("Implemented by C++ engine")

    # --- Effects (queued, applied after action) ---

    def emit_action(self, action: Action) -> None:
        """Queue a chained action to run after this one."""
        raise NotImplementedError("Implemented by C++ engine")

    def play_sound(self, sound: str, pos: Vec3, gain: float = 1.0) -> None:
        """Queue a sound to play at a position."""
        raise NotImplementedError("Implemented by C++ engine")

    def emit_particles(self, particle_type: str, pos: Vec3, count: int = 10) -> None:
        """Queue a particle effect."""
        raise NotImplementedError("Implemented by C++ engine")

    # --- Queries ---

    def get_time_of_day(self) -> float:
        """0.0 = midnight, 0.5 = noon."""
        raise NotImplementedError("Implemented by C++ engine")

    def get_light_level(self, pos: BlockPos) -> int:
        """Light level 0-15 at a position."""
        raise NotImplementedError("Implemented by C++ engine")

    def raycast(self, origin: Vec3, direction: Vec3, max_dist: float = 5.0
                ) -> tuple[BlockPos, Vec3] | None:
        """Cast a ray. Returns (hit_block_pos, hit_normal) or None."""
        raise NotImplementedError("Implemented by C++ engine")
