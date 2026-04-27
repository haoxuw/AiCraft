"""Giant — massive iron guardian with glowing cracks."""

living = {
    "id": "giant",
    "name": "Giant",
    "description": "Massive iron guardian. Each step shakes the ground.",

    "category": "humanoid",
    "playable": True,

    "model": "giant",
    "behavior": "wander",

    # Physics — standardised on Guy. Same hitbox + speed across every
    # playable so PvP and group-pacing stay fair. Visual scale still
    # differs via the box-model file.
    "collision": {"min": [-0.35, 0, -0.35], "max": [0.35, 2.0, 0.35]},
    "walk_speed": 6.0,
    "run_speed": 9.6,
    "gravity": 1.0,
    "eye_height": 1.70,
    "jump_velocity": 8.0,

    "stats": {"strength": 5, "stamina": 5, "agility": 1, "intelligence": 2},

    "primary_color": [0.45, 0.42, 0.40],
    "glow_color": [1.00, 0.58, 0.08],
    "features": ["glowing_cracks", "giant_fists", "shoulder_bolts", "no_neck"],
}
