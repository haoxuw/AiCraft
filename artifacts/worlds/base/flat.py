"""Flat World — perfectly flat grass plane.

Ideal for building, testing, and scripting.
The chest spawns 5 blocks east of the player.

Try modifying:
  - surface_y to change the floor height
  - Add a "village" section to place structures
  - Add mobs to populate the world
"""

world = {
    "id":          "base:flat",
    "name":        "Flat World",
    "description": "A perfectly flat grass plane. Great for building.",

    # ── Terrain ────────────────────────────────────────────────
    "terrain": {
        "type":      "flat",
        "surface_y": 4,      # grass surface at y=4; dirt y=0..3; stone below
        "dirt_depth": 4,
    },

    # ── Spawn ─────────────────────────────────────────────────
    # Fixed spawn position (flat world has no interesting terrain to search).
    "spawn": {
        "x": 30.0,
        "z": 30.0,
    },

    # ── Starter chest ─────────────────────────────────────────
    # Placed this many blocks east/north of spawn so it's never under the player.
    "chest": {
        "offset_x": 5,
        "offset_z": 0,
    },

    # ── No village, trees, or mobs by default ─────────────────
    "village": None,
    "trees":   None,
    "mobs":    [],
}
