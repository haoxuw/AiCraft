"""Test Chicken — minimal world for Flee anchor regression.

Flat terrain, player + 2 chickens spawned within Threatened range. On first
decide each chicken picks Flee (player is a non-same-species Living). The
server's anchor aim pass then scares them away from the player each tick
without re-deciding, until they're past `distance=12` blocks.

Walk toward a chicken and strafe — it should curve away from you rather than
sprinting to one fixed spot.

Launch:
    ./build/solarium-ui-vk --skip-menu --template 4
"""

world = {
    "id":          "test_chicken",
    "name":        "Test Chicken",
    "description": "Flat world, 1 player, 2 chickens. Scare-test for Flee anchor.",

    "terrain": {
        "type":       "flat",
        "surface_y":  4,
        "dirt_depth": 4,
    },

    "spawn": {"x": 0.0, "z": 0.0},

    "chest":   {"offset_x": 40, "offset_z": 40},
    "portal":  False,
    "village": None,

    # Inside Peck.Threatened(range=4). Chicken picks Flee immediately.
    "mobs": [
        {"type": "chicken", "count": 2, "radius": 3},
    ],
}
