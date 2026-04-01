"""Torch — placeable light source."""

item = {
    "id": "base:torch",
    "name": "Torch",
    "category": "placeable",
    "stack_max": 64,
    "cooldown": 0.2,

    "on_use": "place",
    "places_block": "base:torch_block",     # becomes this block when placed

    "model": "torch",
    "color": [0.9, 0.7, 0.2],
}
