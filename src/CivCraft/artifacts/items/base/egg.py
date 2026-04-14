"""Egg — dropped by chickens. Can be eaten or thrown. Right-click to eat."""

item = {
    "id": "egg",
    "name": "Egg",
    "category": "consumable",
    "stack_max": 16,
    "cooldown": 0.5,

    "on_use": "consume",
    "on_equip": None,
    "on_interact": "give",    # left-click on entity: give the egg
    "effect": "heal",
    "effect_amount": 3,

    "model": "egg",
    "color": [0.95, 0.93, 0.88],
}
