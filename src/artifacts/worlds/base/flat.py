"""Flat World — flat grass plane with a small village.

A flat building surface with a spawn portal, a compact village,
and animals roaming nearby. Great for building and testing.

Try modifying:
  - surface_y to change the floor height
  - village houses to add/remove buildings
  - mobs list to change which creatures spawn
"""

world = {
    "id":          "flat",
    "name":        "Flat World",
    "description": "Flat grass plane with a small village and animals.",

    # Prep-phase chunk preload radius. A flat world is trivial to generate,
    # so a smaller radius here keeps joins snappy while post-spawn streaming
    # fills in the rest (clamped to [1, 24]).
    "preload_radius_chunks": 6,

    # ── Terrain ────────────────────────────────────────────────
    "terrain": {
        "type":      "flat",
        "surface_y": 4,      # grass at y=4; dirt y=0..3; stone below
        "dirt_depth": 4,
    },

    # ── Spawn ─────────────────────────────────────────────────
    # Portal anchors at world origin so spawn block is at (0, *, 0).
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
    "village": {
        "offset_x": 30,
        "offset_z": 8,
        "clearing_radius": 30,

        "houses": [
            {"cx": 0,   "cz": 0,   "w": 12, "d": 12, "stories": 2},
            {"cx": 18,  "cz": -5,  "w": 10, "d": 10, "wall": "wood", "roof": "wood"},
            {"cx": -16, "cz": 6,   "w": 10, "d": 10, "wall": "wood", "roof": "wood"},
        ],

        "wall_block":  "cobblestone",
        "roof_block":  "wood",
        "floor_block": "cobblestone",
        "path_block":  "cobblestone",

        "story_height": 6,
        "door_height":  3,
        "window_row":   2,
    },

    # ── Mobs ──────────────────────────────────────────────────
    "mobs": [
        {"type": "villager", "count": 2, "radius": 8},
        {"type": "pig",      "count": 3, "radius": 18},
        {"type": "chicken",  "count": 2, "radius": 15},
        {"type": "dog",      "count": 1, "radius": 12},
        {"type": "cat",      "count": 1, "radius": 10},
    ],
}
