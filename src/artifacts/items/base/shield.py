"""Shield — blocks incoming damage. Left-click to raise and block."""

item = {
    "id": "shield",
    "name": "Shield",
    "category": "shield",
    "equip_slot": "offhand",
    "stack_max": 1,
    "cooldown": 0.0,
    "damage_reduction": 0.5,    # blocks 50% of incoming damage

    "on_use": None,             # right-click: no self-use
    "on_equip": "equip",        # E key: equip to right hand
    "on_interact": "block",     # left-click: raise shield to block

    "model": "shield",
    "color": [0.45, 0.30, 0.15],
    "held_shape": "plate",
}
