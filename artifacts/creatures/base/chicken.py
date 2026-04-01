"""Chicken — skittish bird that pecks at the ground."""

creature = {
    "id": "base:chicken",
    "name": "Chicken",
    "category": "animal",
    "behavior": "peck",

    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.6, 0.2]},
    "gravity": 1.0,
    "walk_speed": 2.5,
    "run_speed": 6.0,

    "max_hp": 5,

    "model": "chicken",
    "color": [0.95, 0.95, 0.90],
}
