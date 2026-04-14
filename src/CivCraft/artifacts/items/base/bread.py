"""Bread — baked loaf that restores health. Right-click to eat."""

item = {
    "id": "bread",
    "name": "Bread",
    "category": "consumable",
    "stack_max": 16,
    "cooldown": 1.0,

    "on_use": "consume",
    "on_equip": None,
    "on_interact": None,
    "effect": "heal",
    "effect_amount": 8,

    "model": "bread",
    "color": [0.78, 0.58, 0.28],
}
