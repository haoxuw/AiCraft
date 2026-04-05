"""Wood Axe — wooden handle with a wedge-shaped blade on one side.

A tool for chopping trees and wood blocks.
"""

model = {
    "id": "wood_axe",
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
        # Axe head — wedge blade offset to one side
        {"offset": [0.08, 0.52, 0], "size": [0.18, 0.14, 0.06], "color": [0.55, 0.55, 0.58, 1]},
        # Blade edge — thin sharp edge
        {"offset": [0.18, 0.52, 0], "size": [0.04, 0.16, 0.04], "color": [0.48, 0.48, 0.52, 1]},
    ]
}
