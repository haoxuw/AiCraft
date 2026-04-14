"""Goose — long-necked white flyer with a bright orange beak.

Hovers at spawn Y (gravity_scale=0 set in C++ builtin.cpp). Gets a honking
territorial behavior later.
"""

living = {
    "id": "base:goose",
    "name": "Goose",
    "description": "Long-necked honker. Just wandering for now — will chase trespassers later.",

    "category": "animal",
    "playable": True,

    "model": "goose",
    "behavior": "wander",

    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 1.0, 0.2]},
    "walk_speed": 3.5,
    "run_speed": 7.0,
    "gravity": 0.0,
    "eye_height": 0.85,
    "jump_velocity": 0.0,

    "primary_color": [0.94, 0.93, 0.90],
}
