"""Skeleton — undead warrior draped in rusted iron."""

living = {
    "id": "base:skeleton",
    "name": "Skeleton",
    "description": "Undead warrior draped in rusted iron.",

    "category": "humanoid",
    "tags": ["humanoid"],
    "playable": True,

    "model": "skeleton",
    "behavior": "wander",

    # Physics
    "collision": {"min": [-0.3, 0, -0.3], "max": [0.3, 1.85, 0.3]},
    "walk_speed": 2.2,
    "run_speed": 4.8,
    "gravity": 1.0,
    "eye_height": 1.60,
    "jump_velocity": 12.4,
    "max_hp": 22,

    "stats": {"strength": 3, "stamina": 2, "agility": 4, "intelligence": 3},

    "skin_color": [0.88, 0.85, 0.78],
    "primary_color": [0.52, 0.35, 0.22],
    "features": ["bone_crown", "tattered_cloth", "shield_fragment", "chain_belt"],
}
