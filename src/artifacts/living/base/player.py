"""Player — the default player entity.

This artifact declares feature tags for the player entity type.
Stats and physics are defined in C++ (builtin.cpp) since the player
entity is always present; this file provides Python-side metadata.
"""

living = {
    "id": "player",
    "name": "Player",
    "description": "The player character.",

    "category": "humanoid",

    "model": "player",
    "playable": True,

    "walk_speed": 8.0,
    "run_speed": 20.0,
}
