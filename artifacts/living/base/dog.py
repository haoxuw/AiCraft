"""Dog — loyal guard companion that follows players and chases cats."""

living = {
    "id": "base:dog",
    "name": "Dog",
    "description": "Loyal guard companion. Follows players and chases cats.",

    "category": "animal",
    "playable": True,

    "model": "dog",
    "behavior": "follow",

    # Physics
    "collision": {"min": [-0.3, 0, -0.3], "max": [0.3, 0.7, 0.3]},
    "walk_speed": 4.0,
    "run_speed": 8.0,
    "gravity": 1.0,
    "eye_height": 0.60,
    "jump_velocity": 9.0,

    "primary_color": [0.75, 0.55, 0.35],

    # Behavior props
    "follow_dist": 3.0,
    "guard_range": 6.0,
    "patrol_range": 12.0,
}
