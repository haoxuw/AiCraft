"""Skeleton — undead warrior draped in rusted iron."""

living = {
    "id": "skeleton",
    "name": "Skeleton",
    "description": "Undead warrior draped in rusted iron.",

    "category": "humanoid",
    "playable": True,

    "model": "skeleton",
    "behavior": "wander",

    # Physics — standardised on Guy.
    "collision": {"min": [-0.35, 0, -0.35], "max": [0.35, 2.0, 0.35]},
    "walk_speed": 6.0,
    "run_speed": 9.6,
    "gravity": 1.0,
    "eye_height": 1.70,
    "jump_velocity": 8.0,

    "stats": {"strength": 3, "stamina": 2, "agility": 4, "intelligence": 3},

    "skin_color": [0.88, 0.85, 0.78],
    "primary_color": [0.52, 0.35, 0.22],
    "features": ["bone_crown", "tattered_cloth", "shield_fragment", "chain_belt"],
}
