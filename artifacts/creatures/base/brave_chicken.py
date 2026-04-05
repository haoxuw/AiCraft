"""Brave Chicken — a fearless golden hen that follows players and fights cats.

Unlike the normal timid chicken, this variant:
  - Follows players like a loyal pet
  - Charges at cats instead of running away
  - Lays eggs when near the player (happy = productive!)
  - Only fears dogs
  - Has more HP (tougher fighter)
  - Distinctive golden color

Uses the 'brave_chicken' behavior (artifacts/behaviors/base/brave_chicken.py).
Fork this creature to create your own chicken personality!
"""

creature = {
    "id": "base:brave_chicken",
    "name": "Brave Chicken",
    "category": "animal",
    "behavior": "brave_chicken",

    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.6, 0.2]},
    "gravity": 1.0,
    "walk_speed": 3.0,
    "run_speed": 7.0,

    "max_hp": 8,

    "model": "chicken",
    "color": [1.0, 0.85, 0.30],   # golden yellow — stands out from white chickens

    "description": "A fearless golden hen. Follows players, fights cats, lays eggs when happy.",
}
