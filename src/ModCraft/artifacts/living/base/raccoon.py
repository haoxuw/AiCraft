"""Raccoon — masked bandit who wanders the village looking for mischief."""

living = {
    "id": "base:raccoon",
    "name": "Raccoon",
    "description": "Masked bandit with a ringed tail. Wandering for now — will raid chests later.",

    "category": "animal",
    "playable": True,

    "model": "raccoon",
    "behavior": "wander",

    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.55, 0.2]},
    "walk_speed": 3.0,
    "run_speed": 6.5,
    "gravity": 1.0,
    "eye_height": 0.45,
    "jump_velocity": 7.5,

    "primary_color": [0.45, 0.45, 0.48],
}
