"""Heal — restores HP to a living entity.

Applied by potions, food, or healing spells.
"""

effect = {
    "id": "heal",
    "name": "Heal",
    "category": "restoration",
    "description": "Restores health points to the target.",

    "target": "self",           # self, target, area
    "amount": 10,               # HP restored
    "duration": 0,              # 0 = instant
    "cooldown": 1.0,            # seconds between uses

    "particles": "heart",
    "sound": "heal",
    "color": [0.3, 0.9, 0.3],  # green particles
}
