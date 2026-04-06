"""Terrain block definitions -- data, not classes.

Each block type is an ObjectMeta instance. The CLASS is shared
(PassiveObject or ReactiveObject). Only the DATA differs.
"""

from modcraft.api.base import ObjectMeta

# --- Stone family (PassiveObject, pickaxe) ---

STONE = ObjectMeta(
    id="base:stone", display_name="Stone", category="terrain",
    hardness=4.0, tool_group="pickaxe", drop="base:cobblestone",
    color=(0.48, 0.48, 0.50),
    groups={"cracky": 3, "stone": 1},
    sound_dig="dig_stone", sound_footstep="step_stone",
)

COBBLESTONE = ObjectMeta(
    id="base:cobblestone", display_name="Cobblestone", category="terrain",
    hardness=3.5, tool_group="pickaxe",
    color=(0.45, 0.45, 0.47),
    groups={"cracky": 3, "stone": 2},
    sound_dig="dig_stone",
)

GRANITE = ObjectMeta(
    id="base:granite", display_name="Granite", category="terrain",
    hardness=4.5, tool_group="pickaxe",
    color=(0.55, 0.40, 0.38),
    groups={"cracky": 2, "stone": 1},
    sound_dig="dig_stone",
)

# --- Dirt family (ReactiveObject for grass, PassiveObject for plain dirt) ---

DIRT = ObjectMeta(
    id="base:dirt", display_name="Dirt", category="terrain",
    hardness=0.8, tool_group="shovel",
    color=(0.52, 0.34, 0.20),
    groups={"crumbly": 3, "soil": 1},
    sound_dig="dig_dirt", sound_footstep="step_dirt",
)

GRASS = ObjectMeta(
    id="base:grass", display_name="Grass Block", category="terrain",
    hardness=0.9, tool_group="shovel", drop="base:dirt",
    color_top=(0.30, 0.58, 0.18),
    color_side=(0.40, 0.38, 0.20),
    color_bottom=(0.52, 0.34, 0.20),
    color=(0.30, 0.58, 0.18),
    groups={"crumbly": 3, "soil": 1, "spreadable": 1},
    sound_dig="dig_dirt", sound_footstep="step_grass",
)

PODZOL = ObjectMeta(
    id="base:podzol", display_name="Podzol", category="terrain",
    hardness=0.8, tool_group="shovel",
    color_top=(0.40, 0.30, 0.18),
    color_side=(0.52, 0.34, 0.20),
    color_bottom=(0.52, 0.34, 0.20),
    color=(0.40, 0.30, 0.18),
    groups={"crumbly": 3, "soil": 1},
    sound_dig="dig_dirt",
)

# --- Sand family (PassiveObject, shovel, falling) ---

SAND = ObjectMeta(
    id="base:sand", display_name="Sand", category="terrain",
    hardness=0.6, tool_group="shovel",
    color=(0.82, 0.77, 0.50),
    groups={"crumbly": 3, "sand": 1, "falling": 1},
    sound_dig="dig_sand", sound_footstep="step_sand",
)

RED_SAND = ObjectMeta(
    id="base:red_sand", display_name="Red Sand", category="terrain",
    hardness=0.6, tool_group="shovel",
    color=(0.75, 0.45, 0.25),
    groups={"crumbly": 3, "sand": 1, "falling": 1},
    sound_dig="dig_sand",
)

GRAVEL = ObjectMeta(
    id="base:gravel", display_name="Gravel", category="terrain",
    hardness=0.9, tool_group="shovel",
    color=(0.55, 0.52, 0.50),
    groups={"crumbly": 2, "falling": 1},
    sound_dig="dig_gravel",
)

CLAY = ObjectMeta(
    id="base:clay", display_name="Clay", category="terrain",
    hardness=0.9, tool_group="shovel",
    color=(0.62, 0.62, 0.68),
    groups={"crumbly": 3},
    sound_dig="dig_dirt",
)

# --- Snow/ice family ---

SNOW = ObjectMeta(
    id="base:snow", display_name="Snow", category="terrain",
    hardness=0.3, tool_group="shovel",
    color=(0.93, 0.95, 0.97),
    groups={"crumbly": 3, "snowy": 1},
    sound_dig="dig_snow", sound_footstep="step_snow",
)

ICE = ObjectMeta(
    id="base:ice", display_name="Ice", category="terrain",
    hardness=1.0, tool_group="pickaxe", transparent=True,
    color=(0.70, 0.85, 0.95),
    groups={"cracky": 3, "slippery": 3},
    sound_dig="dig_glass",
)

# --- Special ---

BEDROCK = ObjectMeta(
    id="base:bedrock", display_name="Bedrock", category="terrain",
    hardness=-1.0,
    color=(0.15, 0.15, 0.15),
    groups={"unbreakable": 1},
)

OBSIDIAN = ObjectMeta(
    id="base:obsidian", display_name="Obsidian", category="terrain",
    hardness=50.0, tool_group="pickaxe",
    color=(0.10, 0.08, 0.15),
    groups={"cracky": 1, "stone": 1},
    sound_dig="dig_stone",
)

# --- Ore family (PassiveObject, pickaxe, drops raw material) ---

COAL_ORE = ObjectMeta(
    id="base:coal_ore", display_name="Coal Ore", category="terrain",
    hardness=3.0, tool_group="pickaxe", drop="base:coal",
    color=(0.30, 0.30, 0.30),
    groups={"cracky": 3, "stone": 1, "ore": 1},
    sound_dig="dig_stone",
)

IRON_ORE = ObjectMeta(
    id="base:iron_ore", display_name="Iron Ore", category="terrain",
    hardness=4.0, tool_group="pickaxe", drop="base:raw_iron",
    color=(0.55, 0.45, 0.40),
    groups={"cracky": 3, "stone": 1, "ore": 1},
    sound_dig="dig_stone",
)

# All terrain block metas in one list for easy registration
ALL_TERRAIN = [
    STONE, COBBLESTONE, GRANITE,
    DIRT, GRASS, PODZOL,
    SAND, RED_SAND, GRAVEL, CLAY,
    SNOW, ICE,
    BEDROCK, OBSIDIAN,
    COAL_ORE, IRON_ORE,
]
