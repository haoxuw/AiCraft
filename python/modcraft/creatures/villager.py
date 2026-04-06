"""Villager — industrious NPC that gathers resources and builds.

Villagers work autonomously: they search for trees, chop wood,
and build simple structures. They have a work/rest cycle.

Try modifying:
  - behavior to 'wander' for a lazy villager
  - walk_speed to make them more/less efficient
  - Create a new behavior .py file for custom NPC jobs!
"""

creature = {
    "id": "base:villager",
    "name": "Villager",
    "category": "animal",  # uses same AI dispatch
    "behavior": "villager",  # references artifacts/behaviors/base/villager.py

    "collision": {"min": [-0.3, 0, -0.3], "max": [0.3, 1.8, 0.3]},
    "gravity": 1.0,
    "walk_speed": 2.5,
    "run_speed": 5.0,
    "eye_height": 1.5,

    "max_hp": 20,

    "model": "villager",
    "color": [0.85, 0.75, 0.60],  # tan
}
