"""Active block CLASSES -- these exist because they add new member functions.

Unlike terrain/plants/crafted (data-only ObjectMeta instances using shared classes),
active blocks need actual classes because they have unique decide() logic.

But WITHIN each class, different variants are still just different ObjectMeta data.
E.g., wheat and carrots are both CropObject with different ObjectMeta.
     NAND, AND, OR gates are all LogicGateObject with different compute().
"""

from aicraft.api.base import (
    ActiveObject, CropObject, SignalObject, LogicGateObject,
    ObjectMeta,
)
from aicraft.api.properties import Property
from aicraft.api.types import BlockPos


# === TNT ===
# Needs its own class: unique fuse countdown + explosion chain behavior.

class TNTBlock(ActiveObject):
    """Fuse countdown -> explosion. Only block with this exact behavior."""

    lit = Property(default=False)
    fuse_ticks = Property(default=80, min_val=0)

    def decide(self, world):
        if not self.lit:
            return []
        self.fuse_ticks -= 1
        if self.fuse_ticks <= 0:
            from aicraft.actions.world import TNTExplode
            return [TNTExplode(center=self.pos.to_block_pos(), radius=3)]
        return []


# TNT meta (data). Class is TNTBlock.
TNT_META = ObjectMeta(
    id="base:tnt", display_name="TNT", category="active",
    hardness=0.0,
    color=(0.80, 0.25, 0.20),
    groups={"tnt": 1, "flammable": 5},
)


# === Crops ===
# CropObject class handles all crops. Different crops = different ObjectMeta.

WHEAT_META = ObjectMeta(
    id="base:wheat", display_name="Wheat", category="crop",
    hardness=0.0, drop="base:wheat_seeds",
    color=(0.70, 0.65, 0.20),
    groups={"crop": 1},
)

CARROT_META = ObjectMeta(
    id="base:carrot", display_name="Carrot", category="crop",
    hardness=0.0, drop="base:carrot",
    color=(0.85, 0.50, 0.15),
    groups={"crop": 1},
)

POTATO_META = ObjectMeta(
    id="base:potato", display_name="Potato", category="crop",
    hardness=0.0, drop="base:potato",
    color=(0.65, 0.55, 0.30),
    groups={"crop": 1},
)

ALL_CROP_METAS = [WHEAT_META, CARROT_META, POTATO_META]


# === Signal / Circuit blocks ===
# SignalObject class handles wires. LogicGateObject handles gates.
# Different gate types override compute().

WIRE_META = ObjectMeta(
    id="base:wire", display_name="Wire", category="signal",
    hardness=0.0, solid=False,
    color=(0.60, 0.10, 0.10),
    groups={"signal": 1},
)

POWER_SOURCE_META = ObjectMeta(
    id="base:power_source", display_name="Power Source", category="signal",
    hardness=1.0, light_emission=7,
    color=(0.90, 0.85, 0.10),
    groups={"signal": 1},
)


# Wire: uses SignalObject class directly, with custom decide()
class WireBlock(SignalObject):
    """Propagates signal, reducing by 1 per block."""

    def decide(self, world):
        new_power = max(0, self.read_neighbor_power(world) - 1)
        if new_power != self.power:
            self.power = new_power
        return []


# Power source: constant output, no logic needed
class PowerSourceBlock(SignalObject):
    """Always outputs max signal."""
    power = Property(default=15, min_val=0, max_val=15)

    def decide(self, world):
        return []


# Gate variants: same class, different compute()

class NANDGate(LogicGateObject):
    """output = NOT(a AND b). Can build any digital circuit."""
    def compute(self, a, b): return 0 if (a > 0 and b > 0) else 15

class ANDGate(LogicGateObject):
    """output = a AND b"""
    def compute(self, a, b): return 15 if (a > 0 and b > 0) else 0

class ORGate(LogicGateObject):
    """output = a OR b"""
    def compute(self, a, b): return 15 if (a > 0 or b > 0) else 0

class NORGate(LogicGateObject):
    """output = NOT(a OR b)"""
    def compute(self, a, b): return 0 if (a > 0 or b > 0) else 15

class XORGate(LogicGateObject):
    """output = a XOR b"""
    def compute(self, a, b): return 15 if ((a > 0) != (b > 0)) else 0

class NOTGate(LogicGateObject):
    """output = NOT(a). Ignores input_b."""
    def compute(self, a, b): return 0 if a > 0 else 15


NAND_META = ObjectMeta(id="base:nand_gate", display_name="NAND Gate", category="signal",
                        hardness=1.0, color=(0.30, 0.30, 0.35), groups={"signal": 1, "logic": 1})
AND_META = ObjectMeta(id="base:and_gate", display_name="AND Gate", category="signal",
                       hardness=1.0, color=(0.25, 0.35, 0.30), groups={"signal": 1, "logic": 1})
OR_META = ObjectMeta(id="base:or_gate", display_name="OR Gate", category="signal",
                      hardness=1.0, color=(0.30, 0.25, 0.35), groups={"signal": 1, "logic": 1})
NOT_META = ObjectMeta(id="base:not_gate", display_name="NOT Gate", category="signal",
                       hardness=1.0, color=(0.35, 0.30, 0.25), groups={"signal": 1, "logic": 1})
XOR_META = ObjectMeta(id="base:xor_gate", display_name="XOR Gate", category="signal",
                       hardness=1.0, color=(0.28, 0.28, 0.38), groups={"signal": 1, "logic": 1})

ALL_SIGNAL_METAS = [WIRE_META, POWER_SOURCE_META, NAND_META, AND_META, OR_META, NOT_META, XOR_META]

# Map: meta.id -> class (for the server to instantiate the right class)
ACTIVE_BLOCK_CLASSES = {
    "base:tnt": TNTBlock,
    "base:wheat": CropObject,
    "base:carrot": CropObject,
    "base:potato": CropObject,
    "base:wire": WireBlock,
    "base:power_source": PowerSourceBlock,
    "base:nand_gate": NANDGate,
    "base:and_gate": ANDGate,
    "base:or_gate": ORGate,
    "base:not_gate": NOTGate,
    "base:xor_gate": XORGate,
}
