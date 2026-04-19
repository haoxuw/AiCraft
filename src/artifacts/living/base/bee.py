"""Bee — small yellow-and-black flyer that hovers near the altar.

Hovers at spawn Y (gravity_scale=0 set in C++ builtin.cpp). Will pollinate
flowers and tend a beenest once those behaviors exist.
"""

living = {
    "id": "bee",
    "name": "Bee",
    "description": "Tiny buzzing pollinator. Will tend a beenest once it has a real behavior.",

    "category": "animal",
    "playable": False,

    "model": "bee",
    "behavior": "flyer_wander",

    "collision": {"min": [-0.1, 0, -0.1], "max": [0.1, 0.3, 0.1]},
    "walk_speed": 4.0,
    "run_speed": 8.0,
    "gravity": 0.0,
    "eye_height": 0.20,
    "jump_velocity": 0.0,

    "primary_color": [0.95, 0.78, 0.15],
}
