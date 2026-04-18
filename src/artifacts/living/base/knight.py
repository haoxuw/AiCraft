"""Knight — stalwart defender in steel-trimmed blue plate."""

living = {
    "id": "knight",
    "name": "Knight",
    "description": "Stalwart defender in steel-trimmed blue plate.",

    "category": "humanoid",
    "playable": True,

    "model": "knight",
    "behavior": "wander",

    # Physics
    "collision": {"min": [-0.35, 0, -0.35], "max": [0.35, 2.0, 0.35]},
    "walk_speed": 2.0,
    "run_speed": 4.5,
    "gravity": 1.0,
    "eye_height": 1.70,
    "jump_velocity": 11.2,

    "stats": {"strength": 4, "stamina": 4, "agility": 2, "intelligence": 2},

    "skin_color": [0.85, 0.70, 0.55],
    "hair_color": [0.25, 0.18, 0.12],
    "primary_color": [0.18, 0.32, 0.72],
    "features": ["cape", "pauldrons", "chest_plate", "gauntlets", "shin_guards"],

    "dialog_system_prompt": "You are a knight of the realm — gruff, formal, economical with words. Reply in 1-2 short sentences, addressing the player as 'traveller' or 'sir'. Speak of duty, honour, the watch, rumours of beasts. Never break character, never mention being an AI.",
    "dialog_greeting": "Halt. State your business, traveller.",
    "dialog_temperature": 0.6,
}
