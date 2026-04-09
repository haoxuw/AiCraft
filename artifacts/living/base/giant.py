"""Giant — massive iron guardian with glowing cracks."""

living = {
    "id": "base:giant",
    "name": "Giant",
    "description": "Massive iron guardian. Each step shakes the ground.",

    "category": "humanoid",
    "playable": True,

    "model": "giant",
    "behavior": "wander",

    # Physics
    "collision": {"min": [-0.6, 0, -0.6], "max": [0.6, 3.2, 0.6]},
    "walk_speed": 1.4,
    "run_speed": 3.0,
    "gravity": 1.0,
    "eye_height": 2.80,
    "jump_velocity": 9.0,
    "max_hp": 80,

    "stats": {"strength": 5, "stamina": 5, "agility": 1, "intelligence": 2},

    "primary_color": [0.45, 0.42, 0.40],
    "glow_color": [1.00, 0.58, 0.08],
    "features": ["glowing_cracks", "giant_fists", "shoulder_bolts", "no_neck"],
}
