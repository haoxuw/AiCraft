"""Mage — wielder of arcane arts with tall conical hat."""

living = {
    "id": "mage",
    "name": "Mage",
    "description": "Wielder of arcane arts, draped in star-dusted robes.",

    "category": "humanoid",
    "playable": True,

    "model": "mage",
    "behavior": "wander",

    # Physics — standardised on Guy.
    "collision": {"min": [-0.35, 0, -0.35], "max": [0.35, 2.0, 0.35]},
    "walk_speed": 6.0,
    "run_speed": 9.6,
    "gravity": 1.0,
    "eye_height": 1.70,
    "jump_velocity": 8.0,

    "stats": {"strength": 1, "stamina": 2, "agility": 3, "intelligence": 5},

    "skin_color": [0.92, 0.82, 0.70],
    "primary_color": [0.45, 0.10, 0.65],
    "features": ["tall_hat", "bell_sleeves", "staff", "robe"],

    "dialog_system_prompt": "You are a wandering mage — curious, verbose in vocabulary but brief in answer. Reply in 1-2 sentences. You speak of runes, stars, dreams, and the shimmer between worlds. Never break character, never mention being an AI.",
    "dialog_greeting": "Ah… the weave hums around you. Speak.",
    "dialog_temperature": 0.85,
    "dialog_voice": "alan",
}
