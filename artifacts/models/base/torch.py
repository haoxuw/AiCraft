"""Torch — wooden stick with burning flame."""

model = {
    "id": "torch",
    "height": 0.6,
    # Held in hand: rotated so tool head points forward
    "equip": {
        "rotation": [-30, 0, 0],
        "offset": [0, 0.0, -0.05],
        "scale": 0.75,
    },
    "parts": [
        # Stick
        {"offset": [0, 0.12, 0], "size": [0.06, 0.28, 0.06], "color": [0.40, 0.28, 0.12, 1]},
        # Flame (bright orange)
        {"offset": [0, 0.28, 0], "size": [0.08, 0.08, 0.08], "color": [1.00, 0.80, 0.20, 1]},
        # Flame tip (yellow glow)
        {"offset": [0, 0.34, 0], "size": [0.04, 0.06, 0.04], "color": [1.00, 0.90, 0.40, 0.8]},
        # Wrap (cloth around stick base)
        {"offset": [0, 0.22, 0], "size": [0.07, 0.04, 0.07], "color": [0.52, 0.38, 0.18, 1]},
    ]
}
