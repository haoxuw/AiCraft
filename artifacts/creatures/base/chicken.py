"""Chicken (Hen) — skittish bird that pecks, flocks, and lays eggs."""

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
    "flee_range": 4.0,
    "egg_chance": 0.10,
    "flock_range": 4.0,

    "model": "chicken",
    "color": [0.95, 0.95, 0.90],
}
