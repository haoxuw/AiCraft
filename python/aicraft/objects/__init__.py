"""Built-in object definitions.

Block types are ObjectMeta INSTANCES (data).
Block classes only exist when they add new member functions.
"""

from agentworld.api.base import (
    PassiveObject, ReactiveObject, ActiveObject,
    CropObject, SignalObject, LogicGateObject, FluidObject,
    LivingObject, PlayerObject, MobObject, ItemObject,
    ObjectMeta,
)

from agentworld.objects.terrain import ALL_TERRAIN
from agentworld.objects.plants import ALL_PLANTS
from agentworld.objects.crafted import ALL_CRAFTED
from agentworld.objects.active_blocks import (
    ALL_CROP_METAS, ALL_SIGNAL_METAS, TNT_META,
    ACTIVE_BLOCK_CLASSES,
)

# All block metas (passive + active), for registration
ALL_BLOCK_METAS: list[ObjectMeta] = (
    ALL_TERRAIN + ALL_PLANTS + ALL_CRAFTED +
    [TNT_META] + ALL_CROP_METAS + ALL_SIGNAL_METAS
)

# Map: meta.id -> ObjectMeta for all block types
BLOCK_REGISTRY: dict[str, ObjectMeta] = {m.id: m for m in ALL_BLOCK_METAS}

# Map: meta.id -> class for active blocks only (passive blocks use PassiveObject)
# This tells the server which Python class to instantiate
BLOCK_CLASS_REGISTRY: dict[str, type] = ACTIVE_BLOCK_CLASSES
