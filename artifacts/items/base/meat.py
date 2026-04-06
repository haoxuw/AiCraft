"""Meat — raw dropped by any animal on death. Can be eaten."""

item = {
    "id": "base:meat",
    "name": "Meat",
    "category": "consumable",
    "stack_max": 64,
    "cooldown": 0.8,

    "on_use": "consume",
    "on_equip": None,
    "on_interact": None,
    "effect": "heal",
    "effect_amount": 4,

    "color": [0.72, 0.22, 0.14],
}
