"""Grass — green-topped earth block.

The surface block of most biomes. Green top with brown sides.
"""

model = {
    "id": "grass",
    "height": 1.0,
    "parts": [
        # Brown sides and bottom
        {"offset": [0, 0.46, 0], "size": [0.80, 0.72, 0.80], "color": [0.45, 0.32, 0.18, 1]},
        # Green top layer
        {"offset": [0, 0.84, 0], "size": [0.80, 0.08, 0.80], "color": [0.35, 0.65, 0.20, 1]},
    ]
}
