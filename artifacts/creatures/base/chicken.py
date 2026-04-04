"""Chicken (Hen) — skittish bird that pecks, flocks, and lays eggs.

Rule composition (priority order):
  1. flee_threats — run from players and cats
  2. lay_egg — drop egg when startled (10% chance, 60s cooldown)
  3. roost — seek elevated perch at dusk
  4. flock — stay near other chickens
  5. dust_bathe — roll in dirt occasionally
  6. peck_ground — peck at seeds
  7. wander_slow — default fallback
"""

creature = {
    "id": "base:chicken",
    "name": "Chicken",
    "category": "animal",
    "behaviors": ["flee_threats", "lay_egg", "roost", "flock", "dust_bathe", "peck_ground", "wander_slow"],

    "collision": {"min": [-0.2, 0, -0.2], "max": [0.2, 0.6, 0.2]},
    "gravity": 1.0,
    "walk_speed": 2.5,
    "run_speed": 6.0,

    "max_hp": 5,
    "flee_range": 4.0,
    "egg_chance": 0.10,
    "flock_range": 4.0,

    "model": "chicken",
    "color": [0.95, 0.95, 0.90],
}
