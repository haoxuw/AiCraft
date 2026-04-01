"""Dog — loyal companion that follows the nearest player.

Dogs are faster than most animals and stay close to their owner.
They sit when the player is nearby and idle.

Try modifying:
  - walk_speed to make the dog faster/slower
  - behavior to change from 'follow' to 'wander' for a stray dog
  - max_hp to make a tougher guard dog
"""

creature = {
    "id": "base:dog",
    "name": "Dog",
    "category": "animal",
    "behavior": "follow",  # references artifacts/behaviors/base/follow.py

    "collision": {"min": [-0.3, 0, -0.3], "max": [0.3, 0.7, 0.3]},
    "gravity": 1.0,
    "walk_speed": 4.0,  # fast enough to keep up with player
    "run_speed": 8.0,

    "max_hp": 15,

    "model": "dog",
    "color": [0.75, 0.55, 0.35],  # brown
}
