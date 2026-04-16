"""Pig — herd animal that grazes and goes home at night."""

living = {
    "id": "pig",
    "name": "Pig",
    "description": "Herd animal that grazes peacefully and goes home at night.",

    "category": "animal",
    "playable": True,

    "model": "pig",
    "behavior": "wander",

    # Physics
    "collision": {"min": [-0.4, 0, -0.4], "max": [0.4, 0.9, 0.4]},
    "walk_speed": 2.0,
    "run_speed": 5.0,
    "gravity": 1.0,
    "eye_height": 0.75,
    "jump_velocity": 7.0,

    "primary_color": [0.9, 0.7, 0.7],

    # Behavior props
    "flee_range": 5.0,
    "group_range": 6.0,
}
