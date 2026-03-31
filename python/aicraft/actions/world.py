"""World system actions -- grass spread, leaf decay, etc.

These are triggered by the server's periodic systems (ABMs),
not by player input.
"""

import random
from aicraft.api.actions import Action, ActionMeta
from aicraft.api.world_view import WorldView
from aicraft.api.types import BlockPos


class GrassSpread(Action):
    """Dirt near grass becomes grass (if exposed to light)."""

    meta = ActionMeta(
        id="base:grass_spread",
        display_name="Grass Spread",
        category="world",
        # Triggered by ABM: interval=6s, chance=1/50
    )

    def __init__(self, target_pos: BlockPos, **kw):
        super().__init__(**kw)
        self.target_pos = target_pos

    def validate(self, world: WorldView) -> bool:
        block = world.get_block(self.target_pos)
        if block is None or block.meta.id != "base:dirt":
            return False
        # Must have a grass neighbor
        for dx, dy, dz in [(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)]:
            neighbor = world.get_block(self.target_pos.offset(dx, dy, dz))
            if neighbor and neighbor.meta.id == "base:grass":
                return True
        return False

    def execute(self, world: WorldView) -> None:
        # Check above is air (needs light)
        above = world.get_block(self.target_pos.offset(0, 1, 0))
        if above is not None and above.meta.solid:
            return
        world.set_block(self.target_pos, "base:grass")


class GrassDie(Action):
    """Grass covered by a solid block reverts to dirt."""

    meta = ActionMeta(
        id="base:grass_die",
        display_name="Grass Die",
        category="world",
    )

    def __init__(self, target_pos: BlockPos, **kw):
        super().__init__(**kw)
        self.target_pos = target_pos

    def validate(self, world: WorldView) -> bool:
        block = world.get_block(self.target_pos)
        if block is None or block.meta.id != "base:grass":
            return False
        above = world.get_block(self.target_pos.offset(0, 1, 0))
        return above is not None and above.meta.solid

    def execute(self, world: WorldView) -> None:
        world.set_block(self.target_pos, "base:dirt")


class TNTExplode(Action):
    """TNT detonates, destroying blocks in a radius."""

    meta = ActionMeta(
        id="base:tnt_explode",
        display_name="TNT Explosion",
        category="item",
        sound="tnt_explode",
        particles="explosion",
    )

    def __init__(self, center: BlockPos, radius: int = 3, **kw):
        super().__init__(**kw)
        self.center = center
        self.radius = radius

    def validate(self, world: WorldView) -> bool:
        return True

    def execute(self, world: WorldView) -> None:
        r = self.radius
        for x in range(-r, r + 1):
            for y in range(-r, r + 1):
                for z in range(-r, r + 1):
                    if x*x + y*y + z*z > r*r:
                        continue
                    pos = self.center.offset(x, y, z)
                    block = world.get_block(pos)
                    if block is None:
                        continue
                    if block.meta.groups.get("unbreakable"):
                        continue
                    # Random chance to drop
                    if random.random() < 0.3:
                        drop = block.meta.drop or block.meta.id
                        world.spawn_entity(pos.to_vec3(), "base:item_entity",
                                           item_type=drop)
                    world.set_block(pos, None)

        world.play_sound("tnt_explode", self.center.to_vec3())
        world.emit_particles("explosion", self.center.to_vec3(), count=100)
