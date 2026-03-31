"""Base classes for all game Objects.

Design principle: a new class exists ONLY when it adds new member functions.
Different stone/dirt/sand types are just different INSTANCES of PassiveObject
with different ObjectMeta values. You don't subclass for data-only differences.

Hierarchy:
    Object (abstract)
      ├── PassiveObject        -- any block/item with no behavior
      │     └── ReactiveObject -- reacts to neighbor changes (grass, sand)
      ├── ActiveObject         -- has decide() producing Actions each tick
      │     ├── LivingObject   -- has HP, on_hit(), on_death()
      │     │     ├── PlayerObject -- human-controlled
      │     │     └── MobObject    -- AI-controlled
      │     ├── CropObject     -- grows through stages
      │     ├── SignalObject   -- carries/processes redstone-like signals
      │     │     └── LogicGateObject -- multi-input logic (NAND, AND, etc.)
      │     └── FluidObject    -- flows and spreads
      └── ItemObject           -- exists in inventory, usable

User-created content:
    Players extend these base classes to create their own objects.
    A new class is needed ONLY when adding new member functions.
    Data-only variants (different color, hardness, etc.) are just new ObjectMeta.

    Examples of what players can build:
      - New passive block:  ObjectMeta(id="alice:marble", ...) using PassiveObject
      - New crop:           CropObject with custom ObjectMeta
      - New logic gate:     class MyGate(LogicGateObject): def compute(...)
      - New mob AI:         class Dragon(MobObject): def decide(...)
      - New signal block:   class Repeater(SignalObject): def decide(...)
      - New item:           class MagicWand(ItemObject): def on_use(...)
      - Entirely new class: class Teleporter(ActiveObject): def decide(...)

    User artifacts are uploaded to the server, validated, and registered
    at runtime via the ArtifactRegistry (see aicraft.api.registry).
"""

from __future__ import annotations
from typing import Any, TYPE_CHECKING
from dataclasses import dataclass, field

from aicraft.api.types import Vec3, BlockPos, EntityId
from aicraft.api.properties import Property

if TYPE_CHECKING:
    from aicraft.api.actions import Action
    from aicraft.api.world_view import WorldView


# ---------------------------------------------------------------------------
# ObjectMeta -- static definition shared by all instances of a type
# ---------------------------------------------------------------------------

@dataclass
class ObjectMeta:
    """Data-only definition of an object type. One per TYPE, not per class.

    100 types of stone -> 100 ObjectMeta instances, all using PassiveObject class.
    """

    # Identity
    id: str                          # "base:dirt_01", "base:granite"
    display_name: str = ""
    description: str = ""
    category: str = "misc"           # terrain, plant, animal, item, signal, etc.

    # Visual (name strings only -- renderer maps to assets)
    texture: str = ""
    texture_top: str = ""
    texture_bottom: str = ""
    texture_side: str = ""
    model: str = ""
    color: tuple[float, float, float] = (1.0, 1.0, 1.0)
    color_top: tuple[float, float, float] | None = None
    color_side: tuple[float, float, float] | None = None
    color_bottom: tuple[float, float, float] | None = None

    # Physics
    solid: bool = True
    transparent: bool = False
    collision_box: tuple[float, ...] = (-0.5, 0.0, -0.5, 0.5, 1.0, 0.5)

    # Block properties
    hardness: float = 1.0            # seconds to mine, -1 = unbreakable
    tool_group: str = ""             # pickaxe, shovel, axe
    drop: str = ""                   # item dropped (default: self)
    stack_max: int = 64
    light_emission: int = 0          # 0-15

    # Living properties
    max_hp: int = 0
    walk_speed: float = 0.0
    gravity_scale: float = 1.0

    # Groups (for game mechanics matching)
    groups: dict[str, int] = field(default_factory=dict)

    # Sounds
    sound_place: str = ""
    sound_dig: str = ""
    sound_footstep: str = ""


# ---------------------------------------------------------------------------
# Object -- abstract base
# ---------------------------------------------------------------------------

