"""Movement actions -- MoveTo, Follow, Flee.

These are intent-based: the action sets a desired target,
and the engine's physics system determines the actual path.
Objects NEVER set their own position directly.
"""

from aicraft.api.actions import Action, ActionMeta
from aicraft.api.world_view import WorldView
from aicraft.api.types import BlockPos, EntityId, Vec3


class MoveTo(Action):
    """Move an entity toward a target position.

    The entity walks toward the target using the unified physics
    engine. Collision, step-up, and gravity are handled by the
    engine -- the action only sets the INTENT.

    Used by:
      - God-mode click-to-move (player clicks terrain)
      - AI pathfinding (mob decide() returns MoveTo)
      - Scripted cutscenes / quests
    """

    meta = ActionMeta(
        id="base:move_to",
        display_name="Move To",
        category="movement",
        range=80.0,
    )

    def __init__(self, actor: EntityId, target_pos: Vec3, **kw):
        super().__init__(**kw)
        self.actor = actor
        self.target_pos = target_pos

    def validate(self, world: WorldView) -> bool:
        actor = world.get_entity(self.actor)
        if actor is None:
            return False

        # Target must have solid ground below it
        ground_pos = BlockPos(
            int(self.target_pos.x),
            int(self.target_pos.y) - 1,
            int(self.target_pos.z),
        )
        ground = world.get_block(ground_pos)
        if ground is None:
            return False  # can't walk on air

        # Target must not be inside solid blocks
        body_pos = BlockPos(
            int(self.target_pos.x),
            int(self.target_pos.y),
            int(self.target_pos.z),
        )
        body_block = world.get_block(body_pos)
        if body_block is not None and body_block.meta.solid:
            return False  # target is inside a wall

        return True

    def execute(self, world: WorldView) -> None:
        """Set the entity's move target. The engine handles the rest."""
        actor = world.get_entity(self.actor)
        if actor is not None:
            actor.set_move_target(self.target_pos)


class Follow(Action):
    """Follow another entity (keep within range)."""

    meta = ActionMeta(
        id="base:follow",
        display_name="Follow",
        category="movement",
    )

    def __init__(self, actor: EntityId, target: EntityId,
                 follow_distance: float = 3.0, **kw):
        super().__init__(**kw)
        self.actor = actor
        self.target = target
        self.follow_distance = follow_distance

    def validate(self, world: WorldView) -> bool:
        return (world.get_entity(self.actor) is not None and
                world.get_entity(self.target) is not None)

    def execute(self, world: WorldView) -> None:
        target = world.get_entity(self.target)
        if target:
            actor = world.get_entity(self.actor)
            actor.set_move_target(target.pos)
