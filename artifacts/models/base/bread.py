"""Bread — golden loaf with darker crust and score marks.

A hearty food item that restores hunger.
"""

model = {
    "id": "bread",
    "height": 0.3,
    "parts": [
        # Loaf body — golden bread
        {"offset": [0, 0.06, 0], "size": [0.24, 0.12, 0.14], "color": [0.78, 0.62, 0.30, 1]},
        # Top crust — darker baked surface
        {"offset": [0, 0.12, 0], "size": [0.22, 0.02, 0.12], "color": [0.65, 0.45, 0.18, 1]},
        # Score mark left — diagonal slash
        {"offset": [-0.05, 0.12, 0], "size": [0.06, 0.02, 0.10], "color": [0.82, 0.68, 0.38, 1]},
        # Score mark center
        {"offset": [0, 0.12, 0], "size": [0.06, 0.02, 0.10], "color": [0.82, 0.68, 0.38, 1]},
        # Score mark right
        {"offset": [0.05, 0.12, 0], "size": [0.06, 0.02, 0.10], "color": [0.82, 0.68, 0.38, 1]},
    ]
}
