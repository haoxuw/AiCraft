"""Pig — herd animal that grazes, wallows in mud, and stampedes.

Rule composition (priority order):
  1. flee_threats — run from players and cats
  2. herd_stampede — join stampede if friend is panicking
  3. mud_seek — seek water/mud to wallow in
  4. herd_stick — stay near herd members
  5. graze — stop and eat grass
  6. wander_slow — default fallback
"""

creature = {
    "id": "base:pig",
    "name": "Pig",
    "category": "animal",
    "behaviors": ["flee_threats", "herd_stampede", "mud_seek", "herd_stick", "graze", "wander_slow"],

    "collision": {"min": [-0.4, 0, -0.4], "max": [0.4, 0.9, 0.4]},
    "gravity": 1.0,
    "walk_speed": 2.0,
    "run_speed": 5.0,

    "max_hp": 10,
    "flee_range": 5.0,
    "group_range": 6.0,

    "model": "pig",
    "color": [0.9, 0.7, 0.7],
}
