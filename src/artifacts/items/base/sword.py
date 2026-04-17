"""Sword — melee weapon. Hold in hotbar, attack with left-click."""

item = {
    "id": "sword",
    "name": "Sword",
    "category": "weapon",
    # No equip_slot — held via hotbar selection (right hand)
    "stack_max": 1,
    "cooldown": 0.5,
    "damage": 2,
    "range": 3.0,

    "on_use": None,           # right-click: no self-use
    "on_equip": None,         # not equippable — use hotbar
    "on_interact": "attack",  # left-click on entity: attack

    # 3-hit combo: left slash → right return → overhead cleave. Clips are
    # registered in platform/client/attack_anim.h. Each chains if the next
    # click lands in the last 30% of the current clip (combo window).
    "attack_animations": "swing_left swing_right cleave",

    "model": "sword",
    "color": [0.7, 0.7, 0.75],
    "held_shape": "blade",
}
