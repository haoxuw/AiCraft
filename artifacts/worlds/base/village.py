"""Village World — rolling terrain with a village, wildlife, and trees.

The player spawns on flat ground about 40 blocks from the village center.
The village has 5 houses (the center house holds the starter chest),
cobblestone paths, villagers, and animals grazing nearby.

Try modifying:
  - village.offset_x / offset_z to move the village closer or farther
  - village.houses to add/remove/resize buildings
  - terrain parameters to create different landscape styles
  - mobs list to change which creatures spawn and how many
  - Change wall_block / roof_block to use different materials

All mob radii are measured from the village center.  The furthest mobs
(pigs at radius 22) are about 60 blocks from the player spawn — always
visible on first look around.
"""

world = {
    "id":          "base:village",
    "name":        "Village",
    "description": "Rolling hills, a village with houses, villagers and animals nearby.",

    # ── Terrain ────────────────────────────────────────────────
    # Continental-style terrain: large flat plains separated by gradual hills.
    # Amplitudes and scales are tuned so most terrain is 0-12 blocks above
    # sea level, with rare mountain peaks reaching 20-24.
    "terrain": {
        "type": "natural",

        # Low-frequency continental base — determines land vs ocean and biome class
        "continent_scale":     0.004,
        "continent_amplitude": 18.0,

        # Medium hills — amplitude scales with continent height (hills on land only)
        "hill_scale":          0.024,
        "hill_amplitude":      6.0,

        # Surface detail — gentle undulation
        "detail_scale":        0.09,
        "detail_amplitude":    2.2,

        # Micro detail — fine surface grain
        "micro_scale":         0.26,
        "micro_amplitude":     0.7,

        "water_level":    -2,   # blocks at/below this Y become ocean
        "snow_threshold": 22,   # blocks at/above this Y get snow cap
        "dirt_depth":      4,   # layers of dirt under surface grass
    },

    # ── Trees ──────────────────────────────────────────────────
    "trees": {
        "density":          0.025,  # probability per eligible surface block
        "trunk_height_min": 5,
        "trunk_height_max": 9,
        "leaf_radius":      3,
    },

    # ── Spawn search ───────────────────────────────────────────
    # C++ scans outward from this origin to find flat terrain where
    # height is in [min_height, max_height].  The found point becomes
    # the spawn anchor; the village is placed at spawn + village.offset.
    "spawn": {
        "search_x":   30.0,
        "search_z":   30.0,
        "min_height":  2.0,
        "max_height": 12.0,
    },

    # ── Village ────────────────────────────────────────────────
    # Village center = spawn_anchor + (offset_x, offset_z).
    # All house coordinates are relative to the village center.
    # House 0 is the MAIN house — the starter chest is placed inside it.
    "village": {
        "offset_x":        40,
        "offset_z":        12,
        "clearing_radius": 28,    # radius of tree-free clearing around center

        # [offset_x, offset_z, width, depth]  —  all relative to village center
        "houses": [
            [  0,   0,   9, 9],   # main house (chest inside)
            [ 20,  -6,   7, 8],
            [-18,   7,   8, 7],
            [  7,  22,   7, 8],
            [-14, -19,   8, 7],
        ],

        "wall_block":  "base:cobblestone",
        "roof_block":  "base:wood",
        "floor_block": "base:cobblestone",
        "path_block":  "base:cobblestone",

        "house_height": 5,   # wall height in blocks
        "door_height":  3,   # door opening height
        "window_row":   2,   # Y-offset within wall for windows
    },

    # ── Mobs ───────────────────────────────────────────────────
    # radius = max distance from VILLAGE CENTER (not spawn).
    # With village at ~41 blocks from spawn, all mobs below radius 19
    # stay within 60 blocks of the player at start.
    "mobs": [
        {"type": "base:villager", "count": 3, "radius": 10},
        {"type": "base:pig",      "count": 4, "radius": 22},
        {"type": "base:chicken",  "count": 3, "radius": 18},
        {"type": "base:dog",      "count": 2, "radius": 14},
        {"type": "base:cat",      "count": 2, "radius": 12},
    ],
}
