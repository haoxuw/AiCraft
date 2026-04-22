"""Test Villager — minimal world with one villager, one dog, one tree,
one house and one chest.

Isolates the woodcutter flow (wake → find tree → chop → deposit → sleep)
with the minimum surrounding state needed to exercise it end-to-end.

Natural terrain with a tight clearing gives a small handful of trees
very close to the villager's house.

Launch:
    make test-villager                       # singleplayer shortcut
    ./build/civcraft-server --template 4     # dedicated
"""

world = {
    "id":          "test_villager",
    "name":        "Test Villager",
    "description": "1 player, 1 villager, 1 dog, 1 tree, 1 house, 1 chest.",

    # Natural terrain so trees can spawn; small amplitude keeps it flat-ish.
    "terrain": {
        "type":                 "natural",
        "base_height":          8,
        "continent_scale":      200.0,
        "continent_amplitude":  3.0,
    },

    "spawn": {
        "x": 0.0,
        "z": 0.0,
    },

    # No portal — just a single spawn-point block, so line of sight is clear.
    "portal": False,

    # Chest placed near spawn so the player can inspect it easily.
    "chest": {
        "offset_x": 3,
        "offset_z": 0,
    },

    # Single small house for the villager (bed + chest auto-placed inside).
    "village": {
        "offset_x":        15,
        "offset_z":        0,
        "clearing_radius": 6,      # small clearing so trees spawn just outside

        "houses": [
            {"cx": 0, "cz": 0, "w": 8, "d": 8, "stories": 1},
        ],

        "wall_block":  "cobblestone",
        "roof_block":  "wood",
        "floor_block": "cobblestone",
        "path_block":  "cobblestone",

        "story_height": 6,
        "door_height":  3,
        "window_row":   2,
    },

    # Sparse trees so we usually have just 1-2 near the house.
    "trees": {
        "density":          0.006,
        "trunk_height_min": 5,
        "trunk_height_max": 6,
        "leaf_radius":      2,
    },

    "mobs": [
        # spawn_at="monument" places the villager on a ring around the
        # village monument. No per-villager home/chest props — the
        # woodcutter behavior discovers chests via scan_entities("chest").
        {"type": "villager", "count": 1, "radius": 3, "spawn_at": "monument",
         "props": {
            "collect_goal": 3,
            "work_radius":  40.0,
        }},
        {"type": "dog",      "count": 1, "radius": 10},
    ],
}
