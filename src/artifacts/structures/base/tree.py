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
    # Server applies these every StructureFeature tick (~1 Hz). Each tree
    # independently rolls per_tick_prob to switch its leaves to a random
    # variant from the current season's palette — so at mid-season some
    # trees are still green while others have already turned.
    "features": [
        {
            "type": "seasonal_leaves",
            "spring_variants": ["leaves_spring", "leaves"],
            "summer_variants": ["leaves", "leaves_summer"],
            "autumn_variants": ["leaves_gold", "leaves_orange", "leaves_red"],
            "winter_variants": ["leaves_bare", "leaves_snow"],
            "per_tick_prob": 0.02,
            "scan_radius": 3,
        },
    ],
}
