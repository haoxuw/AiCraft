"""Wood Shovel — wooden handle with a flat rounded head.

A tool for digging dirt, sand, and gravel.
"""

model = {
    "id": "wood_shovel",
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
        # Blade — flat rounded shovel head
        {"offset": [0, 0.56, 0], "size": [0.14, 0.16, 0.04], "color": [0.55, 0.55, 0.58, 1]},
        # Blade tip — slightly narrower at bottom
        {"offset": [0, 0.50, 0], "size": [0.10, 0.04, 0.04], "color": [0.48, 0.48, 0.52, 1]},
    ]
}
