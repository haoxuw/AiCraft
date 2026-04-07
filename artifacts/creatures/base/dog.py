"""Dog — loyal guard companion that follows players and chases cats."""

creature = {
    "id": "base:dog",
    "name": "Dog",
    "category": "animal",
    "behavior": "follow",

    "collision": {"min": [-0.3, 0, -0.3], "max": [0.3, 0.7, 0.3]},
    "gravity": 1.0,
    "walk_speed": 4.0,
    "run_speed": 8.0,

    "max_hp": 15,
    "follow_dist": 3.0,
    "guard_range": 6.0,
    "patrol_range": 12.0,

    "model": "dog",
    "color": [0.75, 0.55, 0.35],
}
