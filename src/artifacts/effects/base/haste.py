"""Speed Boost — temporarily increases movement speed.

Applied by speed potions or enchantments.
"""

effect = {
    "id": "haste",
    "name": "Haste",
    "category": "buff",
    "description": "Increases movement speed by 50% for the duration.",

    "target": "self",
    "multiplier": 1.5,          # 50% faster
    "duration": 10.0,           # lasts 10 seconds
    "cooldown": 30.0,           # 30 second cooldown

    "particles": "swirl",
    "sound": "buff_apply",
    "color": [0.3, 0.7, 1.0],  # blue particles
}
