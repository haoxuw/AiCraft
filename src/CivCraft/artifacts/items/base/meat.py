"""Meat — raw dropped by any animal on death. Can be eaten."""

item = {
    "id": "meat",
    "name": "Meat",
    "category": "consumable",
    "stack_max": 64,
    "cooldown": 0.8,

    "on_use": "consume",
    "on_equip": None,
    "on_interact": None,
    "effect": "heal",
    "effect_amount": 3,   # material value of meat = 3; hp value = 1 each → heals 3

    "color": [0.72, 0.22, 0.14],
}
