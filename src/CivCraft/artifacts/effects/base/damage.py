"""Damage — deals HP damage to a target entity.

Applied by swords, arrows, explosions, or attack spells.
"""

effect = {
    "id": "damage",
    "name": "Damage",
    "category": "combat",
    "description": "Deals damage to the target entity.",

    "target": "target",         # requires a target entity
    "amount": 5,                # HP removed
    "duration": 0,              # instant
    "range": 3.0,               # melee range
    "knockback": 2.0,           # push target back

    "particles": "hit",
    "sound": "attack_hit",
    "color": [0.9, 0.2, 0.2],  # red particles
}
