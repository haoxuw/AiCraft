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
    "effect_amount": 2,   # material value of apple = 2; hp value = 1 each → heals 2

    "model": "apple",
    "color": [0.85, 0.15, 0.12],
}
