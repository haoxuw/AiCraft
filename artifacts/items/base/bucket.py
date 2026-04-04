"""Bucket — holdable tool for picking up and placing water."""

item = {
    "id": "base:bucket",
    "name": "Bucket",
    "category": "tool",
    "equip_slot": "right_hand",
    "stack_max": 1,
    "cooldown": 0.3,
    "range": 4.0,

    "on_use": None,               # right-click: no self-use
    "on_equip": "equip",          # E key: hold in right hand
    "on_interact": "bucket_use",  # left-click on water: scoop; on ground: pour

    "model": "bucket",
    "color": [0.6, 0.6, 0.6],
}
