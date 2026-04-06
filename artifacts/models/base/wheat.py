"""Wheat — golden mature wheat crop.

Fully grown wheat ready for harvest. Drops wheat items.
"""

model = {
    "id": "wheat",
    "height": 0.6,
    "equip": {
        "rotation": [-20, 10, 0],   # lean forward so grain head is prominent from behind
        "offset": [0, 0.0, -0.04],
        "scale": 0.75,
    },
    "parts": [
        # Stalks — thin golden stems
        {"offset": [0, 0.16, 0], "size": [0.14, 0.32, 0.14], "color": [0.78, 0.68, 0.25, 1]},
        # Grain heads — top cluster
        {"offset": [0, 0.36, 0], "size": [0.18, 0.10, 0.18], "color": [0.82, 0.72, 0.28, 1]},
    ]
}
