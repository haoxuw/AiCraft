"""Crewmate — the iconic astronaut with teal visor."""

living = {
    "id": "crewmate",
    "name": "Crewmate",
    "description": "Iconic astronaut with egg-shaped body and teal visor.",

    "category": "humanoid",
    "playable": True,

    "model": "crewmate",
    "behavior": "wander",

    # Physics
    "collision": {"min": [-0.3, 0, -0.3], "max": [0.3, 1.55, 0.3]},
    "walk_speed": 2.3,
    "run_speed": 5.0,
    "gravity": 1.0,
    "eye_height": 1.40,
    "jump_velocity": 13.0,

    "stats": {"strength": 2, "stamina": 3, "agility": 3, "intelligence": 4},

    "primary_color": [0.85, 0.18, 0.18],
    "visor_color": [0.38, 0.92, 0.85],
    "features": ["visor", "backpack", "stubby_legs"],
}
