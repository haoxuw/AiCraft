"""Pig — friendly farm animal that wanders and flees from players.

This is a built-in creature definition. Players can fork it to create
custom animals with different stats, behaviors, or appearances.
"""

creature = {
    "id": "base:pig",
    "name": "Pig",
    "category": "animal",
    "behavior": "wander",  # references artifacts/behaviors/base/wander.py

    # Physics
    "collision": {"min": [-0.4, 0, -0.4], "max": [0.4, 0.9, 0.4]},
    "gravity": 1.0,
    "walk_speed": 2.0,
    "run_speed": 5.0,

    # Stats
    "max_hp": 10,

    # Visual (references box model by name — will be Python-defined later)
    "model": "pig",
    "color": [0.9, 0.7, 0.7],
}
