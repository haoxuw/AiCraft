"""Sword — melee weapon. Hold in hotbar, attack with left-click."""

item = {
    "id": "sword",
    "name": "Sword",
    "category": "weapon",
    # No equip_slot — held via hotbar selection (right hand)
    "stack_max": 1,
    "cooldown": 0.5,
    "damage": 5,
    "range": 3.0,

    "on_use": None,           # right-click: no self-use
    "on_equip": None,         # not equippable — use hotbar
    "on_interact": "attack",  # left-click on entity: attack

    "model": "sword",
    "color": [0.7, 0.7, 0.75],
}
