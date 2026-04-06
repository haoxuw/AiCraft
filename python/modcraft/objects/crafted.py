"""Crafted block definitions -- data, not classes."""

from modcraft.api.base import ObjectMeta

GLASS = ObjectMeta(
    id="base:glass", display_name="Glass", category="crafted",
    hardness=0.5, transparent=True,
    color=(0.85, 0.90, 0.95),
    groups={"cracky": 3, "oddly_breakable": 3},
    sound_dig="dig_glass",
)

BRICK = ObjectMeta(
    id="base:brick", display_name="Brick", category="crafted",
    hardness=3.0, tool_group="pickaxe",
    color=(0.65, 0.30, 0.25),
    groups={"cracky": 3},
    sound_dig="dig_stone",
)

TORCH = ObjectMeta(
    id="base:torch", display_name="Torch", category="crafted",
    hardness=0.0, solid=False, light_emission=14,
    color=(0.90, 0.80, 0.30),
    groups={"dig_immediate": 3, "attached": 1},
)

BOOKSHELF = ObjectMeta(
    id="base:bookshelf", display_name="Bookshelf", category="crafted",
    hardness=1.5, tool_group="axe",
    color=(0.55, 0.42, 0.22),
    groups={"choppy": 3, "flammable": 3},
    sound_dig="dig_wood",
)

ALL_CRAFTED = [GLASS, BRICK, TORCH, BOOKSHELF]
