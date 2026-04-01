"""Sword — basic melee weapon."""

item = {
    "id": "base:sword",
    "name": "Sword",
    "category": "weapon",
    "equip_slot": "left_hand",
    "stack_max": 1,
    "cooldown": 0.5,

    "on_use": "attack",
    "damage": 5,
    "range": 3.0,

    "model": "sword",
    "color": [0.7, 0.7, 0.75],
}
