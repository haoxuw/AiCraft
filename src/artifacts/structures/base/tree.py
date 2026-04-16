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
}
