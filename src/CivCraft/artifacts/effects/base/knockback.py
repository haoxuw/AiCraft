"""Knockback — pushes target entity away from the source.

Applied by shields, explosions, or heavy attacks.
"""

effect = {
    "id": "knockback",
    "name": "Knockback",
    "category": "combat",
    "description": "Pushes the target away from the attacker.",

    "target": "target",
    "force": 8.0,               # push strength
    "duration": 0,              # instant

    "particles": "impact",
    "sound": "shield_bash",
    "color": [0.8, 0.8, 0.5],
}
