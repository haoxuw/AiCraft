"""Shield — blocks incoming damage while held."""

item = {
    "id": "base:shield",
    "name": "Shield",
    "category": "shield",
    "equip_slot": "right_hand",
    "stack_max": 1,
    "cooldown": 0.0,

    "on_use": "block",
    "damage_reduction": 0.5,    # blocks 50% of incoming damage

    "model": "shield",
    "color": [0.45, 0.30, 0.15],
}
