"""Sword — melee weapon. Attack entities on left-click."""

item = {
    "id": "base:sword",
    "name": "Sword",
    "category": "weapon",
    "equip_slot": "left_hand",
    "stack_max": 1,
    "cooldown": 0.5,
    "damage": 5,
    "range": 3.0,

    # Item action hooks (Python-defined)
    "on_use": None,           # right-click: no self-use
    "on_equip": "equip",      # E key: equip to left hand
    "on_interact": "attack",  # left-click on entity: attack

    "model": "sword",
    "color": [0.7, 0.7, 0.75],
}
