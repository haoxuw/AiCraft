"""Stone Pickaxe — wooden handle with a darker stone T-shaped head.

A sturdier pickaxe made with cobblestone.
"""

model = {
    "id": "stone_pickaxe",
    "height": 1.0,
    # Held in hand: rotated so tool head points forward
    "equip": {
        "rotation": [-90, 0, 0],
        "offset": [0, -0.08, -0.12],
        "scale": 0.75,
    },
    "parts": [
        # Handle — long brown stick
        {"offset": [0, 0.22, 0], "size": [0.06, 0.60, 0.06], "color": [0.50, 0.35, 0.15, 1]},
        # Head — horizontal T-bar (darker stone)
        {"offset": [0, 0.54, 0], "size": [0.30, 0.08, 0.08], "color": [0.42, 0.42, 0.46, 1]},
        # Head top edge — darker cap
        {"offset": [0, 0.58, 0], "size": [0.28, 0.02, 0.06], "color": [0.35, 0.35, 0.40, 1]},
    ]
}
