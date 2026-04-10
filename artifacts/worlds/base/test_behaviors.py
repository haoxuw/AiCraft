"""Test Behaviors — small walled arena for E2E behavior validation.

A 40x40 flat arena surrounded by stone walls, with trees, a chest,
and a bed placed at known positions. Used by automated tests to
validate woodcutter, animal, and decision-queue behaviors.

Deliberately small so entities find targets quickly and tests
complete within reasonable time.
"""

world = {
    "id":          "base:test_behaviors",
    "name":        "Test Behaviors",
    "description": "Walled arena for automated behavior testing.",

    # ── Terrain ────────────────────────────────────────────────
    # Natural terrain so trees spawn via treeDensity.
    # Flat terrain skips tree generation entirely.
    "terrain": {
        "type":       "natural",
        "base_height": 8,
        "continent_scale": 200.0,
        "continent_amplitude": 6.0,
    },

    # ── Spawn ─────────────────────────────────────────────────
    "spawn": {
        "x": 0.0,
        "z": 0.0,
    },

    # ── Starter chest ─────────────────────────────────────────
    "chest": {
        "offset_x": 5,
        "offset_z": 0,
    },

    # ── Village ───────────────────────────────────────────────
    # Single small house for the woodcutter — contains bed + chest.
    "village": {
        "offset_x": 15,
        "offset_z": 0,
        "clearing_radius": 10,

        "houses": [
            {"cx": 0, "cz": 0, "w": 8, "d": 8, "stories": 1},
        ],

        "wall_block":  "base:cobblestone",
        "roof_block":  "base:wood",
        "floor_block": "base:cobblestone",
        "path_block":  "base:cobblestone",

        "story_height": 6,
        "door_height":  3,
        "window_row":   2,
    },

    # ── Trees ─────────────────────────────────────────────────
    # Higher density than village world so woodcutter finds trees quickly.
    "trees": {
        "density":          0.04,
        "trunk_height_min": 4,
        "trunk_height_max": 7,
        "leaf_radius":      2,
    },

    # ── Mobs ──────────────────────────────────────────────────
    # Minimal set for testing.
    "mobs": [
        {"type": "base:villager", "count": 1, "radius": 5, "props": {
            "chop_period": 0.1,         # chop every 0.1s instead of 0.5s
            "collect_goal": 3,          # deposit after 3 logs instead of 5
            "work_radius":  40.0,       # pin search radius for deterministic drift tests
        }},
        {"type": "base:pig",      "count": 1, "radius": 10},
        {"type": "base:chicken",  "count": 1, "radius": 10, "props": {
            "scatter_range": 30.0,      # any nearby entity is a "threat" → flee
            "egg_cooldown":  1.0,       # lay eggs every 1s instead of 10s
            "egg_chance":    1.0,       # always lay (100% instead of 80%)
        }},
    ],
}
