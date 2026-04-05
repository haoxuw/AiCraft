"""Wood Pickaxe — wooden handle with a gray stone T-shaped head.

A basic mining tool for breaking stone blocks.
"""

model = {
    "id": "wood_pickaxe",
    "height": 1.0,
    # Held in hand: rotated so tool head points forward
    "equip": {
        "rotation": [-25, 0, -15],
        "offset": [0.02, 0.0, -0.05],
        "scale": 0.75,
    },
    "parts": [
        # Handle — long brown stick
        {"offset": [0, 0.22, 0], "size": [0.06, 0.60, 0.06], "color": [0.50, 0.35, 0.15, 1]},
        # Head — horizontal T-bar
        {"offset": [0, 0.54, 0], "size": [0.30, 0.08, 0.08], "color": [0.55, 0.55, 0.58, 1]},
        # Head top edge — slightly darker cap
        {"offset": [0, 0.58, 0], "size": [0.28, 0.02, 0.06], "color": [0.48, 0.48, 0.52, 1]},
    ]
}
