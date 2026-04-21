"""Basic terrain blocks — the building blocks of the world.

Invariant: every breakable block drops itself. Break a block → get an item
with the same id → re-place it as the same block. The `drop` field is therefore
always omitted (default = self). Items that aren't blocks (sword, potion, etc.)
live in artifacts/items/ and are non-placeable because their id is not a
registered block type.
"""

blocks = [
    {
        "id": "dirt",
        "name": "Dirt",
        "solid": True,
        "hardness": 0.8,
        "color": [0.45, 0.32, 0.18],
        "can_grow_grass": True,
    },
    {
        "id": "grass",
        "name": "Grass",
        "solid": True,
        "hardness": 0.9,
        "color_top": [0.30, 0.58, 0.20],
        "color_side": [0.45, 0.32, 0.18],
        "reactive": True,              # spreads to dirt, dies when covered
    },
    {
        "id": "stone",
        "name": "Stone",
        "solid": True,
        "hardness": 4.0,
        "color": [0.48, 0.48, 0.50],
    },
    {
        "id": "cobblestone",
        "name": "Cobblestone",
        "solid": True,
        "hardness": 4.0,
        "color": [0.42, 0.42, 0.44],
    },
    {
        "id": "granite",
        "name": "Granite",
        "solid": True,
        "hardness": 4.0,
        "color": [0.58, 0.42, 0.38],
    },
    {
        "id": "marble",
        "name": "Marble",
        "solid": True,
        "hardness": 4.0,
        "color": [0.88, 0.88, 0.85],
    },
    {
        "id": "sandstone",
        "name": "Sandstone",
        "solid": True,
        "hardness": 3.0,
        "color": [0.80, 0.72, 0.48],
    },
    {
        "id": "sand",
        "name": "Sand",
        "solid": True,
        "hardness": 0.6,
        "color": [0.85, 0.78, 0.55],
        "falls": True,                 # affected by gravity
    },
    {
        "id": "snow",
        "name": "Snow",
        "solid": True,
        "hardness": 0.3,
        "color": [0.92, 0.95, 0.98],
    },
    {
        "id": "water",
        "name": "Water",
        "solid": False,
        "transparent": True,
        "hardness": -1,                # can't break water
        "color": [0.20, 0.40, 0.80],
    },
    {
        "id": "wood",
        "name": "Wood",
        "solid": True,
        "hardness": 2.0,
        "color_top": [0.55, 0.40, 0.22],
        "color_side": [0.40, 0.28, 0.12],
    },
    {
        "id": "logs",
        "name": "Log",
        "solid": True,
        "hardness": 2.0,
        "color_top": [0.40, 0.30, 0.18],    # end-grain darker
        "color_side": [0.55, 0.40, 0.22],   # same as wood side
    },
    {
        "id": "leaves",
        "name": "Leaves",
        "solid": True,
        "transparent": True,
        "hardness": 0.3,
        "color": [0.20, 0.50, 0.15],
    },
    {
        "id": "glass",
        "name": "Glass",
        "solid": True,
        "transparent": True,
        "hardness": 0.3,
        "color": [0.75, 0.85, 0.90],
    },
    {
        "id": "beenest",
        "name": "Bee Nest",
        "solid": True,
        "hardness": 0.6,
        # Warm honeycomb amber — bees anchor their wander radius around this.
        "color": [0.85, 0.60, 0.15],
    },
]
