"""Brave Chicken — a fearless golden hen that follows players and fights cats.

Unlike the normal timid chicken, this variant:
  - Follows players like a loyal pet
  - Charges at cats instead of running away
  - Lays eggs when near the player (happy = productive!)
  - Only fears dogs
  - Has more HP (tougher fighter)
  - Distinctive golden color

Fork this file to create your own chicken personality!
"""

living = {
    "id": "base:brave_chicken",
    "name": "Brave Chicken",
    "description": "A fearless golden hen. Follows players, fights cats, lays eggs when happy.",

    "category": "animal",
    "playable": True,

    "model": "chicken",
    "behavior": "brave_chicken",

    # Physics
    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.6, 0.2]},
    "walk_speed": 3.0,
    "run_speed": 7.0,
    "gravity": 1.0,
    "eye_height": 0.55,
    "jump_velocity": 6.5,
    "max_hp": 8,

    "primary_color": [1.0, 0.85, 0.30],   # golden — stands out from white chickens
}
