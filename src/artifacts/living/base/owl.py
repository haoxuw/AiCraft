"""Owl — brown-and-cream flyer with huge yellow eyes.

Hovers at spawn Y (gravity_scale=0 set in C++ builtin.cpp). Gets a real
behavior (night hunter? perch watcher?) later.
"""

living = {
    "id": "owl",
    "name": "Owl",
    "description": "Huge-eyed night flyer. Just wandering in the air for now.",

    "category": "animal",
    "playable": True,

    "model": "owl",
    "behavior": "flyer_wander",

    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.8, 0.2]},
    "walk_speed": 3.0,
    "run_speed": 6.0,
    "gravity": 0.0,
    "eye_height": 0.60,
    "jump_velocity": 0.0,

    "primary_color": [0.45, 0.30, 0.15],
}
