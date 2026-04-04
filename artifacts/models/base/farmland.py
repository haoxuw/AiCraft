"""Farmland — tilled dirt block with furrow lines.

Created by using a hoe on dirt. Crops grow on farmland.
"""

model = {
    "id": "farmland",
    "height": 1.0,
    "parts": [
        # Tilled soil — dark brown
        {"offset": [0, 0.46, 0], "size": [0.80, 0.72, 0.80], "color": [0.35, 0.22, 0.10, 1]},
        # Furrow lines — darker top grooves
        {"offset": [0, 0.84, 0], "size": [0.80, 0.04, 0.80], "color": [0.28, 0.18, 0.08, 1]},
    ]
}
