"""Player input actions -- mine, place, attack."""

from modcraft.api.actions import Action, ActionMeta
from modcraft.api.world_view import WorldView
from modcraft.api.types import BlockPos, EntityId, Vec3


class Mine(Action):
    """Player mines (breaks) a block."""

    meta = ActionMeta(
        id="base:mine",
        display_name="Mine",
        category="player",
        range=5.0,
        sound="dig_{tool_group}",
        particles="block_break",
    )

    def __init__(self, actor: EntityId, target_pos: BlockPos, **kw):
        super().__init__(**kw)
        self.actor = actor
        self.target_pos = target_pos

    def validate(self, world: WorldView) -> bool:
        actor = world.get_entity(self.actor)
        if actor is None:
            return False
        block = world.get_block(self.target_pos)
        if block is None:
            return False
        if block.meta.hardness < 0:  # unbreakable
            return False
        if actor.pos.distance(self.target_pos.to_vec3()) > self.meta.range:
            return False
        return True

    def execute(self, world: WorldView) -> None:
        block = world.get_block(self.target_pos)

        # Determine what to drop
        drop_id = block.meta.drop or block.meta.id

        # Spawn dropped item
        drop_pos = self.target_pos.to_vec3() + Vec3(0, 0.5, 0)
        world.spawn_entity(drop_pos, "base:item_entity", item_type=drop_id)

        # Notify the block
        block.on_dig(world, self.target_pos,
                     world.get_entity(self.actor))

        # Remove the block
        world.set_block(self.target_pos, None)

        # Sound
        world.play_sound(block.meta.sound_dig or "dig_default",
                         self.target_pos.to_vec3())


class Place(Action):
    """Player places a block."""

    meta = ActionMeta(
        id="base:place",
        display_name="Place",
        category="player",
        range=5.0,
        sound="place_block",
    )

    def __init__(self, actor: EntityId, target_pos: BlockPos,
                 block_type: str, **kw):
        super().__init__(**kw)
        self.actor = actor
        self.target_pos = target_pos
        self.block_type = block_type

    def validate(self, world: WorldView) -> bool:
        actor = world.get_entity(self.actor)
        if actor is None:
            return False
        # Target must be air
        existing = world.get_block(self.target_pos)
        if existing is not None:
            return False
        if actor.pos.distance(self.target_pos.to_vec3()) > self.meta.range:
            return False
        return True

    def execute(self, world: WorldView) -> None:
        # Create block instance from type registry
        world.set_block(self.target_pos, self.block_type)

        world.play_sound("place_block", self.target_pos.to_vec3())


class Attack(Action):
    """Player attacks an entity."""

    meta = ActionMeta(
        id="base:attack",
        display_name="Attack",
        category="player",
        range=4.0,
        sound="attack_swing",
        animation="attack",
    )

    def __init__(self, actor: EntityId, target: EntityId,
                 damage: float = 1.0, **kw):
        super().__init__(**kw)
        self.actor = actor
        self.target = target
        self.damage = damage

    def validate(self, world: WorldView) -> bool:
        actor = world.get_entity(self.actor)
        target = world.get_entity(self.target)
        if actor is None or target is None:
            return False
        if actor.pos.distance(target.pos) > self.meta.range:
            return False
        return True

    def execute(self, world: WorldView) -> None:
        target = world.get_entity(self.target)
        if hasattr(target, 'on_hit'):
            actor = world.get_entity(self.actor)
            target.on_hit(world, actor, self.damage)
