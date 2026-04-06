"""Plant block definitions -- data, not classes."""

from modcraft.api.base import ObjectMeta

WOOD_OAK = ObjectMeta(
    id="base:wood_oak", display_name="Oak Log", category="plant",
    hardness=2.0, tool_group="axe",
    color_top=(0.50, 0.38, 0.18), color_side=(0.42, 0.28, 0.12),
    color_bottom=(0.50, 0.38, 0.18), color=(0.42, 0.28, 0.12),
    groups={"choppy": 2, "tree": 1, "flammable": 2},
    sound_dig="dig_wood", sound_footstep="step_wood",
)

WOOD_BIRCH = ObjectMeta(
    id="base:wood_birch", display_name="Birch Log", category="plant",
    hardness=2.0, tool_group="axe",
    color_top=(0.60, 0.55, 0.42), color_side=(0.80, 0.78, 0.72),
    color_bottom=(0.60, 0.55, 0.42), color=(0.80, 0.78, 0.72),
    groups={"choppy": 2, "tree": 1, "flammable": 2},
    sound_dig="dig_wood",
)

PLANKS_OAK = ObjectMeta(
    id="base:planks_oak", display_name="Oak Planks", category="crafted",
    hardness=1.5, tool_group="axe",
    color=(0.60, 0.45, 0.25),
    groups={"choppy": 2, "wood": 1, "flammable": 2},
    sound_dig="dig_wood",
)

LEAVES_OAK = ObjectMeta(
    id="base:leaves_oak", display_name="Oak Leaves", category="plant",
    hardness=0.3,
    color=(0.18, 0.48, 0.10),
    groups={"snappy": 3, "flammable": 2, "leaves": 1},
    sound_dig="dig_leaves",
)

LEAVES_BIRCH = ObjectMeta(
    id="base:leaves_birch", display_name="Birch Leaves", category="plant",
    hardness=0.3,
    color=(0.30, 0.55, 0.18),
    groups={"snappy": 3, "flammable": 2, "leaves": 1},
    sound_dig="dig_leaves",
)

ALL_PLANTS = [
    WOOD_OAK, WOOD_BIRCH,
    PLANKS_OAK,
    LEAVES_OAK, LEAVES_BIRCH,
]
