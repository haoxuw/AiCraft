"""Potion — glass bottle with colored liquid.

Change the color to create different potion types!
Red = health, Blue = mana, Green = speed.
"""

model = {
    "id": "potion",
    "equip": {"rotation": [-10.0, 0.0, 0.0], "offset": [0.0, 0.0, -0.03]},
    "parts": [
        {"offset": [0.0, 0.078, 0.0], "size": [0.078, 0.13, 0.078], "color": [0.8, 0.2, 0.3, 1.0]},
        {"offset": [0.0, 0.156, 0.0], "size": [0.039, 0.052, 0.039], "color": [0.7, 0.15, 0.2, 1.0]},
        {"offset": [0.0, 0.1885, 0.0], "size": [0.052, 0.026, 0.052], "color": [0.5, 0.45, 0.35, 1.0]},
    ],
}