class Object:
    """Abstract base. Everything in the world is an Object."""

    meta: ObjectMeta

    def __init__(self, meta: ObjectMeta | None = None, **kwargs: Any):
        if meta is not None:
            self.meta = meta  # instance-level override
        self._dirty_fields: set[str] = set()
        self._entity_id: EntityId = 0
        self._pos: Vec3 = Vec3()
        for key, val in kwargs.items():
            setattr(self, key, val)

    @property
    def entity_id(self) -> EntityId:
        return self._entity_id

    @property
    def pos(self) -> Vec3:
        return self._pos

    @pos.setter
    def pos(self, value: Vec3):
        self._pos = value
        self._dirty_fields.add('_pos')

    def get_dirty_fields(self) -> dict[str, Any]:
        return {n: getattr(self, n, None) for n in self._dirty_fields}

    def clear_dirty(self):
        self._dirty_fields.clear()

    def serialize(self) -> dict[str, Any]:
        data = {"type": self.meta.id}
        for cls in type(self).__mro__:
            for key, val in vars(cls).items():
                if isinstance(val, Property):
                    data[key] = getattr(self, key)
        data["pos"] = self._pos.to_tuple()
        return data


# ---------------------------------------------------------------------------
# PassiveObject -- blocks/items with NO behavior
# ---------------------------------------------------------------------------

class PassiveObject(Object):
    """No tick behavior. Only reacts when an Action targets it.

    This single class covers: stone, dirt, sand, wood, planks, glass,
    brick, ore, etc. Different types are different ObjectMeta instances,
    NOT different classes.

    Usage:
        stone = PassiveObject(meta=ObjectMeta(id="base:stone", ...))
        dirt  = PassiveObject(meta=ObjectMeta(id="base:dirt", ...))
    """

    def on_place(self, world: WorldView, pos: BlockPos, placer: Object | None = None):
        """Called when placed in the world."""
        pass

    def on_dig(self, world: WorldView, pos: BlockPos, digger: Object | None = None):
        """Called when mined/broken."""
        pass

    def on_interact(self, world: WorldView, pos: BlockPos, actor: Object | None = None):
        """Called on right-click."""
        pass


# ---------------------------------------------------------------------------
# ReactiveObject -- passive but reacts to neighbor changes
# ---------------------------------------------------------------------------

class ReactiveObject(PassiveObject):
    """Like PassiveObject but also reacts when adjacent blocks change.

    Covers: grass (dies when covered), sand (falls), snow (melts near heat).
    New class because it adds a new member function: on_neighbor_changed().
    """

    def on_neighbor_changed(self, world: WorldView, pos: BlockPos,
                            neighbor_pos: BlockPos):
        """Called when an adjacent block is placed/removed."""
        pass


# ---------------------------------------------------------------------------
# ActiveObject -- has decide(), ticks every frame
# ---------------------------------------------------------------------------

class ActiveObject(Object):
    """Has a decide() method called each tick. Proposes Actions.

    decide() is READ-ONLY. Returns a list of Actions.
    """

    def decide(self, world: WorldView) -> list[Action]:
        return []


# ---------------------------------------------------------------------------
# CropObject -- grows through stages (wheat, carrots, etc.)
# ---------------------------------------------------------------------------

class CropObject(ActiveObject):
    """Block that grows through stages over time.

    New class because it adds: growth_stage, grow(), is_mature().
    """

    growth_stage = Property(default=0, min_val=0, max_val=7)
    max_stage = Property(default=7, min_val=1)
    growth_timer = Property(default=0.0, tick_rate=1.0)
    min_light = Property(default=8, min_val=0, max_val=15)

    def is_mature(self) -> bool:
        return self.growth_stage >= self.max_stage

    def grow(self):
        """Advance one growth stage."""
        if not self.is_mature():
            self.growth_stage += 1

    def decide(self, world: WorldView) -> list[Action]:
        if self.is_mature():
            return []
        if self.growth_timer > 30:
            self.growth_timer = 0.0
            light = world.get_light_level(self.pos.to_block_pos())
            if light >= self.min_light:
                self.grow()
        return []


# ---------------------------------------------------------------------------
# SignalObject -- carries or processes signals (wire, gates, power)
# ---------------------------------------------------------------------------

