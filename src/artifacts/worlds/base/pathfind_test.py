"""Pathfind Test World — deterministic empty arena for headless tests.

Flat grass plane, spawn at origin, NO village, NO starter chest, NO mobs.
Each pathfinding subtest places its own obstacles (walls, buildings, pits)
via direct chunk writes after world creation, so the arena must start
completely empty and predictable.

Not intended for human play. Loaded only by civcraft-test-pathfinding.
"""

world = {
    "id":          "pathfind_test",
    "name":        "Pathfind Test Arena",
    "description": "Empty flat arena used by headless pathfinding tests.",

    "preload_radius_chunks": 4,

    "terrain": {
        "type":      "flat",
        "surface_y": 4,
        "dirt_depth": 4,
    },

    "spawn": {
        "x": 0.0,
        "z": 0.0,
    },

    # No chest, no village, no mobs — subtests program the arena.
    "mobs": [],
}
