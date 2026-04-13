"""Basic terrain blocks — the building blocks of the world.

Only the essentials. No granite, diorite, etc.
"""

blocks = [
    {
        "id": "base:dirt",
        "name": "Dirt",
        "solid": True,
        "hardness": 0.8,
        "color": [0.45, 0.32, 0.18],
        "can_grow_grass": True,
    },
    {
        "id": "base:grass",
        "name": "Grass",
        "solid": True,
        "hardness": 0.9,
        "color_top": [0.30, 0.58, 0.20],
        "color_side": [0.45, 0.32, 0.18],
        "reactive": True,              # spreads to dirt, dies when covered
    },
    {
        "id": "base:stone",
        "name": "Stone",
        "solid": True,
        "hardness": 4.0,
        "color": [0.48, 0.48, 0.50],
        "drop": "base:cobblestone",
    },
    {
        "id": "base:cobblestone",
        "name": "Cobblestone",
        "solid": True,
        "hardness": 4.0,
        "color": [0.42, 0.42, 0.44],
    },
    {
        "id": "base:sand",
        "name": "Sand",
        "solid": True,
        "hardness": 0.6,
        "color": [0.85, 0.78, 0.55],
        "falls": True,                 # affected by gravity
    },
    {
        "id": "base:snow",
        "name": "Snow",
        "solid": True,
        "hardness": 0.3,
        "color": [0.92, 0.95, 0.98],
    },
    {
        "id": "base:water",
        "name": "Water",
        "solid": False,
        "transparent": True,
        "hardness": -1,                # can't break water
        "color": [0.20, 0.40, 0.80],
    },
    {
        "id": "base:wood",
        "name": "Wood",
        "solid": True,
        "hardness": 2.0,
        "color_top": [0.55, 0.40, 0.22],
        "color_side": [0.40, 0.28, 0.12],
    },
    {
        "id": "base:logs",
        "name": "Log",
        "solid": True,
        "hardness": 2.0,
        "color_top": [0.40, 0.30, 0.18],    # end-grain darker
        "color_side": [0.55, 0.40, 0.22],   # same as wood side
    },
    {
        "id": "base:leaves",
        "name": "Leaves",
        "solid": True,
        "transparent": True,
        "hardness": 0.3,
        "color": [0.20, 0.50, 0.15],
    },
    {
        "id": "base:glass",
        "name": "Glass",
        "solid": True,
        "transparent": True,
        "hardness": 0.3,
        "color": [0.75, 0.85, 0.90],
        "drop": "",                    # doesn't drop when broken
    },
]
