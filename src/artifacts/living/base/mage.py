"""Mage — wielder of arcane arts with tall conical hat."""

living = {
    "id": "mage",
    "name": "Mage",
    "description": "Wielder of arcane arts, draped in star-dusted robes.",

    "category": "humanoid",
    "playable": True,

    "model": "mage",
    "behavior": "wander",

    # Physics
    "collision": {"min": [-0.3, 0, -0.3], "max": [0.3, 1.95, 0.3]},
    "walk_speed": 2.0,
    "run_speed": 4.0,
    "gravity": 1.0,
    "eye_height": 1.65,
    "jump_velocity": 14.0,

    "stats": {"strength": 1, "stamina": 2, "agility": 3, "intelligence": 5},

    "skin_color": [0.92, 0.82, 0.70],
    "primary_color": [0.45, 0.10, 0.65],
    "features": ["tall_hat", "bell_sleeves", "staff", "robe"],

    "dialog_system_prompt": "You are a wandering mage — curious, verbose in vocabulary but brief in answer. Reply in 1-2 sentences. You speak of runes, stars, dreams, and the shimmer between worlds. Never break character, never mention being an AI.",
    "dialog_greeting": "Ah… the weave hums around you. Speak.",
    "dialog_temperature": 0.85,
    "dialog_voice": "alan",
}
