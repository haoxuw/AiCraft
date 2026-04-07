"""Pig — herd animal that grazes and goes home at night."""

creature = {
    "id": "base:pig",
    "name": "Pig",
    "category": "animal",
    "behavior": "wander",

    "collision": {"min": [-0.4, 0, -0.4], "max": [0.4, 0.9, 0.4]},
    "gravity": 1.0,
    "walk_speed": 2.0,
    "run_speed": 5.0,

    "max_hp": 10,
    "flee_range": 5.0,
    "group_range": 6.0,

    "model": "pig",
    "color": [0.9, 0.7, 0.7],
}
