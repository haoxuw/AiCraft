"""Potion — consumable with customizable effect.

The default potion heals 10 HP. Players can modify the effect,
amount, and duration to create speed potions, strength potions, etc.
"""

item = {
    "id": "base:potion",
    "name": "Potion",
    "category": "potion",
    "stack_max": 16,
    "consumable": True,

    "effect": "heal",
    "effect_amount": 10,
    "effect_duration": 0,       # 0 = instant

    "model": "potion",
    "color": [0.8, 0.2, 0.3],
}
