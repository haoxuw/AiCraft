"""Cat — independent feline that chases chickens and naps.

Cats wander slowly, chase chickens that get too close,
and take frequent naps. Very independent.
"""

creature = {
    "id": "base:cat",
    "name": "Cat",
    "category": "animal",
    "behavior": "prowl",  # references artifacts/behaviors/base/prowl.py

    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.5, 0.2]},
    "gravity": 1.0,
    "walk_speed": 3.5,
    "run_speed": 7.0,

    "max_hp": 8,

    "model": "cat",
    "color": [0.90, 0.55, 0.20],
}
