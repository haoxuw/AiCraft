"""Beaver — stocky brown rodent that wanders near the spawn for now.

Waits for rivers + tree-chopping behavior later; currently just wanders.
"""

living = {
    "id": "beaver",
    "name": "Beaver",
    "description": "Stocky brown rodent with a paddle tail. Will build dams once rivers exist.",

    "category": "animal",
    "playable": True,

    "model": "beaver",
    "behavior": "wander",

    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.5, 0.2]},
    "walk_speed": 2.5,
    "run_speed": 5.0,
    "gravity": 1.0,
    "eye_height": 0.40,
    "jump_velocity": 6.0,

    "primary_color": [0.42, 0.26, 0.14],
}
