"""Potion — glass bottle with colored liquid.

Change the color to create different potion types!
Red = health, Blue = mana, Green = speed.
"""

model = {
    "id": "potion",
    "height": 0.5,
    "parts": [
        # Bottle body — round, colored glass
        {"offset": [0, 0.12, 0], "size": [0.12, 0.20, 0.12], "color": [0.80, 0.20, 0.30, 1]},
        # Neck — narrow
        {"offset": [0, 0.24, 0], "size": [0.06, 0.08, 0.06], "color": [0.70, 0.15, 0.20, 1]},
        # Cork — brown cap
        {"offset": [0, 0.29, 0], "size": [0.08, 0.04, 0.08], "color": [0.50, 0.45, 0.35, 1]},
    ]
}
