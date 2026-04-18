blueprint = {
    "id": "tree",
    "display_name": "Oak Tree",
    "regenerates": True,
    "regen_interval_s": 30,
    "anchor": {
        "block_type": "root",
        "offset": [0, -1, 0],
        "hardness": 16,
    },
    "blocks": [
        {"offset": [0, 0, 0], "type": "logs"},
        {"offset": [0, 1, 0], "type": "logs"},
        {"offset": [0, 2, 0], "type": "logs"},
        {"offset": [1, 2, 0], "type": "leaves"},
        {"offset": [-1, 2, 0], "type": "leaves"},
        {"offset": [0, 2, 1], "type": "leaves"},
        {"offset": [0, 2, -1], "type": "leaves"},
        {"offset": [0, 3, 0], "type": "leaves"},
    ],
    # Each tree rolls once per season: on hit, every leaf in ITS OWN canopy
    # flips to a single randomly-picked variant and the tree is locked for the
    # rest of the season. spawn_transition_chance seeds a fresh world already
    # in-season (~half the trees arrive pre-transitioned), and per_tick_prob
    # at 1 Hz spreads the remainder gradually — ~200s expected per tree so
    # the forest visibly turns over minutes, not seconds.
    "features": [
        {
            "type": "seasonal_leaves",
            "spring_variants": ["leaves_spring", "leaves"],
            "summer_variants": ["leaves", "leaves_summer"],
            "autumn_variants": ["leaves_gold", "leaves_orange", "leaves_red"],
            "winter_variants": ["leaves_bare", "leaves_snow"],
            "per_tick_prob": 0.005,
            "spawn_transition_chance": 0.5,
        },
    ],
}
