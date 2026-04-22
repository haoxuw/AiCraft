"""Stair — L-step. Bottom slab + upper quarter on one side."""

model = {
    "id": "stair",
    "height": 1.0,
    "parts": [
        # Bottom full slab
        {"offset": [0,    0.25, 0],    "size": [0.80, 0.40, 0.80], "color": [0.68, 0.52, 0.30, 1]},
        # Upper quarter (bias toward +Z so it reads as an L in preview)
        {"offset": [0,    0.70, 0.20], "size": [0.80, 0.40, 0.40], "color": [0.60, 0.44, 0.24, 1]},
    ]
}
