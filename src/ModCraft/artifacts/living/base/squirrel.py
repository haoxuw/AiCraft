"""Squirrel — small twitchy rodent that scampers around the altar."""

living = {
    "id": "base:squirrel",
    "name": "Squirrel",
    "description": "Quick little rodent with a big bushy tail.",

    "category": "animal",
    "playable": True,

    "model": "squirrel",
    "behavior": "wander",

    "collision": {"min": [-0.12, 0, -0.12], "max": [0.12, 0.35, 0.12]},
    "walk_speed": 4.0,
    "run_speed": 9.0,
    "gravity": 1.0,
    "eye_height": 0.28,
    "jump_velocity": 9.0,

    "primary_color": [0.55, 0.32, 0.15],
}
