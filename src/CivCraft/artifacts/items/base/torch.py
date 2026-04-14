"""Torch — holdable light source. Can be placed on the ground."""

item = {
    "id": "torch",
    "name": "Torch",
    "category": "tool",
    "equip_slot": "offhand",
    "stack_max": 64,
    "cooldown": 0.2,

    "on_use": None,              # right-click: no self-use
    "on_equip": "equip",         # E key: hold in left hand
    "on_interact": None,         # left-click: no entity interaction

    "model": "torch",
    "color": [0.9, 0.7, 0.2],
}