class SignalObject(ActiveObject):
    """Block that carries or processes signals for circuits.

    New class because it adds: power, get_output(), propagate().
    """

    power = Property(default=0, min_val=0, max_val=15)

    def get_output(self) -> int:
        """Signal strength this block outputs."""
        return self.power

    def read_neighbor_power(self, world: WorldView) -> int:
        """Read max power from adjacent signal blocks."""
        max_power = 0
        bp = self.pos.to_block_pos()
        for dx, dy, dz in [(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1)]:
            neighbor = world.get_block(bp.offset(dx, dy, dz))
            if neighbor and hasattr(neighbor, 'get_output'):
                max_power = max(max_power, neighbor.get_output())
        return max_power


class LogicGateObject(SignalObject):
    """Signal block with multiple inputs and logic.

    New class because it adds: input reading from specific faces,
    logic computation (AND, OR, NAND, NOR, XOR, etc.).
    """

    input_a = Property(default=0, min_val=0, max_val=15)
    input_b = Property(default=0, min_val=0, max_val=15)
    output = Property(default=0, min_val=0, max_val=15)

    def get_output(self) -> int:
        return self.output

    def compute(self, a: int, b: int) -> int:
        """Override this for different gate types. Default: NAND."""
        a_on = a > 0
        b_on = b > 0
        return 0 if (a_on and b_on) else 15

    def decide(self, world: WorldView) -> list[Action]:
        bp = self.pos.to_block_pos()
        block_a = world.get_block(bp.offset(-1, 0, 0))
        block_b = world.get_block(bp.offset(0, 0, -1))
        self.input_a = block_a.get_output() if hasattr(block_a, 'get_output') else 0
        self.input_b = block_b.get_output() if hasattr(block_b, 'get_output') else 0
        self.output = self.compute(self.input_a, self.input_b)
        return []


# ---------------------------------------------------------------------------
# FluidObject -- flows and spreads
# ---------------------------------------------------------------------------

class FluidObject(ActiveObject):
    """Liquid that flows. New class because it adds: level, flow logic.

    Covers: water, lava, any custom fluid.
    """

    level = Property(default=8, min_val=0, max_val=8)
    is_source = Property(default=True)

    def decide(self, world: WorldView) -> list[Action]:
        # Flow logic: check below, then spread horizontally
        return []


# ---------------------------------------------------------------------------
# LivingObject -- has HP, can die
# ---------------------------------------------------------------------------

class LivingObject(ActiveObject):
    """Has health, can take damage and die.

    New class because it adds: hp, on_hit(), on_death().
    """

    hp = Property(default=10, min_val=0)

    def on_hit(self, world: WorldView, attacker: Object, damage: float):
        self.hp -= damage
        if self.hp <= 0:
            self.on_death(world)

    def on_death(self, world: WorldView):
        pass


class PlayerObject(LivingObject):
    """Human-controlled. New class because decide() is driven by network input,
    and it has inventory/hunger mechanics."""

    hp = Property(default=20, min_val=0, max_val=20)
    hunger = Property(default=20.0, min_val=0.0, max_val=20.0, tick_rate=-0.005)
    selected_slot = Property(default=0, min_val=0, max_val=8)

    def decide(self, world: WorldView) -> list[Action]:
        return []  # actions come from network input


class MobObject(LivingObject):
    """AI-driven. Override decide() for behavior.

    This single class + different ObjectMeta covers:
    pig, cow, sheep, zombie, spider, etc.
    Only subclass if the mob needs NEW member functions.
    """
    pass


# ---------------------------------------------------------------------------
# ItemObject -- exists in inventory, can be used
# ---------------------------------------------------------------------------

class ItemObject(Object):
    """Something that exists in inventory, not in the world grid.

    New class because it adds: on_use(), stack behavior.
    Covers: tools, food, potions, materials.
    """

    durability = Property(default=-1, description="-1 = infinite")
    stack_count = Property(default=1, min_val=1)

    def on_use(self, world: WorldView, user: Object, target: BlockPos | None = None):
        """Called when player uses this item."""
        pass
