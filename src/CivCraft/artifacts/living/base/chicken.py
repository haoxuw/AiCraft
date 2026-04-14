"""Chicken (Hen) — skittish bird that pecks, flocks, and lays eggs."""

living = {
    "id": "chicken",
    "name": "Chicken",
    "description": "Skittish bird that pecks, flocks, and lays eggs.",

    "category": "animal",
    "playable": True,

    "model": "chicken",
    "behavior": "peck",

    # Physics
    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.6, 0.2]},
    "walk_speed": 2.5,
    "run_speed": 6.0,
    "gravity": 1.0,
    "eye_height": 0.55,
    "jump_velocity": 6.0,

    "primary_color": [0.95, 0.95, 0.90],

    # Behavior props
    "flee_range": 4.0,
    "egg_chance": 0.10,
    "flock_range": 4.0,
}
