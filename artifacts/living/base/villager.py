"""Villager — industrious humanoid that chops wood and builds.

Every living thing in ModCraft uses the same `living = {...}` schema.
Fork this file to make your own humanoid — change `stats`, `behavior`,
`features`, or `model` and drop it into artifacts/living/player/.

Try:
  - behavior = 'wander' for a lazy villager
  - behavior = 'follow' for a pet companion
  - playable = True means you can pick this character at the main menu
"""

living = {
    "id": "base:villager",
    "name": "Villager",
    "description": "Industrious humanoid who chops wood, builds, and sleeps in their own bed.",

    # Two groups only — "humanoid" or "animal" (field name stays "category" for
    # compatibility with ArtifactRegistry, which extracts it for subcategory).
    "category": "humanoid",
    "tags": ["humanoid"],
    "playable": True,

    "model": "villager",
    "behavior": "woodcutter",

    # Physics
    "collision": {"min": [-0.3, 0, -0.3], "max": [0.3, 1.8, 0.3]},
    "walk_speed": 2.5,
    "run_speed": 5.0,
    "gravity": 1.0,
    "eye_height": 1.5,
    "jump_velocity": 11.2,
    "max_hp": 20,

    # Character stats (humanoids only — animals omit this)
    "stats": {"strength": 2, "stamina": 3, "agility": 3, "intelligence": 3},

    # Cosmetic
    "skin_color": [0.88, 0.74, 0.60],
    "primary_color": [0.34, 0.44, 0.28],
    "features": ["scarf", "backpack", "goggles", "bandolier"],

    # Behavior props (woodcutter reads these)
    "work_radius": 80.0,
    "collect_goal": 5,
}
