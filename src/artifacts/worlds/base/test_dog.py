"""Test Dog — minimal world with just the player and one dog.

Used to isolate dog behavior and player-dog interactions (following,
aggression, the brave_chicken's dog-fear logic, etc.) without any
other entities adding noise.

Flat terrain, no trees, no village — purely a sandbox for testing
one behavior at a time.

Launch:
    make test-dog                # singleplayer shortcut
    ./build/solarium-server --template 2   # dedicated
"""

world = {
    "id":          "test_dog",
    "name":        "Test Dog",
    "description": "Flat world, 1 player, 1 dog. Nothing else.",

    "terrain": {
        "type":       "flat",
        "surface_y":  4,
        "dirt_depth": 4,
    },

    "spawn": {
        "x": 0.0,
        "z": 0.0,
    },

    # Starter chest kept far away so it doesn't interfere.
    "chest": {
        "offset_x": 40,
        "offset_z": 40,
    },

    # No portal — just a single spawn-point block, so line of sight is clear.
    "portal":  False,
    "village": None,

    # One dog, spawned 10 blocks from the player so we can watch the
    # "approach → arrive → stop" handoff happen in full.
    "mobs": [
        {"type": "dog", "count": 1, "radius": 10},
    ],
}
