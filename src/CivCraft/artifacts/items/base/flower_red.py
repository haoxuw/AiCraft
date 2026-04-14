"""Red flower — dropped when you break a grass block wearing one.

Placing this item back on top of grass restores the annotation.
"""

item = {
    "id": "flower_red",
    "name": "Red Flower",
    "category": "plant",
    "stack_max": 64,

    # Right-click on a block tries to place this as an annotation on top.
    "on_use": "place_annotation",
    "on_equip": None,
    "on_interact": None,

    "model": "flower_red",
    "color": [0.90, 0.18, 0.18],
}
