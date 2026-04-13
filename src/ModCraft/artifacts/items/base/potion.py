"""Potion — consumable healing drink. Right-click to drink."""

item = {
    "id": "base:potion",
    "name": "Potion",
    "category": "consumable",
    "stack_max": 16,
    "cooldown": 1.0,

    "on_use": "consume",       # right-click: drink and heal
    "on_equip": None,          # not equippable
    "on_interact": None,       # no entity interaction
    "effect": "heal",
    "effect_amount": 5,   # material value of potion = 5; hp value = 1 each → heals 5

    "model": "potion",
    "color": [0.8, 0.2, 0.3],
}
