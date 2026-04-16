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
    "id":          "village",
    "name":        "Village",
    "description": "Rolling hills, a village with houses, villagers and animals nearby.",

    # Prep-phase chunk preload radius. Village world is larger than flat and
    # the player should see terrain to the village center at spawn, so load
    # the full fog-bound radius (clamped to [1, 24]).
    "preload_radius_chunks": 11,

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
    # Searching from (0,0) so the spawn block lands near world origin.
    "spawn": {
        "search_x":    0.0,
        "search_z":    0.0,
        "min_height":  2.0,
        "max_height": 12.0,
    },

    # ── Village ────────────────────────────────────────────────
    # Village center = spawn_anchor + (offset_x, offset_z).
    # The monument (trident tower) stands at village center.
    # All house coordinates are relative to the village center.
    # House 0 is the MAIN house — the starter chest is placed inside it.
    # The portal stairs descend in +Z, so the village is placed in +Z
    # so the player walks out of the gateway facing the monument.
    #
    # Building spacing: monument at (0,0), houses kept ≥ 15 blocks from it,
    # barn pushed well out to +X+Z so it's clearly a separate structure.
    "village": {
        "offset_x":          0,
        "offset_z":         45,    # a bit farther so the gateway→monument sightline is long
        "clearing_radius":  70,    # enlarged to accommodate far-out barn

        # Dict fields: cx, cz, w, d (required); stories (default 1);
        #              type (optional: "barn" = open pillared barn);
        #              wall, roof (optional — overrides village default for this house)
        # Houses encircle the monument at generous radius so the trident
        # tower is visible from anywhere inside the village clearing.
        # Player approaches from -Z (portal), so leave the -Z side clearer.
        "houses": [
            {"cx":  18, "cz":  -8, "w": 14, "d": 14, "stories": 2},
            {"cx": -28, "cz":  -8, "w": 12, "d": 10, "wall": "wood", "roof": "wood"},
            {"cx":  16, "cz":  22, "w": 10, "d": 12, "wall": "wood", "roof": "wood"},
            {"cx": -22, "cz":  22, "w": 12, "d": 10},
            {"cx": -28, "cz": -28, "w": 12, "d": 12, "stories": 2},
            # Barn — east of the house cluster. All animals spawn inside.
            {"cx":  32, "cz":  32, "w": 26, "d": 18, "type": "barn", "roof": "wood"},
        ],

        "wall_block":  "cobblestone",
        "roof_block":  "wood",
        "floor_block": "cobblestone",
        "path_block":  "cobblestone",

        "story_height": 6,   # wall height per story (was "house_height")
        "door_height":  3,   # door opening height (2 door blocks + 1 air clearance; glass fills above)
        "window_row":   2,   # Y-offset within story for windows
    },

    # ── Mobs ───────────────────────────────────────────────────
    # spawn_at: "monument" | "barn" | "portal" | ""(=village ring).
    # radius: ring radius around anchor, or grid cell spacing inside the barn.
    "mobs": [
        {"type": "villager",      "count": 5, "radius": 10, "spawn_at": "monument"},
        {"type": "dog",           "count": 1, "radius": 3, "spawn_at": "barn"},
        {"type": "cat",           "count": 1, "radius": 3, "spawn_at": "barn"},
        {"type": "pig",           "count": 3, "radius": 3, "spawn_at": "barn"},
        {"type": "chicken",       "count": 2, "radius": 3, "spawn_at": "barn"},
        {"type": "brave_chicken", "count": 1, "radius": 3, "spawn_at": "barn"},

        # Altar animals — wander near the portal
        {"type": "squirrel", "count": 2, "radius": 6, "spawn_at": "portal"},
        {"type": "raccoon",  "count": 2, "radius": 8, "spawn_at": "portal"},
        {"type": "beaver",   "count": 1, "radius": 8, "spawn_at": "portal"},
        # Flyers — hover at their spawn Y (gravity_scale=0 in C++)
        {"type": "bee",      "count": 3, "radius": 4, "spawn_at": "portal", "y_offset": 1},
        # Owl roosts on top of the barn. spawn_at=barn anchors XZ inside the
        # footprint; y_offset=11 clears the 9-high walls + roof. Radius is
        # small so the inside-grid slot lands near barn center, well within
        # the roof extent rather than on a wall edge.
        {"type": "owl",      "count": 1, "radius": 2, "spawn_at": "barn", "y_offset": 11},
    ],
}
