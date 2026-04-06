"""Flat World — flat grass plane with a small village.

A flat building surface with a spawn portal, a compact village,
and animals roaming nearby. Great for building and testing.

Try modifying:
  - surface_y to change the floor height
  - village houses to add/remove buildings
  - mobs list to change which creatures spawn
"""

world = {
    "id":          "base:flat",
    "name":        "Flat World",
    "description": "Flat grass plane with a small village and animals.",

    # ── Terrain ────────────────────────────────────────────────
    "terrain": {
        "type":      "flat",
        "surface_y": 4,      # grass at y=4; dirt y=0..3; stone below
        "dirt_depth": 4,
    },

    # ── Spawn ─────────────────────────────────────────────────
    "spawn": {
        "x": 30.0,
        "z": 30.0,
    },

    # ── Starter chest ─────────────────────────────────────────
    "chest": {
        "offset_x": 5,
        "offset_z": 0,
    },

    # ── Village ───────────────────────────────────────────────
    "village": {
        "offset_x": 30,
        "offset_z": 8,
        "clearing_radius": 30,

        "houses": [
            {"cx": 0,   "cz": 0,   "w": 12, "d": 12, "stories": 2},
            {"cx": 18,  "cz": -5,  "w": 10, "d": 10, "wall": "base:wood", "roof": "base:wood"},
            {"cx": -16, "cz": 6,   "w": 10, "d": 10, "wall": "base:wood", "roof": "base:wood"},
        ],

        "wall_block":  "base:cobblestone",
        "roof_block":  "base:wood",
        "floor_block": "base:cobblestone",
        "path_block":  "base:cobblestone",

        "story_height": 6,
        "door_height":  5,
        "window_row":   2,
    },

    # ── Mobs ──────────────────────────────────────────────────
    "mobs": [
        {"type": "base:villager", "count": 2, "radius": 8},
        {"type": "base:pig",      "count": 3, "radius": 18},
        {"type": "base:chicken",  "count": 2, "radius": 15},
        {"type": "base:dog",      "count": 1, "radius": 12},
        {"type": "base:cat",      "count": 1, "radius": 10},
    ],
}
