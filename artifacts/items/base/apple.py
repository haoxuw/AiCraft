"""Apple — juicy fruit that restores health. Right-click to eat."""

item = {
    "id": "base:apple",
    "name": "Apple",
    "category": "consumable",
    "stack_max": 16,
    "cooldown": 1.0,

    "on_use": "consume",
    "on_equip": None,
    "on_interact": None,
    "effect": "heal",
    "effect_amount": 5,

    "model": "apple",
    "color": [0.85, 0.15, 0.12],
}
