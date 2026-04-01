"""Tool visuals -- pickaxes, axes, shovels.

Client-side visual only. Attached to right_hand slot.
"""

from aicraft.items.base import ItemVisual, ItemPiece

WOOD_HANDLE = (0.50, 0.35, 0.15, 1)
STONE_HEAD  = (0.50, 0.50, 0.52, 1)
IRON_HEAD   = (0.55, 0.55, 0.58, 1)

wood_pickaxe = ItemVisual(
    id="base:wood_pickaxe",
    name="Wood Pickaxe",
    slot="right_hand",
    pieces=[
        ItemPiece("handle", WOOD_HANDLE, (0, 0, 0),     (0.02, 0.28, 0.02)),
        ItemPiece("head",   IRON_HEAD,   (0, 0.22, 0),  (0.10, 0.03, 0.02)),
    ],
)

stone_pickaxe = ItemVisual(
    id="base:stone_pickaxe",
    name="Stone Pickaxe",
    slot="right_hand",
    pieces=[
        ItemPiece("handle", WOOD_HANDLE, (0, 0, 0),     (0.02, 0.28, 0.02)),
        ItemPiece("head",   STONE_HEAD,  (0, 0.22, 0),  (0.12, 0.04, 0.03)),
    ],
)

wood_axe = ItemVisual(
    id="base:wood_axe",
    name="Wood Axe",
    slot="right_hand",
    pieces=[
        ItemPiece("handle", WOOD_HANDLE, (0, 0, 0),       (0.02, 0.28, 0.02)),
        ItemPiece("blade",  IRON_HEAD,   (0.04, 0.20, 0), (0.06, 0.08, 0.02)),
    ],
)

wood_shovel = ItemVisual(
    id="base:wood_shovel",
    name="Wood Shovel",
    slot="right_hand",
    pieces=[
        ItemPiece("handle", WOOD_HANDLE,             (0, 0, 0),     (0.02, 0.30, 0.02)),
        ItemPiece("blade",  (0.55, 0.45, 0.30, 1),  (0, 0.26, 0),  (0.05, 0.06, 0.02)),
    ],
)
